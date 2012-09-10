#ifndef _TORNET_NODE_HPP_
#define _TORNET_NODE_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/function.hpp>
#include <fc/vector.hpp>
#include <fc/sha1.hpp>
#include <fc/ip.hpp>
#include <fc/any.hpp>
#include <fc/optional.hpp>
#include <fc/pke.hpp>
#include <tornet/db/peer.hpp>
#include <tornet/host.hpp>

namespace fc { 
  class thread;
  class string;
  class path;


  template<typename T>
  class optional;
}

namespace tn {
  class     channel;
  class     connection;
  namespace detail { class node_private; }


  /**
   *  @class node
   *
   *  @brief Hosts services and manages connections to other nodes.
   *
   *  The node class is application / protocol neutral dealing only with data 
   *  channels.  
   *
   *  Often it is necessary to associate data and objects with a particular node 
   *  that share the lifetime of the node connection.  Instead of having multiple
   *  databases that must sychronize scope, the node provides a generic method of
   *  associating objects with the node that will be automatically freed when 
   *  communication with the node is no longer maintained.
   */
  class node : public fc::retainable {
    public:
      typedef fc::shared_ptr<db::peer>           peer_db_ptr;
      typedef fc::shared_ptr<node>               ptr;
      typedef fc::sha1                           id_type;
      typedef fc::ip::endpoint                   endpoint;
      typedef fc::function<void(const channel&)> new_channel_handler;

      node();
      ~node();

      fc::vector<db::peer::record> active_peers()const;

      const id_type& get_id()const;

      fc::thread&    get_thread()const;
      peer_db_ptr    get_peers()const;

      /**
       * @param ddir - data directory where identity information is stored.
       * @param port - send/recv messages via this port.
       */
      void     init( const fc::path& ddir, uint16_t port );

      void     start_rank_search( double effort = 1 );
      uint32_t rank()const;

      uint64_t* nonce()const;

      void     close();

      void     cache_object( const id_type& node_id, const fc::string& key, const fc::any& v );
      fc::any  get_cached_object( const id_type& node_id, const fc::string& key )const;

      /**
       *  Searches through active connections and returns the endpoints closest to target
       *  sorted by distance from target.
       *
       *  @param limit - the maximum distance to consider, or unlimited distance if limit is 0
       *  @param n     - the number of nodes to return
       *
       *  TODO:  Add a method to query info about a given node.
       */
      fc::vector<host> find_nodes_near( const id_type& target, uint32_t n, 
                                        const fc::optional<id_type>& limit = fc::optional<id_type>() );

      /**
       *  Calls find_nodes_near on the remote node 'rnode' and returns the result.
       *
       *  @param limit - the maximum distance to consider, or unlimited distance if limit is 0
       */
      fc::vector<host> remote_nodes_near( const id_type& rnode, const id_type& target, uint32_t n, 
                                          const fc::optional<id_type>& limit = fc::optional<id_type>() );

      /**
       *  Connect to the endpoint and return the ID of the node or throw on error.
       */
      id_type connect_to( const endpoint& ep );

      /**
       *  This method will attempt to connect to node_id and then create a new channel to node_port with
       *  the coresponding local_port for return messages.  
       *
       *  @param node_id     - the ID of the node that we wish to connect to.
       *  @param remote_port - the port the remote host is listening for incoming 
       *                       connections.  @ref listen
       *  @param share - return an existing channel to the given node_id and channel num
       *
       *  Throws if unable to resolve or connect to node_id
       */
      channel open_channel( const id_type& node_id, uint16_t remote_chan_num, bool share = true );

      /**
       *  Every time a new channel is created to this node_chan_num, @param on_new_channel is called.
       *  @param service_chan_num - the port accepting new channels
       *  @param service_name - the name of the service running on port (http,chunk,chat,dns,etc)
       *  @param on_new_channel - called from the node's thread when a new channel 
       *                          is created on service_chan_num
       *
       *  @return true if node_chan_num is unused, otherwise false;
       */
      void start_service( uint16_t service_chan_num, const fc::string& service_name, const new_channel_handler& on_new_channel );

      /**
       *  Stop accepting new channels on service_port, does not close
       *  any open channels.
       */
      void close_service( uint16_t service_channel_num );

      /**
       *  @return the endpoint that our UDP packets come from.
       */
      fc::ip::endpoint local_endpoint( const fc::ip::endpoint& dest = fc::ip::endpoint() )const;

    private:
      friend class connection;
      void                     update_dist_index( const id_type& id, connection* c );
      channel                  create_channel( connection* c, uint16_t rcn, uint16_t lcn );
      void                     send( const char* d, uint32_t l, const fc::ip::endpoint& );
      fc::signature_t          sign( const fc::sha1& h );
      const fc::public_key_t&  pub_key()const;
      const fc::private_key_t& priv_key()const;


      class impl;
      impl* my;
  };

};

#endif
