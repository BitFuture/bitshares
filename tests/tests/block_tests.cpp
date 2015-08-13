/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/witness_schedule_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

genesis_state_type make_genesis() {
   genesis_state_type genesis_state;

   genesis_state.initial_timestamp = time_point_sec( GRAPHENE_TESTING_GENESIS_TIMESTAMP );

   auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")));
   genesis_state.initial_active_witnesses = 10;
   for( int i = 0; i < genesis_state.initial_active_witnesses; ++i )
   {
      auto name = "init"+fc::to_string(i);
      genesis_state.initial_accounts.emplace_back(name,
                                                  init_account_priv_key.get_public_key(),
                                                  init_account_priv_key.get_public_key(),
                                                  true);
      genesis_state.initial_committee_candidates.push_back({name});
      genesis_state.initial_witness_candidates.push_back({name, init_account_priv_key.get_public_key()});
   }
   genesis_state.initial_parameters.current_fees->zero_all_fees();
   return genesis_state;
}

BOOST_AUTO_TEST_SUITE(block_tests)

BOOST_AUTO_TEST_CASE( block_database_test )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );

      block_database bdb;
      bdb.open( data_dir.path() );
      FC_ASSERT( bdb.is_open() );
      bdb.close();
      FC_ASSERT( !bdb.is_open() );
      bdb.open( data_dir.path() );

      signed_block b;
      for( uint32_t i = 0; i < 5; ++i )
      {
         if( i > 0 ) b.previous = b.id();
         b.witness = witness_id_type(i+1);
         bdb.store( b.id(), b );

         auto fetch = bdb.fetch_by_number( b.block_num() );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
         fetch = bdb.fetch_by_number( i+1 );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
         fetch = bdb.fetch_optional( b.id() );
         FC_ASSERT( fetch.valid() );
         FC_ASSERT( fetch->witness ==  b.witness );
      }

      for( uint32_t i = 1; i < 5; ++i )
      {
         auto blk = bdb.fetch_by_number( i );
         FC_ASSERT( blk.valid() );
         FC_ASSERT( blk->witness == witness_id_type(blk->block_num()) );
      }

      auto last = bdb.last();
      FC_ASSERT( last );
      FC_ASSERT( last->id() == b.id() );

      bdb.close();
      bdb.open( data_dir.path() );
      last = bdb.last();
      FC_ASSERT( last );
      FC_ASSERT( last->id() == b.id() );

      for( uint32_t i = 0; i < 5; ++i )
      {
         auto blk = bdb.fetch_by_number( i+1 );
         FC_ASSERT( blk.valid() );
         FC_ASSERT( blk->witness == witness_id_type(blk->block_num()) );
      }

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( generate_empty_blocks )
{
   try {
      fc::time_point_sec now( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      signed_block b;

      // TODO:  Don't generate this here
      auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      {
         database db;
         db.open(data_dir.path(), make_genesis );
         b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);

         for( uint32_t i = 1; i < 200; ++i )
         {
            BOOST_CHECK( db.head_block_id() == b.id() );
            witness_id_type prev_witness = b.witness;
            witness_id_type cur_witness = db.get_scheduled_witness(1).first;
            BOOST_CHECK( cur_witness != prev_witness );
            b = db.generate_block(db.get_slot_time(1), cur_witness, init_account_priv_key, database::skip_nothing);
            BOOST_CHECK( b.witness == cur_witness );
         }
         db.close();
      }
      {
         database db;
         db.open(data_dir.path(), []{return genesis_state_type();});
         BOOST_CHECK_EQUAL( db.head_block_num(), 200 );
         for( uint32_t i = 0; i < 200; ++i )
         {
            BOOST_CHECK( db.head_block_id() == b.id() );
            witness_id_type prev_witness = b.witness;
            witness_id_type cur_witness = db.get_scheduled_witness(1).first;
            BOOST_CHECK( cur_witness != prev_witness );
            b = db.generate_block(db.get_slot_time(1), cur_witness, init_account_priv_key, database::skip_nothing);
         }
         BOOST_CHECK_EQUAL( db.head_block_num(), 400 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( undo_block )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      {
         database db;
         db.open(data_dir.path(), make_genesis);
         fc::time_point_sec now( GRAPHENE_TESTING_GENESIS_TIMESTAMP );
         std::vector< time_point_sec > time_stack;

         auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
         for( uint32_t i = 0; i < 5; ++i )
         {
            now = db.get_slot_time(1);
            time_stack.push_back( now );
            auto b = db.generate_block( now, db.get_scheduled_witness( 1 ).first, init_account_priv_key, database::skip_nothing );
         }
         BOOST_CHECK( db.head_block_num() == 5 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 4 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 3 );
         BOOST_CHECK( db.head_block_time() == now );
         db.pop_block();
         time_stack.pop_back();
         now = time_stack.back();
         BOOST_CHECK( db.head_block_num() == 2 );
         BOOST_CHECK( db.head_block_time() == now );
         for( uint32_t i = 0; i < 5; ++i )
         {
            now = db.get_slot_time(1);
            time_stack.push_back( now );
            auto b = db.generate_block( now, db.get_scheduled_witness( 1 ).first, init_account_priv_key, database::skip_nothing );
         }
         BOOST_CHECK( db.head_block_num() == 7 );
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( fork_blocks )
{
   try {
      fc::temp_directory data_dir1( graphene::utilities::temp_directory_path() );
      fc::temp_directory data_dir2( graphene::utilities::temp_directory_path() );

      database db1;
      db1.open(data_dir1.path(), make_genesis);
      database db2;
      db2.open(data_dir2.path(), make_genesis);
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      for( uint32_t i = 0; i < 10; ++i )
      {
         auto b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
         try {
            PUSH_BLOCK( db2, b );
         } FC_CAPTURE_AND_RETHROW( ("db2") );
      }
      for( uint32_t i = 10; i < 13; ++i )
      {
         auto b =  db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
      }
      string db1_tip = db1.head_block_id().str();
      uint32_t next_slot = 3;
      for( uint32_t i = 13; i < 16; ++i )
      {
         auto b =  db2.generate_block(db2.get_slot_time(next_slot), db2.get_scheduled_witness(next_slot).first, init_account_priv_key, database::skip_nothing);
         next_slot = 1;
         // notify both databases of the new block.
         // only db2 should switch to the new fork, db1 should not
         PUSH_BLOCK( db1, b );
         BOOST_CHECK_EQUAL(db1.head_block_id().str(), db1_tip);
         BOOST_CHECK_EQUAL(db2.head_block_id().str(), b.id().str());
      }

      //The two databases are on distinct forks now, but at the same height. Make a block on db2, make it invalid, then
      //pass it to db1 and assert that db1 doesn't switch to the new fork.
      signed_block good_block;
      BOOST_CHECK_EQUAL(db1.head_block_num(), 13);
      BOOST_CHECK_EQUAL(db2.head_block_num(), 13);
      {
         auto b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
         good_block = b;
         b.transactions.emplace_back(signed_transaction());
         b.transactions.back().operations.emplace_back(transfer_operation());
         b.sign( init_account_priv_key );
         BOOST_CHECK_EQUAL(b.block_num(), 14);
         GRAPHENE_CHECK_THROW(PUSH_BLOCK( db1, b ), fc::exception);
      }
      BOOST_CHECK_EQUAL(db1.head_block_num(), 13);
      BOOST_CHECK_EQUAL(db1.head_block_id().str(), db1_tip);

      // assert that db1 switches to new fork with good block
      BOOST_CHECK_EQUAL(db2.head_block_num(), 14);
      PUSH_BLOCK( db1, good_block );
      BOOST_CHECK_EQUAL(db1.head_block_id().str(), db2.head_block_id().str());
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( undo_pending )
{
   try {
      fc::temp_directory data_dir( graphene::utilities::temp_directory_path() );
      {
         database db;
         db.open(data_dir.path(), make_genesis);

         auto init_account_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
         public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
         const graphene::db::index& account_idx = db.get_index(protocol_ids, account_object_type);

         transfer_operation t;
         t.to = account_id_type(1);
         t.amount = asset( 10000000 );
         {
            signed_transaction trx;
            set_expiration( db, trx );

            trx.operations.push_back(t);
            PUSH_TX( db, trx, ~0 );

            auto b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1).first, init_account_priv_key, ~0);
         }

         signed_transaction trx;
         set_expiration( db, trx );
         account_id_type nathan_id = account_idx.get_next_id();
         account_create_operation cop;
         cop.registrar = GRAPHENE_TEMP_ACCOUNT;
         cop.name = "nathan";
         cop.owner = authority(1, init_account_pub_key, 1);
         cop.active = cop.owner;
         trx.operations.push_back(cop);
         //sign( trx,  init_account_priv_key  );
         PUSH_TX( db, trx );

         auto b = db.generate_block(db.get_slot_time(1), db.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);

         BOOST_CHECK(nathan_id(db).name == "nathan");

         trx.clear();
         set_expiration( db, trx );
         t.fee = asset(1);
         t.from = account_id_type(1);
         t.to = nathan_id;
         t.amount = asset(5000);
         trx.operations.push_back(t);
         db.push_transaction(trx, ~0);
         trx.clear();
         set_expiration( db, trx );
         trx.operations.push_back(t);
         db.push_transaction(trx, ~0);

         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 10000);
         db.clear_pending();
         BOOST_CHECK(db.get_balance(nathan_id, asset_id_type()).amount == 0);
      }
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( switch_forks_undo_create )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() ),
                         dir2( graphene::utilities::temp_directory_path() );
      database db1,
               db2;
      db1.open(dir1.path(), make_genesis);
      db2.open(dir2.path(), make_genesis);
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      set_expiration( db1, trx );
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = GRAPHENE_TEMP_ACCOUNT;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      PUSH_TX( db1, trx );

      // generate blocks
      // db1 : A
      // db2 : B C D

      auto aw = db1.get_global_properties().active_witnesses;
      auto b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);

      BOOST_CHECK(nathan_id(db1).name == "nathan");

      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
      db1.push_block(b);
      aw = db2.get_global_properties().active_witnesses;
      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
      db1.push_block(b);

      GRAPHENE_CHECK_THROW(nathan_id(db1), fc::exception);

      PUSH_TX( db2, trx );

      aw = db2.get_global_properties().active_witnesses;
      b = db2.generate_block(db2.get_slot_time(1), db2.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
      db1.push_block(b);

      BOOST_CHECK(nathan_id(db1).name == "nathan");
      BOOST_CHECK(nathan_id(db2).name == "nathan");
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( duplicate_transactions )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() ),
                         dir2( graphene::utilities::temp_directory_path() );
      database db1,
               db2;
      db1.open(dir1.path(), make_genesis);
      db2.open(dir2.path(), make_genesis);
      BOOST_CHECK( db1.get_chain_id() == db2.get_chain_id() );

      auto skip_sigs = database::skip_transaction_signatures | database::skip_authority_check;

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      signed_transaction trx;
      set_expiration( db1, trx );
      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      PUSH_TX( db1, trx, skip_sigs );

      trx = decltype(trx)();
      set_expiration( db1, trx );
      transfer_operation t;
      t.to = nathan_id;
      t.amount = asset(500);
      trx.operations.push_back(t);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      PUSH_TX( db1, trx, skip_sigs );

      GRAPHENE_CHECK_THROW(PUSH_TX( db1, trx, skip_sigs ), fc::exception);

      auto b = db1.generate_block( db1.get_slot_time(1), db1.get_scheduled_witness( 1 ).first, init_account_priv_key, skip_sigs );
      PUSH_BLOCK( db2, b, skip_sigs );

      GRAPHENE_CHECK_THROW(PUSH_TX( db1, trx, skip_sigs ), fc::exception);
      GRAPHENE_CHECK_THROW(PUSH_TX( db2, trx, skip_sigs ), fc::exception);
      BOOST_CHECK_EQUAL(db1.get_balance(nathan_id, asset_id_type()).amount.value, 500);
      BOOST_CHECK_EQUAL(db2.get_balance(nathan_id, asset_id_type()).amount.value, 500);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( tapos )
{
   try {
      fc::temp_directory dir1( graphene::utilities::temp_directory_path() );
      database db1;
      db1.open(dir1.path(), make_genesis);

      const account_object& init1 = *db1.get_index_type<account_index>().indices().get<by_name>().find("init1");

      auto init_account_priv_key  = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")) );
      public_key_type init_account_pub_key  = init_account_priv_key.get_public_key();
      const graphene::db::index& account_idx = db1.get_index(protocol_ids, account_object_type);

      auto b = db1.generate_block( db1.get_slot_time(1), db1.get_scheduled_witness( 1 ).first, init_account_priv_key, database::skip_nothing);

      signed_transaction trx;
      //This transaction must be in the next block after its reference, or it is invalid.
      trx.set_expiration( db1.get_slot_time(1) );
      trx.set_reference_block( db1.head_block_id() );

      account_id_type nathan_id = account_idx.get_next_id();
      account_create_operation cop;
      cop.registrar = init1.id;
      cop.name = "nathan";
      cop.owner = authority(1, init_account_pub_key, 1);
      cop.active = cop.owner;
      trx.operations.push_back(cop);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      db1.push_transaction(trx);
      b = db1.generate_block(db1.get_slot_time(1), db1.get_scheduled_witness(1).first, init_account_priv_key, database::skip_nothing);
      trx.clear();

      transfer_operation t;
      t.to = nathan_id;
      t.amount = asset(50);
      trx.operations.push_back(t);
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      //relative_expiration is 1, but ref block is 2 blocks old, so this should fail.
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db1, trx, database::skip_transaction_signatures | database::skip_authority_check ), fc::exception);
      set_expiration( db1, trx );
      trx.signatures.clear();
      trx.sign( init_account_priv_key, db1.get_chain_id() );
      db1.push_transaction(trx, database::skip_transaction_signatures | database::skip_authority_check);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( optional_tapos, database_fixture )
{
   try
   {
      ACTORS( (alice)(bob) );

      generate_block();

      BOOST_TEST_MESSAGE( "Create transaction" );

      transfer( account_id_type(), alice_id, asset( 1000000 ) );
      transfer_operation op;
      op.from = alice_id;
      op.to = bob_id;
      op.amount = asset( 1000 );
      signed_transaction tx;
      tx.operations.push_back( op );
      set_expiration( db, tx );

      BOOST_TEST_MESSAGE( "ref_block_num=0, ref_block_prefix=0" );

      tx.ref_block_num = 0;
      tx.ref_block_prefix = 0;
      tx.signatures.clear();
      sign( tx, alice_private_key );
      PUSH_TX( db, tx );

      BOOST_TEST_MESSAGE( "proper ref_block_num, ref_block_prefix" );

      set_expiration( db, tx );
      tx.signatures.clear();
      sign( tx, alice_private_key );
      PUSH_TX( db, tx );

      BOOST_TEST_MESSAGE( "ref_block_num=0, ref_block_prefix=12345678" );

      tx.ref_block_num = 0;
      tx.ref_block_prefix = 0x12345678;
      tx.signatures.clear();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );

      BOOST_TEST_MESSAGE( "ref_block_num=1, ref_block_prefix=12345678" );

      tx.ref_block_num = 1;
      tx.ref_block_prefix = 0x12345678;
      tx.signatures.clear();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );

      BOOST_TEST_MESSAGE( "ref_block_num=9999, ref_block_prefix=12345678" );

      tx.ref_block_num = 9999;
      tx.ref_block_prefix = 0x12345678;
      tx.signatures.clear();
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( maintenance_interval, database_fixture )
{
   try {
      generate_block();
      BOOST_CHECK_EQUAL(db.head_block_num(), 2);

      fc::time_point_sec maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      auto initial_properties = db.get_global_properties();
      const account_object& nathan = create_account("nathan");
      upgrade_to_lifetime_member(nathan);
      const committee_member_object nathans_committee_member = create_committee_member(nathan);
      {
         account_update_operation op;
         op.account = nathan.id;
         op.new_options = nathan.options;
         op.new_options->votes.insert(nathans_committee_member.vote_id);
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      transfer(account_id_type()(db), nathan, asset(5000));

      generate_blocks(maintenence_time - initial_properties.parameters.block_interval);
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.maximum_transaction_size,
                        initial_properties.parameters.maximum_transaction_size);
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        db.head_block_time().sec_since_epoch() + db.get_global_properties().parameters.block_interval);
      // shuffling is now handled by the witness_schedule_object.
      BOOST_CHECK(db.get_global_properties().active_witnesses == initial_properties.active_witnesses);
      BOOST_CHECK(db.get_global_properties().active_committee_members == initial_properties.active_committee_members);

      generate_block();

      auto new_properties = db.get_global_properties();
      BOOST_CHECK(new_properties.active_committee_members != initial_properties.active_committee_members);
      BOOST_CHECK(std::find(new_properties.active_committee_members.begin(),
                            new_properties.active_committee_members.end(), nathans_committee_member.id) !=
                  new_properties.active_committee_members.end());
      BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().next_maintenance_time.sec_since_epoch(),
                        maintenence_time.sec_since_epoch() + new_properties.parameters.maintenance_interval);
      maintenence_time = db.get_dynamic_global_properties().next_maintenance_time;
      BOOST_CHECK_GT(maintenence_time.sec_since_epoch(), db.head_block_time().sec_since_epoch());
      db.close();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_FIXTURE_TEST_CASE( limit_order_expiration, database_fixture )
{ try {
   //Get a sane head block time
   generate_block();

   auto* test = &create_bitasset("TEST");
   auto* core = &asset_id_type()(db);
   auto* nathan = &create_account("nathan");
   auto* committee = &account_id_type()(db);

   transfer(*committee, *nathan, core->amount(50000));

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );

   limit_order_create_operation op;
   op.seller = nathan->id;
   op.amount_to_sell = core->amount(500);
   op.min_to_receive = test->amount(500);
   op.expiration = db.head_block_time() + fc::seconds(10);
   trx.operations.push_back(op);
   auto ptrx = PUSH_TX( db, trx, ~0 );

   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );

   auto ptrx_id = ptrx.operation_results.back().get<object_id_type>();
   auto limit_index = db.get_index_type<limit_order_index>().indices();
   auto limit_itr = limit_index.begin();
   BOOST_REQUIRE( limit_itr != limit_index.end() );
   BOOST_REQUIRE( limit_itr->id == ptrx_id );
   BOOST_REQUIRE( db.find_object(limit_itr->id) );
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 49500 );
   auto id = limit_itr->id;

   generate_blocks(op.expiration, false);
   test = &get_asset("TEST");
   core = &asset_id_type()(db);
   nathan = &get_account("nathan");
   committee = &account_id_type()(db);

   BOOST_CHECK(db.find_object(id) == nullptr);
   BOOST_CHECK_EQUAL( get_balance(*nathan, *core), 50000 );
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( double_sign_check, database_fixture )
{ try {
   generate_block();
   const auto& alice = account_id_type()(db);
   ACTOR(bob);
   asset amount(1000);

   set_expiration( db, trx );
   transfer_operation t;
   t.from = alice.id;
   t.to = bob.id;
   t.amount = amount;
   trx.operations.push_back(t);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();

   db.push_transaction(trx, ~0);

   trx.operations.clear();
   t.from = bob.id;
   t.to = alice.id;
   t.amount = amount;
   trx.operations.push_back(t);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();

   BOOST_TEST_MESSAGE( "Verify that not-signing causes an exception" );
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), fc::exception );

   BOOST_TEST_MESSAGE( "Verify that double-signing causes an exception" );
   sign( trx, bob_private_key );
   sign( trx, bob_private_key );
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), tx_duplicate_sig );

   BOOST_TEST_MESSAGE( "Verify that signing with an extra, unused key fails" );
   trx.signatures.pop_back();
   sign( trx, generate_private_key("bogus" ));
   GRAPHENE_REQUIRE_THROW( db.push_transaction(trx, 0), tx_irrelevant_sig );

   BOOST_TEST_MESSAGE( "Verify that signing once with the proper key passes" );
   trx.signatures.pop_back();
   db.push_transaction(trx, 0);
   sign( trx, bob_private_key );

} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( change_block_interval, database_fixture )
{ try {
   generate_block();

   db.modify(db.get_global_properties(), [](global_property_object& p) {
      p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
   });

   BOOST_TEST_MESSAGE( "Creating a proposal to change the block_interval to 1 second" );
   {
      proposal_create_operation cop = proposal_create_operation::committee_proposal(db.get_global_properties().parameters, db.head_block_time());
      cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + 10;
      committee_member_update_global_parameters_operation uop;
      uop.new_parameters.block_interval = 1;
      cop.proposed_ops.emplace_back(uop);
      trx.operations.push_back(cop);
      db.push_transaction(trx);
   }
   BOOST_TEST_MESSAGE( "Updating proposal by signing with the committee_member private key" );
   {
      proposal_update_operation uop;
      uop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
      uop.active_approvals_to_add = {get_account("init0").get_id(), get_account("init1").get_id(),
                                     get_account("init2").get_id(), get_account("init3").get_id(),
                                     get_account("init4").get_id(), get_account("init5").get_id(),
                                     get_account("init6").get_id(), get_account("init7").get_id()};
      trx.operations.push_back(uop);
      sign( trx, init_account_priv_key );
      /*
      sign( trx, get_account("init1" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init2" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init3" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init4" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init5" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init6" ).active.get_keys().front(),init_account_priv_key);
      sign( trx, get_account("init7" ).active.get_keys().front(),init_account_priv_key);
      */
      db.push_transaction(trx);
      BOOST_CHECK(proposal_id_type()(db).is_authorized_to_execute(db));
   }
   BOOST_TEST_MESSAGE( "Verifying that the interval didn't change immediately" );

   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);
   auto past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 5);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 10);

   BOOST_TEST_MESSAGE( "Generating blocks until proposal expires" );
   generate_blocks(proposal_id_type()(db).expiration_time + 5);
   BOOST_TEST_MESSAGE( "Verify that the block interval is still 5 seconds" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 5);

   BOOST_TEST_MESSAGE( "Generating blocks until next maintenance interval" );
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   generate_block();   // get the maintenance skip slots out of the way

   BOOST_TEST_MESSAGE( "Verify that the new block interval is 1 second" );
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.block_interval, 1);
   past_time = db.head_block_time().sec_since_epoch();
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 1);
   generate_block();
   BOOST_CHECK_EQUAL(db.head_block_time().sec_since_epoch() - past_time, 2);
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( pop_block_twice, database_fixture )
{
   try
   {
      uint32_t skip_flags = (
           database::skip_witness_signature
         | database::skip_transaction_signatures
         | database::skip_authority_check
         );

      const asset_object& core = asset_id_type()(db);

      // Sam is the creator of accounts
      private_key_type committee_key = init_account_priv_key;
      private_key_type sam_key = generate_private_key("sam");
      account_object sam_account_object = create_account("sam", sam_key);

      //Get a sane head block time
      generate_block( skip_flags );

      db.modify(db.get_global_properties(), [](global_property_object& p) {
         p.parameters.committee_proposal_review_period = fc::hours(1).to_seconds();
      });

      transaction tx;
      processed_transaction ptx;

      account_object committee_account_object = committee_account(db);
      // transfer from committee account to Sam account
      transfer(committee_account_object, sam_account_object, core.amount(100000));

      generate_block(skip_flags);

      create_account("alice");
      generate_block(skip_flags);
      create_account("bob");
      generate_block(skip_flags);

      db.pop_block();
      db.pop_block();
   } catch(const fc::exception& e) {
      edump( (e.to_detail_string()) );
      throw;
   }
}

