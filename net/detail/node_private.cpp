#include <tornet/net/detail/node_private.hpp>
#include <boost/cmt/asio.hpp>
#include <boost/cmt/signals.hpp>
#include <boost/cmt/asio/udp/socket.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

namespace boost { namespace asio { namespace ip {  
std::size_t hash_value( const boost::asio::ip::udp::endpoint& ep ) {
  std::size_t seed = 0;
  boost::hash_combine( seed, ep.address().to_v4().to_ulong() );
  boost::hash_combine( seed, ep.port() );
  return seed;
}
} } } 

namespace tornet { namespace detail {
  using namespace boost::asio::ip;

  node_private::node_private( node& n, boost::cmt::thread& t)
  :m_node(n), m_thread(t), m_done(false),m_next_chan_num(10000),rank_search_thread(0),m_rank(0) {
  
  }

  node_private::~node_private() {
    wlog( "cleaning up node" );
    try {
       // for each connection, send close message
       close();

        m_done = true;
        // todo:  close all connections
        if( m_sock )
            m_sock->cancel(); 
        m_thread.quit();
    } catch ( const boost::exception& e ) {
      wlog( "Unexpected exception %1%", boost::diagnostic_information(e) );
    } catch ( const boost::system::system_error& e ) {
      slog( "Expected exception %1%", boost::diagnostic_information(e) );
    } catch ( const std::exception& e ) {
      wlog( "Unexpected exception %1%", boost::diagnostic_information(e) );
    }
  }

  void node_private::close() {
    wlog( "" );
    if( &boost::cmt::thread::current() != &m_thread ) {
      m_thread.async<void>( boost::bind(&node_private::close, this  ) ).wait();
      return;
    }
    ep_to_con_map::iterator itr = m_ep_to_con.begin();
    while( itr != m_ep_to_con.end() ) {
      itr->second->close();
      ++itr;
    }
  }

  void node_private::init( const boost::filesystem::path& datadir, uint16_t port ) {
    if( &boost::cmt::thread::current() != &m_thread ) {
      m_thread.async<void>( boost::bind(&node_private::init, this, datadir, port ) ).wait();
      return;
    }
    m_datadir = datadir;
    boost::filesystem::path kf = datadir/"identity";
    if( !boost::filesystem::exists( datadir ) ) {
      slog( "Creating new data directory: %1%", datadir );
      boost::filesystem::create_directories(datadir);
    }
    if( !boost::filesystem::exists(kf) ) {
      slog( "Creating new node identity: %1%", kf );
      std::ofstream os;
      os.open( kf.native().c_str(), std::ios::out | std::ios::binary );
      scrypt::generate_keys( m_pub_key,m_priv_key );
      os << m_pub_key << m_priv_key;
      os.write( (char*)m_nonce, sizeof(m_nonce) );
      os.write( (char*)m_nonce_search, sizeof(m_nonce_search) );
    } else {
      std::ifstream ink;
      ink.open( kf.native().c_str(), std::ios::in | std::ios::binary );
      ink >> m_pub_key >> m_priv_key;
      ink.read( (char*)m_nonce, sizeof(m_nonce) );
      ink.read( (char*)m_nonce_search, sizeof(m_nonce_search) );

      scrypt::sha1_encoder  rank_sha;
      rank_sha.write( (char*)m_nonce, sizeof(m_nonce) );
      rank_sha << m_pub_key;
      scrypt::sha1 r = rank_sha.result();
      m_rank = 161 - scrypt::bigint( (const char*)r.hash, sizeof(r.hash) ).log2();
    }

    scrypt::sha1_encoder sha; 
    sha << m_pub_key;
    m_id = sha.result();

    // load peers
    m_peers = boost::make_shared<db::peer>( m_id, datadir/"peers" );
    m_peers->init();
    m_publish_db = boost::make_shared<db::publish>( datadir/"publish_db" );
    m_publish_db->init();

    listen(port);
  }

  void node_private::listen( uint16_t port ) {
    m_sock = boost::shared_ptr<udp::socket>( new udp::socket( boost::cmt::asio::default_io_service() ) );
    m_sock->open( udp::v4() );
    m_sock->set_option(boost::asio::socket_base::receive_buffer_size(3*1024*1024) );
    m_sock->bind( udp::endpoint(boost::asio::ip::address(), port ) );
    slog( "Starting node %2% listening on port: %1%", m_sock->local_endpoint().port(), m_id );
    m_rl_complete = m_thread.async<void>( boost::bind( &node_private::read_loop, this ) );
  }

  void node_private::read_loop() {
    slog("");
    try {
      uint32_t count;
      while( !m_done ) {
         if( count % 60 == 0 )  // avoid an infinate loop flooding us!
            boost::cmt::usleep(400);

         // allocate a new buffer for each packet... we have no idea how long it may be around
         tornet::buffer b;
         boost::asio::ip::udp::endpoint from;
         size_t s = boost::cmt::asio::udp::receive_from( *m_sock, b.data(), b.size(), from );
         //slog( "%1% from %2%:%3%", s, from.address().to_string(), from.port() );

         if( s ) {
            b.resize( s );
            ++count;
            handle_packet( b, from ); 
         }
      }
    } catch ( const boost::exception& e ) { elog( "%1%", boost::diagnostic_information(e) ); }
  }

