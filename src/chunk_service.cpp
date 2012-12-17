#include <tornet/chunk_service.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/chunk_search.hpp>
#include <tornet/chunk_service_client.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/download_status.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/buffer.hpp>
#include <fc/raw.hpp>
#include <fc/stream.hpp>
#include <fc/json.hpp>
#include <fc/hex.hpp>
#include <fc/thread.hpp>
#include <stdio.h>


#include <boost/filesystem.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <tornet/node.hpp>

#include <fc/interprocess/file_mapping.hpp>
//#include <boost/interprocess/file_mapping.hpp>
//#include <boost/interprocess/mapped_region.hpp>


//#include "chunk_session.hpp"
//#include "reflect_chunk_session.hpp"
//#include <tn/error.hpp>
//#include "chunk_search.hpp"
#include <fc/blowfish.hpp>
#include <fc/super_fast_hash.hpp>

#include <tornet/service_ports.hpp>
#include "chunk_service_connection.hpp"

  /**
   *  Reverses the randomization performed by randomize
   *  @param seed - the value returned by randomize.
   */
  void      derandomize( uint64_t seed, fc::vector<char>& data );
  void      derandomize( uint64_t seed, const fc::mutable_buffer& b );
  /**
   *  Determins if data is sufficiently random for the purposes of
   *  the chunk service.
   */
  bool      is_random( const fc::vector<char>& data );
  /**
   *  Takes arbitrary data and 'randomizes' it returning the MT19937 key
   *  that results in a random sequence that satisifies is_random()
   *
   *  Modifies data using the random sequence.
   */
  uint64_t  randomize( fc::vector<char>& data, uint64_t init_seed );

namespace tn {

  class chunk_service::impl : public fc::retainable {
    public:
      impl(chunk_service& cs, const tn::node::ptr& n)
      :_self(cs),_node(n) {
        _publishing = false;
      }
      chunk_service&   _self;
      cafs             _cafs;
      tn::node::ptr    _node;
      db::chunk::ptr   _cache_db;
      db::chunk::ptr   _local_db;
      db::publish::ptr _pub_db;
      bool             _publishing;
      fc::future<void> _pub_loop_complete;

      void on_new_connection( const channel& c );
      void publish_loop();

      fc::vector<chunk_service_connection::ptr> _cons;
  };