BOOST_FIXTURE_TEST_CASE( witness_scheduler_missed_blocks, database_fixture )
{ try {
   db.get_near_witness_schedule();
   generate_block();
   auto near_schedule = db.get_near_witness_schedule();

   std::for_each(near_schedule.begin(), near_schedule.end(), [&](witness_id_type id) {
      generate_block(0);
      BOOST_CHECK(db.get_dynamic_global_properties().current_witness == id);
   });

   near_schedule = db.get_near_witness_schedule();
   generate_block(0, init_account_priv_key, 2);
   BOOST_CHECK(db.get_dynamic_global_properties().current_witness == near_schedule[2]);

   near_schedule.erase(near_schedule.begin(), near_schedule.begin() + 3);
   auto new_schedule = db.get_near_witness_schedule();
   new_schedule.erase(new_schedule.end() - 3, new_schedule.end());
   BOOST_CHECK(new_schedule == near_schedule);

   std::for_each(near_schedule.begin(), near_schedule.end(), [&](witness_id_type id) {
      generate_block(0);
      BOOST_CHECK(db.get_dynamic_global_properties().current_witness == id);
   });
} FC_LOG_AND_RETHROW() }

BOOST_FIXTURE_TEST_CASE( rsf_missed_blocks, database_fixture )
{
   try
   {
      generate_block();

      auto rsf = [&]() -> string
      {
         fc::uint128 rsf = db.get( witness_schedule_id_type() ).recent_slots_filled;
         string result = "";
         result.reserve(128);
         for( int i=0; i<128; i++ )
         {
            result += ((rsf.lo & 1) == 0) ? '0' : '1';
            rsf >>= 1;
         }
         return result;
      };

      auto pct = []( uint32_t x ) -> uint32_t
      {
         return uint64_t( GRAPHENE_100_PERCENT ) * x / 128;
      };

      BOOST_CHECK_EQUAL( rsf(),
         "1111111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), GRAPHENE_100_PERCENT );

      generate_block( ~0, init_account_priv_key, 1 );
      BOOST_CHECK_EQUAL( rsf(),
         "0111111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(127) );

      generate_block( ~0, init_account_priv_key, 1 );
      BOOST_CHECK_EQUAL( rsf(),
         "0101111111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(126) );

      generate_block( ~0, init_account_priv_key, 2 );
      BOOST_CHECK_EQUAL( rsf(),
         "0010101111111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(124) );

      generate_block( ~0, init_account_priv_key, 3 );
      BOOST_CHECK_EQUAL( rsf(),
         "0001001010111111111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(121) );

      generate_block( ~0, init_account_priv_key, 5 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000010001001010111111111111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(116) );

      generate_block( ~0, init_account_priv_key, 8 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000010000010001001010111111111111111111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(108) );

      generate_block( ~0, init_account_priv_key, 13 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000100000000100000100010010101111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1000000000000010000000010000010001001010111111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1100000000000001000000001000001000100101011111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1110000000000000100000000100000100010010101111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block();
      BOOST_CHECK_EQUAL( rsf(),
         "1111000000000000010000000010000010001001010111111111111111111111"
         "1111111111111111111111111111111111111111111111111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(95) );

      generate_block( ~0, init_account_priv_key, 64 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000000000000000000000000000000000000000000000000000000"
         "1111100000000000001000000001000001000100101011111111111111111111"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(31) );

      generate_block( ~0, init_account_priv_key, 32 );
      BOOST_CHECK_EQUAL( rsf(),
         "0000000000000000000000000000000010000000000000000000000000000000"
         "0000000000000000000000000000000001111100000000000001000000001000"
      );
      BOOST_CHECK_EQUAL( db.witness_participation_rate(), pct(8) );
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