  void node_private::handle_packet( const tornet::buffer& b, const udp::endpoint& ep ) {
    boost::unordered_map<endpoint,connection::ptr>::iterator itr = m_ep_to_con.find(ep);
    if( itr == m_ep_to_con.end() ) {
      slog( "creating new connection" );
      // failing that, create
      connection::ptr c( new connection( *this, ep, m_peers ) );
      m_ep_to_con[ep] = c;
      c->handle_packet(b);
    } else { itr->second->handle_packet(b); }
  }

  
  void node_private::start_service( uint16_t num, const std::string& name, const node::new_channel_handler& cb ) {
    if( !m_services.insert( service( num, name, cb ) ).second )
      TORNET_THROW( "Unable to start service '%1%' on channel '%2%' because channel '%2%' is in use.", %name %num );
    slog( "Starting service '%1%' on channel %2%", name, num );
  }

  /**
   *  Creates a new channel for connection if there is a service with lcn.  Otherwise it
   *  throws an error that no one is listening on that channel for new connections.
   *
   *  @param c   - the connection that packets on this channel are routed over
   *  @param rcn - remote channel number
   *  @param lcn - local channel number
   */
  channel node_private::create_channel( const connection::ptr& c, uint16_t rcn, uint16_t lcn ) {
    service_set::iterator itr = m_services.find( lcn );
    if( itr == m_services.end() ) 
      TORNET_THROW( "Unable to open channel '%1%', no services listening on that port", %lcn );
    channel nc( c, rcn, lcn );
    itr->handler( nc );
    return nc;
  }

  void node_private::close_service( uint16_t c ) {
    service_set::iterator itr = m_services.find( c );
    if( itr != m_services.end() ) 
        m_services.erase(itr);
  }

  void node_private::send( const char* data, uint32_t s, const endpoint& ep ) {
      boost::cmt::asio::udp::send_to(*m_sock, data, s, ep );
  }

  void node_private::sign( const scrypt::sha1& digest, scrypt::signature_t& s ) {
    m_priv_key.sign(digest,s);
  }

  /**
   *  Find a connection for node id, create a channel and return it.
   */
  channel node_private::open_channel( const node_id& nid, uint16_t remote_chan_num, bool share ) {
    node_id dist = nid ^ m_id;
    std::map<node_id,connection*>::iterator itr = m_dist_to_con.find( dist );
    if( itr != m_dist_to_con.end() ) { 
      if( share ) {
        channel ch = itr->second->find_channel( remote_chan_num );
        if( ch ) 
          return ch;
      }
      
      channel ch( itr->second->shared_from_this(),  remote_chan_num, get_new_channel_num() ); 
      itr->second->add_channel(ch);
      return ch;
    } 

    // Start KAD search for the specified node
/*
    connection::ptr con = kad_find( nid );
    if( con ) {
      channel ch( con, remote_chan_num, get_new_channel_num() ); 
      con->add_channel(ch);
      return ch;
    }
*/

    TORNET_THROW( "No known connections to %1%", %nid );
    return channel();
  }


  node_private::node_id node_private::connect_to( const node::endpoint& ep ) {
    ep_to_con_map::iterator itr = m_ep_to_con.find(ep);
    connection::ptr con;
    if( itr == m_ep_to_con.end() ) {
      connection::ptr c(new connection( *this, ep, m_peers ));
      m_ep_to_con[ep] = c;
      itr = m_ep_to_con.find(ep);
    }
    con = itr->second;
    while ( true ) { // keep waiting for the state to change
      switch( con->get_state() ) {
        case connection::failed:
          TORNET_THROW( "Attempt to connect to %1%:%2% failed", %ep.address().to_string() %ep.port() );
        case connection::connected:
          slog( "returning %1%", con->get_remote_id() );
          return con->get_remote_id(); 
        default: try {
          con->advance();
          // as long as we are advancing on our own, keep waiting for connected.   
          while( boost::cmt::wait<connection::state_enum>( con->state_changed, 
                                        boost::chrono::milliseconds(250) ) != connection::connected ) ;
        } catch ( boost::cmt::error::future_wait_timeout& e ) {
          slog( "timeout... advance! %1%", boost::diagnostic_information(e) );
        }
      }
    }
  }


