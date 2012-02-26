#ifndef _TORNET_DETAIL_NODE_PRIVATE_HPP_
#define _TORNET_DETAIL_NODE_PRIVATE_HPP_
#include <tornet/net/detail/connection.hpp>
#include <tornet/net/node.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/cmt/future.hpp>

#include <scrypt/scrypt.hpp>
#include <scrypt/bigint.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <map>
#include <tornet/db/peer.hpp>

namespace tornet { namespace detail {
  using namespace boost::multi_index;

  struct service {
    struct by_name{};
    struct by_port{};

    service(){}
    service( uint16_t p, const std::string& n, const node::new_channel_handler& c )
    :port(p),name(n),handler(c){}
    uint16_t                  port;
    std::string               name;
    node::new_channel_handler handler; 
    bool operator < ( const service& s )const { return port < s.port; }
  };

  typedef multi_index_container< 
    service,
    indexed_by< //sequenced<>,
                ordered_unique<  member<service, uint16_t, &service::port > >,
                ordered_non_unique<  member<service, std::string, &service::name > >
             >
  > service_set; 


/** 
 *  
 */
class node_private {
  public:
    typedef boost::asio::ip::udp::endpoint endpoint;
    typedef scrypt::sha1                   node_id;
    typedef boost::unordered_map<endpoint,connection::ptr>  ep_to_con_map;    
    node_private( node& n, boost::cmt::thread& t );
    ~node_private();
    void    init( const boost::filesystem::path& ddir, uint16_t port );
    void    close();

    void    send( const char* data, uint32_t s, const endpoint& ep );
    void    sign( const scrypt::sha1& digest, scrypt::signature_t& );
            
    void    listen( uint16_t p );
    void    read_loop();
    channel open_channel( const node_id& node_id, uint16_t remote_chan_num );
    void    close_service(uint16_t p);
    channel create_channel( const connection::ptr&, uint16_t rcn, uint16_t lcn );


    boost::cmt::thread& get_thread()const { return m_thread; }
    void                handle_packet( const tornet::buffer& b, const endpoint& ep );

    // detects same id with multiple endpoints and NULL cs
    void update_dist_index( const node_id&, connection* c );

    const uint64_t* nonce()const { return m_nonce; }

    void start_service( uint16_t service_chan_num, const std::string& service_name, 
                           const node::new_channel_handler& on_new_channel );

    const scrypt::public_key_t& pub_key()const { return m_pub_key; }

    node_id connect_to( const endpoint& ep );

    const node_id& get_id()const { return m_id; }

    /**
     *  Searches through active connections and returns the endpoints closest to target
     *  sorted by distance from target.
     *
     *  TODO:  Add a method to query info about a given node.
     */
    std::map<node_id,endpoint> find_nodes_near( const node_id& target, uint32_t n, const boost::optional<node_id>& limit );

    /**
     *  Calls find_nodes_near on the remote node 'rnode' and returns the result.
     */
    std::map<node_id,endpoint> remote_nodes_near( const node_id& rnode, 
                                                  const node_id& target, uint32_t n, const boost::optional<node_id>& limit );

    connection* get_connection( const node::id_type& remote_id );


    std::vector<db::peer::record> active_peers()const;
    boost::shared_ptr<db::peer>                     m_peers;
    boost::cmt::thread*                             rank_search_thread;
    double                                          rank_search_effort;

    uint32_t rank()const;
    void     rank_search();
  private:
    uint16_t get_new_channel_num() { return ++m_next_chan_num; }
    node&                                           m_node;
    boost::cmt::thread&                             m_thread;
    bool                                            m_done;


    uint8_t                                         m_rank;
    uint16_t                                        m_next_chan_num;

    boost::filesystem::path                         m_datadir;
    scrypt::sha1                                    m_id; // sha1(m_pub_key)
    boost::cmt::future<void>                        m_rl_complete;
    uint64_t                                        m_nonce[2];
    uint64_t                                        m_nonce_search[2];
    scrypt::public_key_t                            m_pub_key;
    scrypt::private_key_t                           m_priv_key;
    ep_to_con_map                                   m_ep_to_con;    
    std::map<node_id,connection*>                   m_dist_to_con;
    boost::shared_ptr<boost::asio::ip::udp::socket> m_sock;     

    service_set                                     m_services;
};

} } // tornet::detail

#endif // _TORNET_DETAIL_NODE_PRIVATE_HPP_