  void chunk_service::impl::on_new_connection( const tn::channel& c ) {
      chunk_service_connection::ptr con( new chunk_service_connection( udt_channel(c,1024), _self ) );
      _cons.push_back(con);
  }


chunk_service::chunk_service( const fc::path& dbdir, const cafs& c, const tn::node::ptr& node )
:my( new impl(*this,node) ) {
    my->_cafs = c;
    fc::create_directories(dbdir/"cache_db");
    fc::create_directories(dbdir/"local_db");
    my->_cache_db.reset( new db::chunk( node->get_id(), dbdir/"cache_db" ) );
    my->_cache_db->init();
    my->_pub_db.reset( new db::publish( dbdir/"publish_db" ) );
    my->_pub_db->init();

    my->_node->start_service( chunk_service_udt_port, "chunkd", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
}

void chunk_service::shutdown() {
    enable_publishing( false );
    my->_node->close_service( chunk_service_udt_port );
    my->_cache_db.reset();
    my->_pub_db.reset();
}


chunk_service::~chunk_service(){
//  slog( "%p", this );
} 

db::chunk::ptr&     chunk_service::get_cache_db() { return my->_cache_db; }
db::publish::ptr&   chunk_service::get_publish_db() { return my->_pub_db; }
node::ptr&          chunk_service::get_node() { return my->_node; }
cafs                chunk_service::get_cafs()const { return my->_cafs; }


fc::vector<char> chunk_service::fetch_chunk( const fc::sha1& chunk_id ) {
  try {
    // TODO: should I increment the 'fetch count'??
    return my->_cafs.get_chunk( chunk_id );
  } catch ( fc::error_report& er ) {
    throw FC_REPORT_PUSH( er, "Unable to fetch chunk id ${chunk_id}", 
                              fc::value().set( "chunk_id", chunk_id ) );
  }
#if 0
  fc::vector<char> d;
  tn::db::chunk::meta met;
  try {
    if( my->_cache_db->fetch_meta( chunk_id, met, false ) ) {
       return my->_cafs.fetch_chunk( chunk_id ); 
    }
  } catch(...) {}
  FC_THROW_MSG( "Unknown chunk %s", chunk_id );
  return fc::vector<char>();
#endif
}


void chunk_service::enable_publishing( bool state ) {
  if( my->_publishing != state ) {
    my->_publishing = state;
    if( state ) {
        my->_pub_loop_complete = fc::async([this](){ my->publish_loop(); } );
    }
  }
}

bool chunk_service::is_publishing()const { return my->_publishing; }



void chunk_service::impl::publish_loop() {
  while( _publishing ) {
    fc::sha1 cid; 
    tn::db::publish::record next_pub;
    if ( _pub_db->fetch_next( cid, next_pub ) ) {

       auto time_till_update = next_pub.next_update - fc::time_point::now().time_since_epoch().count();

       if( time_till_update > 0 ) {
          //slog( "waiting %llu us for next publish update.", time_till_update );
          fc::usleep( fc::microseconds(time_till_update) );
       }

       // TODO: increase parallism of search?? This should be a background task 
       // so latency is not an issue... 

       //slog( "Searching for chunk %s", fc::string(cid).c_str() );
       tn::chunk_search::ptr csearch(new tn::chunk_search(_node,cid, 10, 1, true )); 
       csearch->start();
       csearch->wait();

      
       typedef std::map<fc::sha1,tn::host> chunk_map;
       typedef std::map<fc::sha1,fc::sha1> host_map;
       const host_map&  hn = csearch->hosting_nodes();

       next_pub.host_count = hn.size();
       // if the chunk was not found on the desired number of nodes, we need to find a node
       // to push it to.
       if( hn.size() < next_pub.desired_host_count ) {
            wlog( "Published chunk %s found on at least %d hosts, desired replication is %d",
                 to_string(cid).c_str(), hn.size(), next_pub.desired_host_count );
/*
            slog( "Hosting nodes: " );
            auto itr = hn.begin();
            while( itr != hn.end() ) {
              slog( "    node-dist: %s  node id: %s", 
                          fc::string(itr->first).c_str(), fc::string(itr->second).c_str() );
              ++itr;
            }
            slog( "Near nodes: " );
*/
            fc::optional<fc::sha1> store_on_node;

            const chunk_map  nn = csearch->current_results();
            for( auto nitr = nn.begin(); nitr != nn.end(); ++nitr ) {
 //             slog( "    node-dist: %s  node id: %s  %s", fc::string(nitr->first).c_str(), fc::string(nitr->second.id).c_str(), fc::string(nitr->second.ep).c_str() );
              if( hn.find( nitr->first ) == hn.end() && !store_on_node ) {
                if( !store_on_node ) store_on_node = nitr->second.id; 
              }
            }

            if( !store_on_node ) { wlog( "No new hosts available to store chunk" ); }
            else {
  //              slog( "Storing chunk %s on node %s", fc::string(cid).c_str(), fc::string(*store_on_node).c_str() );
                auto csc   = _node->get_client<tn::chunk_service_client>(*store_on_node);
                store_response r = csc->store( _self.fetch_chunk(cid) ).wait();
                if( r.result == 0 ) next_pub.host_count++;
  //              slog( "Response: %d", int(r.result));
            }
            next_pub.next_update = (fc::time_point::now() + fc::microseconds(30*1000*1000)).time_since_epoch().count();
            _pub_db->store( cid, next_pub );
       } else {
   //         wlog( "Published chunk %s found on at least %d hosts, desired replication is %d",
   //              to_string(cid).c_str(), hn.size(), next_pub.desired_host_count );

         // TODO: calculate next update interval for this chunk based upon its current popularity
         //       and host count and how far away the closest host was found.
         next_pub.next_update = 
          (fc::time_point::now() + fc::microseconds(30*1000*1000)).time_since_epoch().count();
         _pub_db->store( cid, next_pub );
      }
    
    } else {
      // TODO: do something smarter, like exit publish loop until there is something to publish
      fc::usleep( fc::microseconds(1000 * 1000*3)  );
      wlog( "nothing to publish..." );
    }


  }
}



fc::vector<char> chunk_service::download_chunk( const fc::sha1& chunk_id ) {
  try {
    try {
      return fetch_chunk( chunk_id );
    } catch ( fc::error_report& er ) {
       wlog( "Unable to fetch %s, searching for it..", fc::string(chunk_id).c_str() );
       tn::chunk_search::ptr csearch( new tn::chunk_search( get_node(), chunk_id, 5, 1, true ) );  
       csearch->start();
       csearch->wait();
    
       auto hn = csearch->hosting_nodes().begin();
       while( hn != csearch->hosting_nodes().end() ) {
          auto csc = get_node()->get_client<chunk_service_client>(hn->second);
    
          try {
              // give the node 5 minutes to send 1 MB
              fetch_response fr = csc->fetch( chunk_id, -1 ).wait( fc::microseconds( 1000*1000*300 ) );
              slog( "Response size %d", fr.data.size() );
    
              auto fhash = fc::sha1::hash( fr.data.data(), fr.data.size() );
              if( fhash == chunk_id ) {
                 // TODO: replace with CAFS get_local_db()->store_chunk(chunk_id,fr.data);
                 //my->_cafs.store( 
                 fc::vector<char> tmp = fc::move(fr.data);
                 return tmp;
              } else {
                  wlog( "Node failed to return expected chunk" );
                  wlog( "Received %s of size %d  expected  %s",
                         fc::string( fhash ).c_str(), fr.data.size(),
                         fc::string( chunk_id ).c_str() );
              }
          } catch ( ... ) {
             wlog( "Exception thrown while attempting to fetch %s from %s", 
                      fc::string(chunk_id).c_str(), fc::string(hn->second).c_str() );
             wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
          }
         ++hn;
       }
    }
  } catch ( fc::error_report& er ) {
    throw FC_REPORT_PUSH( er, "Unable to download chunk ${chunk_id}", fc::value().set("chunk_id", chunk_id) );
  }
}

} // namespace tn