  /**
   *  The connection is responsible for updating the node index that maps ids to active connections.
   */
  void node_private::update_dist_index( const node_private::node_id& nid, connection* c ) {
    elog( "%1%", nid );
    node_id dist = nid ^ m_id;
    std::map<node_id,connection*>::iterator itr = m_dist_to_con.find(dist);
    if( c ) {
        if( itr == m_dist_to_con.end() ) {
          m_dist_to_con[dist] = c; // add it
        } else {
          if( itr->second != c ) {
              itr->second->close();
              wlog( "Already have a connection to node %1%, closing it", nid );
              itr->second = c;
          }
        }
    } else {  // clear the connection
        if( itr != m_dist_to_con.end() ) {
    //      ep_to_con_map::iterator epitr = m_ep_to_con.find( itr->second->get_endpoint() );
    //      if( epitr != m_ep_to_con.end() ) { 
    //        m_ep_to_con.erase(epitr); 
    //      }
          m_dist_to_con.erase(itr);
        }
    }
  }

  std::map<node::id_type,node::endpoint> node_private::find_nodes_near( const node::id_type& target, 
                                                                        uint32_t n, const boost::optional<node::id_type>& limit ) {
    std::map<node::id_type,node::endpoint>  near;
    std::map<node_id,connection*>::const_iterator itr =  m_dist_to_con.lower_bound( target ^ m_id );
    if( itr == m_dist_to_con.end() ) {
      int c  = 0;
      while( itr != m_dist_to_con.begin() && c < n){
        --itr;
        c++;
      }
    }

    wlog( "m_dist_to_con::size %1%  near.size %2%   n: %3%", m_dist_to_con.size(), near.size(), n );
    if( itr == m_dist_to_con.end() ) {
      elog( "no nodes closer..." );
      return near;
    }
    while( itr != m_dist_to_con.end() && near.size() < n ) {
      node::id_type dist = (itr->first^m_id)^target;
      // if( limit != node::id_type() || dist < limit ) {
      slog( "dist: %1%  target: %2%", dist, target );
        near[ dist  ] = itr->second->get_endpoint();
     // }
      ++itr;
    }
    return near;
  }

  connection* node_private::get_connection( const node::id_type& remote_id ) {
     std::map<node_id,connection*>::const_iterator itr = m_dist_to_con.find( remote_id ^ m_id );
     if( itr != m_dist_to_con.end() ) return itr->second;
     TORNET_THROW( "No known connection to %1%", %remote_id );
  }

  std::map<node::id_type,node::endpoint> node_private::remote_nodes_near( const node::id_type& remote_id, 
                                                                          const node::id_type& target, uint32_t n,
                                                                          const boost::optional<node::id_type>& limit ) {
    connection* con = get_connection( remote_id ); 
    return con->find_nodes_near( target, n, limit );
  }
  std::vector<db::peer::record> node_private::active_peers()const {
    std::vector<db::peer::record> recs(m_dist_to_con.size());
    std::map<node_id,connection*>::const_iterator itr = m_dist_to_con.begin();
    int i = 0;
    while( itr != m_dist_to_con.end() ) {
      recs[i] = itr->second->get_db_record();
      ++i;
      ++itr;
    }
    return recs;
  }

  uint32_t node_private::rank()const {
    return m_rank;
  }

  void node_private::rank_search() {
    uint64_t nonces[2];
    nonces[0]= m_nonce_search[0];
    nonces[1]= m_nonce_search[1];
    
    int cur_rank = rank();
    uint64_t cnt = 0;
    for( uint64_t i = nonces[0]; i < uint64_t(-1); ++i ) {
      nonces[0] = i;
      for( uint64_t n = nonces[1]; n < uint64_t(-1); ++n ) {
        ++cnt;
        nonces[1] = n;
        scrypt::sha1_encoder  rank_sha;
        rank_sha.write( (char*)nonces, sizeof(nonces) );
        rank_sha << m_pub_key;
        scrypt::sha1 r = rank_sha.result();
        int nrank = 161 - scrypt::bigint( (const char*)r.hash, sizeof(r.hash) ).log2();
        if( nrank > cur_rank ) {
          slog( "nonce: %1%  %2%", nonces[0], nonces[1] );
          slog( "Rank promoted from %1% to %2%", cur_rank, nrank );
          m_nonce[0] = nonces[0];
          m_nonce[1] = nonces[1];

          std::ofstream os;
          os.open( (m_datadir/"identity").native().c_str(), std::ios::out | std::ios::binary );
          os << m_pub_key << m_priv_key;
          os.write( (char*)m_nonce, sizeof(m_nonce) );

          cur_rank = nrank;
          m_rank   = nrank;
        }
        if( cnt % 100000000ll == 0 ) {
          slog( "%1%   %2%   %3%", i, double(i)/(uint64_t(-1)), n  );
          std::ofstream os;
          os.open( (m_datadir/"identity").native().c_str(), std::ios::out | std::ios::binary );
          os << m_pub_key << m_priv_key;
          os.write( (char*)m_nonce, sizeof(m_nonce) );
          os.write( (char*)m_nonce_search, sizeof(m_nonce_search) );
          m_nonce_search[0] = nonces[0];
          m_nonce_search[1] = nonces[1];
          
          boost::cmt::usleep(1000);
          // sleep an appropriate fraction to maintain desired level of effort
        }
      }
      nonces[1] = 0;
    }
  }

} } // tornet::detail
