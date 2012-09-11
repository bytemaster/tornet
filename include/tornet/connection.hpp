#ifndef _TORNET_CONNECTION_HPP_
#define _TORNET_CONNECTION_HPP_
#include <fc/fwd.hpp>
#include <fc/vector.hpp>
#include <fc/optional.hpp>
//#include <fc/any.hpp>
#include <tornet/db/peer.hpp>
#include <tornet/channel.hpp>
#include <tornet/host.hpp>
#include <fc/signals.hpp>


namespace tn {
  class node;
  class buffer;

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
  class connection : public fc::retainable {
    public:
        enum state_enum {
          failed        = -1, // enter this state if unable to advance
          uninit        = 0,  // nothing is known
          generated_dh  = 1,  // we have generated and sent a dh pub key
          received_dh   = 2,  // we have received a dh key, sent a dh key, and sent an auth
          authenticated = 3,  // we have received a valid authorization
          connected     = 4   // we have received a valid auth response
        };

        // max 5 bits
        enum proto_message_type {
          data_msg                 = 0,
          auth_msg                 = 1,
          auth_resp_msg            = 2,
          route_lookup_msg         = 3,
          route_msg                = 4,
          close_msg                = 5,
          update_rank              = 6,
          req_reverse_connect_msg  = 7,
          req_connect_msg          = 8
        };

        typedef fc::shared_ptr<connection> ptr;
        typedef fc::sha1                   node_id;

        connection( node& np, const fc::ip::endpoint& ep, const db::peer::ptr& pptr );
        connection( node& np, const fc::ip::endpoint& ep, const node_id& auth_id, state_enum init_state = connected );
        ~connection();
        
        node&       get_node()const;

        fc::ip::endpoint get_endpoint()const;
        node_id          get_remote_id()const;
        uint8_t          get_remote_rank()const;
        state_enum       get_state()const;
        bool             is_behind_nat()const;

        // return false if state transitioned to failed, otherwise true
        bool       auto_advance();
        void       advance();
        void       close();
        void       close_channels();

        void       reset();

        // multiplex based upon state
        void handle_packet( const tn::buffer& b );
        bool decode_packet( const tn::buffer& b );

        void handle_uninit( const tn::buffer& b ); 
        void handle_generated_dh( const tn::buffer& b ); 
        void handle_received_dh( const tn::buffer& b ); 
        void handle_authenticated( const tn::buffer& b ); 
        void handle_connected( const tn::buffer& b ); 

        bool handle_data_msg( const tn::buffer& b );
        bool handle_auth_msg( const tn::buffer& b );
        bool handle_auth_resp_msg( const tn::buffer& b );
        bool handle_close_msg( const tn::buffer& b );
        bool handle_lookup_msg( const tn::buffer& b );
        bool handle_route_msg( const tn::buffer& b );
        bool handle_update_rank_msg( const tn::buffer& b );

        // requests the remote host attempt to connect to ep
        void request_reverse_connect(const fc::ip::endpoint& ep ); 
        void send_request_connect( const fc::ip::endpoint& ep );
        bool handle_request_reverse_connect_msg( const tn::buffer& b );
        bool handle_request_connect_msg( const tn::buffer& b );

        void generate_dh();
        bool process_dh( const tn::buffer& b );
        void send_dh();
        void send_auth();
        void send_auth_response(bool);
        void send_update_rank();


        void close_channel( const channel& c );
        void send_close();
        void send( const channel& c, const tn::buffer& b );


        void set_remote_id( const node_id& nid );
        void send( const char* c, uint32_t l, proto_message_type t ); 

        void add_channel( const channel& c );
        channel find_channel( uint16_t remote_chan_num )const;

        fc::vector<tn::host> find_nodes_near( const node_id& target, uint32_t n, const fc::optional<node_id>& limit  );

        //fc::function<void(state_enum)> state_changed;
        boost::signal<void(state_enum)> state_changed;
        const db::peer::record&      get_db_record()const;

//        void    cache_object( const fc::string& key, const fc::any& v );
//        fc::any get_cached_object( const fc::string& key )const;

    private:
        void  goto_state( state_enum s );

        class impl;
        fc::fwd<impl,696> my;
  };
}

#endif // _TORNET_CONNECTION_HPP_
