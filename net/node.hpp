#ifndef _TORNET_NODE_HPP_
#define _TORNET_NODE_HPP_
#include <tornet/net/channel.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/asio.hpp>
#include <tornet/db/peer.hpp>
#include <tornet/db/publish.hpp>

namespace tornet {
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
  class node {
    public:
      typedef boost::shared_ptr<node>               ptr;
      typedef scrypt::sha1                          id_type;
      typedef boost::asio::ip::udp::endpoint        endpoint;
      typedef boost::function<void(const channel&)> new_channel_handler;

      node();
      ~node();

      std::vector<db::peer::record> active_peers()const;

      const id_type& get_id()const;

      boost::cmt::thread& get_thread()const;

      db::peer::ptr get_peers()const;

      /**
       * @param ddir - data directory where identity information is stored.
       * @param port - send/recv messages via this port.
       */
      void init( const boost::filesystem::path& ddir, uint16_t port );

      void start_rank_search( double effort = 1 );
      uint32_t rank()const;

      void close();

      void        cache_object( const id_type& node_id, const std::string& key, const boost::any& v );
      boost::any  get_cached_object( const id_type& node_id, const std::string& key )const;

      /**
       *  Searches through active connections and returns the endpoints closest to target
       *  sorted by distance from target.
       *
       *  @param limit - the maximum distance to consider, or unlimited distance if limit is 0
       *
       *  TODO:  Add a method to query info about a given node.
       */
      std::map<id_type,endpoint> find_nodes_near( const id_type& target, uint32_t n, 
                                                  const boost::optional<id_type>& limit = boost::optional<id_type>() );

      /**
       *  Calls find_nodes_near on the remote node 'rnode' and returns the result.
       *
       *  @param limit - the maximum distance to consider, or unlimited distance if limit is 0
       */
      std::map<id_type,endpoint> remote_nodes_near( const id_type& rnode, 
                                                    const id_type& target, uint32_t n,
                                                    const boost::optional<id_type>& limit = boost::optional<id_type>() );

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
      void start_service( uint16_t service_chan_num, const std::string& service_name, 
                          const new_channel_handler& on_new_channel );

      /**
       *  Stop accepting new channels on service_port, does not close
       *  any open channels.
       */
      void close_service( uint16_t service_channel_num );

    private:
      detail::node_private* my; 
  };

};

#endif
