#include "name_service_impl.hpp"
#include <tornet/name_service_client.hpp>
#include <tornet/db/name.hpp>
#include <tornet/tornet_app.hpp>
#include <fc/fwd_impl.hpp>


namespace tn {
  struct block_vote{
     block_vote(){}
     block_vote( const fc::sha1& i, const tn::name_block& b )
     :id(i),count(1),block(b){}
     fc::sha1             id;
     int                  count;
     tn::name_block       block;
  };


  /**
   *  Ask my peers for the head block id.
   *  
   *  
   *
   *  Then after downloading the head block, I will randomly select
   *  nodes from my contact list and request all of the transactions
   *  for the head node.
   *
   *  Then I will randomly pick a node to request the previous block and
   *  its transactions from and I wll repeat this process until
   *  I come to the gensis block or a block that I already know.
   *
   *  After downloading all transactions and blocks, then I will replay
   *  the transactions and validate the results updating the name_record
   *  database in the process.
   *
   *  After everything has been validated and my 'root node' is the same
   *  as the rest of the network then I can start generating my own 
   *  transactions.
   *
   *  Downloading is a long-running process with many intermediate states 
   *  and is therefore maintained in its own state object.
   *
   *  Once connected to the network, this node will be receiving live 
   *  transaction updates.  These transactions get logged and broadcast.
   */

  fc::sha1 name_service::impl::find_head_block() {
    auto ap = tn::get_node()->active_peers();

    fc::vector<block_vote> votes;
     
    // traverse peers from farthest to closest
    for( int i = ap.size()-1; i >= 0; --i ) {
      try { // a connection error could occur
         auto nsc = get_node()->get_client<name_service_client>(ap[i].id());
         auto nb  = nsc->fetch_head().wait().block; 
         fc::sha1::encoder enc; 
         fc::raw::pack( enc, nb );
         auto r = enc.result();
         int v = 0;
         for( v = 0; v < votes.size(); ++v ) {
          if( votes[v].id == r ) {
            votes[v].count++; 
            break;
          }
         }
         if( v == votes.size() ) {
            votes.push_back(block_vote(r,nb));
         }
      }catch(...) {
        wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
      }
    }

    if( !votes.size() ) {
      wlog( "unable to find any nodes to download latest name chain" );
      // TODO: start new chain!
      return fc::sha1();
    }

    if( votes.size() > 1 ) {
      wlog( "there appears to be some disagreement about the head node" );
    }

    int majority = 0;
    for( int m = 1; m < votes.size(); ++m ) {
      if( votes[m].count > votes[majority].count ) majority = m;
    }
    _name_db->store( votes[majority].block );  

    return votes[majority].id;
  }

  void name_service::download_block_chain() {
     auto head_sha = my->find_head_block();

     tn::name_block cur_block;
     my->download_block(head_sha,cur_block);

     while( cur_block.prev_block_id != fc::sha1() ) {
        my->download_block(cur_block.prev_block_id,cur_block);
     }
      
     auto ap = tn::get_node()->active_peers();

  }

}
