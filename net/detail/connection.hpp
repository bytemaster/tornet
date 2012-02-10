#ifndef _TORNET_DETAIL_CONNECTION_HPP_
#define _TORNET_DETAIL_CONNECTION_HPP_
#include <scrypt/blowfish.hpp>
#include <scrypt/dh.hpp>
#include <scrypt/sha1.hpp>
#include <boost/unordered_map.hpp>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <tornet/net/buffer.hpp>
#include <boost/scoped_ptr.hpp>
#include <tornet/net/channel.hpp>
#include <tornet/db/peer.hpp>

#include <boost/cmt/future.hpp>

#include <boost/signals.hpp>

namespace tornet {

  namespace detail {
      class node_private;

      /**
       *  Manages internal state for a particular peer endpoint.
       *  Encrypts/Decrypts messages to and from this endpoint.
       *  Maintains channel map sending messages to the proper channel
       *
       *  Node receives message, looks for connection.
       *      If connection is found, passes it to connection object.
       *      If no current connection is found, the node will look for last-known
       *        state for this endpoint and create a connection in that state.
       *      
       *      When the connection authenticates a remote peer, it updates the peer index.
       *      When there is an error, the connection updates the peer index
       *      
       *      When the connection receives a message for an unknown channel, it asks the
       *        node to create a new channel
       */
      class connection : public boost::enable_shared_from_this<connection>{
        public:
            enum state_enum {
              failed        = -1, // enter this state if unable to advance
              uninit        = 0,  // nothing is known
              generated_dh  = 1,  // we have generated and sent a dh pub key
              received_dh   = 2,  // we have received a dh key, sent a dh key, and sent an auth
              authenticated = 3,  // we have received a valid authorization
              connected     = 4   // we have received a valid auth response
            };

            enum proto_message_type {
              data_msg           = 0,
              auth_msg           = 1,
              auth_resp_msg      = 2,
              route_lookup_msg   = 3,
              route_msg          = 4,
              close_msg          = 5
            };

            typedef boost::shared_ptr<connection> ptr;
            typedef boost::weak_ptr<connection>   wptr;
            typedef boost::asio::ip::udp::endpoint endpoint;
            typedef scrypt::sha1                   node_id;

            connection( detail::node_private& np, const endpoint& ep, const db::peer::ptr& pptr );
            connection( detail::node_private& np, const endpoint& ep, 
                        const node_id& auth_id, state_enum init_state = connected );
            ~connection();
            
            boost::cmt::thread& get_thread()const;

            endpoint   get_endpoint()const;
            node_id    get_remote_id()const;
            uint8_t    get_remote_rank()const { return m_record.rank; }
            state_enum get_state()const { return m_cur_state; }

            // return false if state transitioned to failed, otherwise true
            bool       auto_advance();
            void       advance();
            void       close();
            void       close_channels();

            void       reset();

            // multiplex based upon state
            void handle_packet( const tornet::buffer& b );
            bool decode_packet( const tornet::buffer& b );

            void handle_uninit( const tornet::buffer& b ); 
            void handle_generated_dh( const tornet::buffer& b ); 
            void handle_received_dh( const tornet::buffer& b ); 
            void handle_authenticated( const tornet::buffer& b ); 
            void handle_connected( const tornet::buffer& b ); 

            bool handle_data_msg( const tornet::buffer& b );
            bool handle_auth_msg( const tornet::buffer& b );
            bool handle_auth_resp_msg( const tornet::buffer& b );
            bool handle_close_msg( const tornet::buffer& b );
            bool handle_lookup_msg( const tornet::buffer& b );
            bool handle_route_msg( const tornet::buffer& b );

            void generate_dh();
            bool process_dh( const tornet::buffer& b );
            void send_dh();
            void send_auth();
            void send_auth_response(bool);

            void close_channel( const channel& c );
            void send_close();
            void send( const channel& c, const tornet::buffer& b );

            void set_remote_id( const node_id& nid );
            void send( const char* c, uint32_t l, proto_message_type t ); 

            void add_channel( const channel& c );


            std::map<node_id,endpoint> find_nodes_near( const node_id& target, uint32_t n );

            boost::signal<void(state_enum)> state_changed;
        private:
            void  goto_state( state_enum s );

            uint16_t                                  m_advance_count;
            node_private&                             m_node;
            
            boost::scoped_ptr<scrypt::diffie_hellman> m_dh;
            boost::scoped_ptr<scrypt::blowfish>       m_bf;
            state_enum                                m_cur_state;
            
            node_id                                   m_remote_id;
            endpoint                                  m_remote_ep;

            typedef std::map<node_id,endpoint> route_table;

            std::map<node_id, boost::cmt::promise<route_table>::ptr > route_lookups;
                
            
            boost::unordered_map<uint32_t,channel>    m_channels;
            db::peer::ptr                             m_peers;
            db::peer::record                          m_record;
      };
  }

}

#endif // _TORNET_CONNECTION_HPP_
