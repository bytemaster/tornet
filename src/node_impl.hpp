#ifndef _NODE_IMPL_HPP
#define _NODE_IMPL_HPP
#include <tornet/node.hpp>
#include <fc/thread.hpp>
#include <fc/udp_socket.hpp>
#include <tornet/db/peer.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/connection.hpp>
#include <tornet/kbucket.hpp>
#include <boost/unordered_map.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

namespace fc { namespace ip {
inline std::size_t hash_value( const fc::ip::endpoint& ep ) {
  std::size_t seed = 0;
  boost::hash_combine( seed, uint32_t(ep.get_address()) );
  boost::hash_combine( seed, ep.port() );
  return seed;
}
} }  

namespace tn {
  using namespace boost::multi_index;
  typedef boost::unordered_map<fc::ip::endpoint,connection::ptr>  ep_to_con_map;    

  struct service {
    struct by_name{};
    struct by_port{};

    service(){}
    service( uint16_t p, const fc::string& n, const node::new_channel_handler& c )
    :port(p),name(n),handler(c){}
    uint16_t                  port;
    fc::string               name;
    node::new_channel_handler handler; 
    bool operator < ( const service& s )const { return port < s.port; }
  };

  typedef multi_index_container< 
    service,
    indexed_by< //sequenced<>,
                ordered_unique<  member<service, uint16_t, &service::port > >,
                ordered_non_unique<  member<service, fc::string, &service::name > >
             >
  > service_set; 


  class node::impl {
    public:
      impl( node& s ):_self(s),_thread("node"){
        _done = false;
        _rank = 0;
        _nonce[0] = _nonce[1] = 0;
        _lookup_sock.connect( fc::ip::endpoint( fc::ip::address("74.125.228.40"), 8000 ) );
        _processing = false;
      }
      ~impl() {
        slog( "start quit" );
        _thread.quit();
        slog( "don quit" );
      }

      node&                           _self;
      fc::thread                      _thread;
      fc::sha1                        _id;
      uint32_t                        _rank;
      uint64_t                        _nonce[2];
      uint64_t                        _nonce_search[2];
      fc::private_key_t               _priv_key;
      fc::public_key_t                _pub_key;
      service_set                     _services;
      fc::udp_socket                  _sock;
      fc::udp_socket                  _lookup_sock;
      fc::future<void>                _read_loop_complete;
      ep_to_con_map                   _ep_to_con;
      bool                            _done;
      fc::path                        _datadir;
      std::map<fc::sha1,connection*>  _dist_to_con;
      kbucket                         _kbuckets;


      /**
       *  Store connections that have messages queue that need
       *  processed.  This vector is a sorted 'priority heap' sorted
       *  by the nodes service priority.  
       */
      std::vector<connection*>        _process_queue;
      bool                            _processing;
      static bool pq_comparer( connection* l, connection* r ) { return l->priority() > r->priority(); }

      db::peer::ptr    _peers;
      db::publish::ptr _publish_db;

      void listen( uint16_t p ) {
         slog( "Listening on port %d", p );
        _sock.open();
        _sock.set_receive_buffer_size( 3*1024*1024 );
        _sock.bind( fc::ip::endpoint( fc::ip::address(), p ) );
        _read_loop_complete = _thread.async( [=](){ read_loop(); } );

      }
      void read_loop() {
        try {
          uint32_t count = 0;
          while( !_done ) {
             if( count % 60 == 0 )  // avoid an infinate loop flooding us!
                fc::usleep(fc::microseconds(400));

             // allocate a new buffer for each packet... we have no idea how long it may be around
             tn::buffer b;
             fc::ip::endpoint from;
             size_t s = _sock.receive_from( b.data(), b.size(), from );

             if( s ) {
                b.resize( s );
                ++count;
                handle_packet( fc::move(b), from ); 
             }
          }
        } catch ( ... ) { elog( "%s", fc::current_exception().diagnostic_information().c_str() ); }
      }

      void handle_packet( tn::buffer&& b, const fc::ip::endpoint& ep ) {
        auto itr = _ep_to_con.find(ep);
        if( itr == _ep_to_con.end() ) {
          slog( "creating new connection" );
          // failing that, create
          connection::ptr c( new connection( _self, ep, _peers ) );
          _ep_to_con[ep] = c;
          c->post_packet(std::move(b));
          process_connection(c.get());
        } else { 
          itr->second->post_packet(std::move(b)); 
          process_connection(itr->second.get());
        }
      }
      void process_connection( connection* c ) {
          if( c->pending_packets() > 1 ) return;
          _process_queue.push_back(c);
          std::push_heap( _process_queue.begin(), _process_queue.end(), &impl::pq_comparer );

          if( !_processing ) {
            _processing = true;
            fc::async( [=](){ process_queue(); }, "process_queue" );
          }
      }

      /**
       *  The read_loop fiber is pulling packets off of the network and 
       *  posting them into their respecitve connections queues.  Each connection
       *  is then put on a priority queue to be processed by process_queue fiber.
       *
       *  The process_queue fiber wants to ensure that all packets have been
       *  received before processing subsequent packets so that they can get
       *  processed in the proper order.
       *
       *  Because connections are sorted by priority incoming messages will be
       *  processed in priority order.  
       */
      void process_queue() {
         while( _process_queue.size() ) {
            _process_queue.front()->process_next_message();
            if( !_process_queue.front()->pending_packets() ) {
              std::pop_heap( _process_queue.begin(), _process_queue.end(), &impl::pq_comparer );
              _process_queue.pop_back();
            }
            // give the read_loop (or other running tasks a chance to make some
            // progress before continuing to the next message.
            fc::yield();
         }
         _processing = false;
      }


      connection* get_connection( const fc::sha1& remote_id )const {
         auto itr = _dist_to_con.find( remote_id ^ _id );
         if( itr != _dist_to_con.end() ) return itr->second;
         FC_THROW( "No known connection to %s", fc::string(remote_id).c_str() );
      }
  };

  





}

#endif
