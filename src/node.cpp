#include <tornet/node.hpp>
#include <tornet/channel.hpp>
#include "node_impl.hpp"
#include <fc/bigint.hpp>
//#include <fc/signals.hpp>
#include <fc/error.hpp>
#include <fc/stream.hpp>

namespace tn {

  typedef detail::node_private node_private;

  node::node( ) {
    my = new node::impl( *this );
  }

  node::~node() {
    delete my;
  }

  fc::thread&          node::get_thread()const { return my->_thread; }
  const node::id_type& node::get_id()const     { return my->_id;     }

  void                 node::close() {
    if( !my->_thread.is_current() ) {
      my->_thread.async( [this](){ close(); } ).wait();
      return;
    }
    // TODO ... 
    auto itr = my->_ep_to_con.begin();
    while( itr != my->_ep_to_con.end() ) {
      itr->second->close();
      ++itr;
    }
  }


  void node::init( const fc::path& datadir, uint16_t port ) {
    if( !my->_thread.is_current() ) {
       my->_thread.async( [&,this](){ init( datadir, port ); } ).wait();
       return;
    }

    my->_datadir = datadir;
    fc::path kf = datadir/"identity";
    if( !fc::exists( datadir ) ) {
      slog( "Creating new data directory: %s", datadir.string().c_str() );
      fc::create_directories(datadir);
    }
    if( !fc::exists(kf) ) {
      slog( "Creating new node identity: %s", kf.string().c_str() );
      fc::ofstream os( kf.string().c_str(), std::ios::out | std::ios::binary );
      fc::generate_keys( my->_pub_key,my->_priv_key );
      os << my->_pub_key << my->_priv_key;
      os.write( (char*)my->_nonce, sizeof(my->_nonce) );
      os.write( (char*)my->_nonce_search, sizeof(my->_nonce_search) );
    } else {
      fc::ifstream ink;
      ink.open( kf.string().c_str(), std::ios::in | std::ios::binary );
      ink >> my->_pub_key >> my->_priv_key;
      ink.read( (char*)my->_nonce, sizeof(my->_nonce) );
      ink.read( (char*)my->_nonce_search, sizeof(my->_nonce_search) );

      fc::sha1::encoder  rank_sha;
      rank_sha.write( (char*)my->_nonce, sizeof(my->_nonce) );
      rank_sha << my->_pub_key;
      fc::sha1 r = rank_sha.result();
      my->_rank = 161 - fc::bigint( r.data(), sizeof(r) ).log2();
    }

    my->_id = fc::sha1::hash(my->_pub_key); 
    my->_kbuckets.set_id(my->_id);

    // load peers
    my->_peers = new db::peer( my->_id, datadir/"peers" );
    my->_peers->init();

    my->_publish_db = new db::publish( datadir/"publish_db" );
    my->_publish_db->init();

    my->listen(port);
  }

  fc::sha1 node::connect_to( const endpoint& ep, const endpoint& nat_ep ) {
    if( !my->_thread.is_current() ) {
       return my->_thread.async( [&,this](){ return connect_to( ep, nat_ep ); } ).wait();
    }
    elog( "connect to %s via %s", fc::string(ep).c_str(), fc::string(nat_ep).c_str() );

    ep_to_con_map::iterator ep_con = my->_ep_to_con.find(ep);
    if( ep_con != my->_ep_to_con.end() ) { return ep_con->second->get_remote_id(); }

    ep_to_con_map::iterator nat_con = my->_ep_to_con.find(nat_ep);
    if( nat_con == my->_ep_to_con.end() || nat_con->second->get_state() != connection::connected ) { 
      FC_THROW_MSG( "No active connection to NAT endpoint %s", nat_ep );
    }

    connection::ptr con(new connection( *this, ep, my->_peers ));
    my->_ep_to_con[ep] = con;
    con->send_punch();

    nat_con->second->request_reverse_connect(ep);

    // wait for the reverse connection...
    while ( true ) { // keep waiting for the state to change
      switch( con->get_state() ) {
        case connection::failed:
          FC_THROW_MSG( "Attempt to connect to %s failed", ep );
        case connection::connected:
          //slog( "returning %1%", con->get_remote_id() );
          return con->get_remote_id(); 
        default: try {
          // con->advance();
          // as long as we are advancing on our own, keep waiting for connected.   
          while( fc::wait( con->state_changed, fc::milliseconds(1000) ) != connection::connected ) ;
        } catch ( const fc::future_wait_timeout& e ) {
          slog( "timeout... advance!" );
          throw;
        }
      }
    }
  }



  node::id_type node::connect_to( const node::endpoint& ep ) {
    if( !my->_thread.is_current() ) {
       return my->_thread.async( [&,this](){ return connect_to( ep ); } ).wait();
    }

    ep_to_con_map::iterator itr = my->_ep_to_con.find(ep);
    connection::ptr con;
    if( itr == my->_ep_to_con.end() ) {
      connection::ptr c(new connection( *this, ep, my->_peers ));
      my->_ep_to_con[ep] = c;
      itr = my->_ep_to_con.find(ep);
    }
    con = itr->second;
    while ( true ) { // keep waiting for the state to change
      switch( con->get_state() ) {
        case connection::failed:
          FC_THROW_MSG( "Attempt to connect to %s failed", ep );
        case connection::connected:
          //slog( "returning %1%", con->get_remote_id() );
          return con->get_remote_id(); 
        default: try {
          con->advance();
          // as long as we are advancing on our own, keep waiting for connected.   
          while( fc::wait( con->state_changed, fc::milliseconds(250) ) != connection::connected ) ;
        } catch ( const fc::future_wait_timeout& e ) {
          slog( "timeout... advance!" );
        }
      }
    }
  }

  channel node::open_channel( const fc::sha1& nid, uint16_t remote_chan_num ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return open_channel( nid, remote_chan_num ); } ).wait();
    }
    connection* c = my->get_connection(nid);
      //channel ch( itr->second->shared_from_this(),  remote_chan_num, get_new_channel_num() ); 
    channel ch( c,  remote_chan_num, c->get_free_channel_num() ); 
    c->add_channel(ch);
    return ch;
  } 


  /**
   *  This method should attempt to find n nodes near target that are no further than limit.
   *    
   *  The result is a list of nodes that this node is actively connected to. 
   *
   *  Nodes are stored by their 'distance' from this node in the _dist_to_con map.  
   *
   *  How do I determine which nodes to 'keep in memory' and which ones to 'flush to disk'?  
   *    - Keep all connections in memory that have a slot in the kbucket.
   *    - Save connections to DB that have been idle for 30 minutes
   *    - Free nodes from DB upon user request??
   */
  fc::vector<host> node::find_nodes_near( const id_type& target, uint32_t n, const fc::optional<id_type>& limit ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return find_nodes_near( target, n, limit ); } ).wait();
    }
    slog( "%s n: %d  limit: %s", fc::string(target).c_str(), n, (!!limit?fc::string(*limit).c_str():"none") );

    fc::vector<host> near;
    near.reserve(n);

    int bid = my->_kbuckets.get_bucket_id_for_target( target );
    slog( ".");
    while( bid >= 0 && n > near.size() ) {
        std::vector<connection*>& buck = my->_kbuckets.get_bucket_for_target(bid);
        //slog( "bucket id %d  size %d", bid, buck.size() );
        auto b= buck.begin();
        while( b != buck.end() && n > near.size() ) {
          if( !!limit && (*b)->get_remote_id() >= *limit ) {
            wlog( "ignoring due to limit" );
            ++b;
            continue;
          }
          slog( "found node %s", fc::string((*b)->get_remote_id()).c_str() ); 
          near.push_back( host( (*b)->get_remote_id(), 
                                (*b)->get_endpoint() ) );;

          if( (*b)->is_behind_nat() ) {
            near.back().nat_hosts.resize(1);
          }
          ++b;
        }
        --bid;
    }
    slog( "returning %d nodes", near.size());
    return near;
  }





  fc::ip::endpoint node::local_endpoint( const fc::ip::endpoint& dst )const {
    auto ep = dst;
    if( dst == fc::ip::endpoint() ) {
      ep = fc::ip::endpoint( fc::ip::address("74.125.228.40"), 8000 );
    }
    my->_lookup_sock.connect( ep );
    auto lp = my->_lookup_sock.local_endpoint();
    lp.set_port( my->_sock.local_endpoint().port() );
    return lp;
  }


  fc::vector<host> node::remote_nodes_near( const id_type& rnode, const id_type& target, uint32_t n, 
                                          const fc::optional<id_type>& limit  ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return remote_nodes_near( rnode, target, n, limit ); } ).wait();
    }
    connection* con = my->get_connection( rnode ); 
    return con->find_nodes_near( target, n, limit );
  }

  void node::start_service( uint16_t cn, const fc::string& name, const node::new_channel_handler& cb ) {
    if( !my->_thread.is_current() ) {
      my->_thread.async( [&,this](){ return start_service( cn, name, cb ); } ).wait();
      return;
    }
    if( !my->_services.insert( service( cn, name, cb ) ).second )
      FC_THROW_MSG( "Unable to start service '%s' on channel '%s' because channel '%s' is in use.", name.c_str(), cn, cn );
    slog( "Starting service '%s' on channel %d", name.c_str(), cn );
  }

  void node::close_service( uint16_t cn ) {
    if( !my->_thread.is_current() ) {
      my->_thread.async( [&,this](){ close_service(cn); } ).wait();
      return;
    }
    // TODO
  }

  /**
   *  Unlike accessing the peer database, this only returns currently
   *  connected peers and includes data not yet saved in the peer db.
   */
  fc::vector<db::peer::record> node::active_peers()const {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [this](){ return active_peers(); } ).wait();
    }
    fc::vector<db::peer::record> recs(my->_dist_to_con.size());
    auto itr = my->_dist_to_con.begin();
    auto end = my->_dist_to_con.end();
    int i = 0;
    while( itr != end ) {
      recs[i] = itr->second->get_db_record();
      ++i;
      ++itr;
    }
    return recs;
  }

  /// TODO: Flush status from active connections
  db::peer::ptr node::get_peers()const { 
    return my->_peers;
  }

  /**
   *  Starts a thread looking for nonce's that result in a higher node rank.
   *
   *  @param effort - the percent of a thread to apply to this effort.
   */
  void node::start_rank_search( double effort ) {
    /*
    my->rank_search_effort = effort;
    if( !my->rank_search_thread ) {
      my->rank_search_thread = boost::cmt::thread::create("rank");
      my->rank_search_thread->async( boost::bind( &node_private::rank_search, my ) );
    }
    */
  }

/*
  void node::cache_object( const id_type& node_id, const fc::string& key, const fc::any& v ) { 
    if( !my->_thread.is_current() ) {
      my->_thread.async( [&,this](){ return cache_object(node_id,key,v); } ).wait();
      return;
    }
    // TODO
  }
  boost::any  node::get_cached_object( const id_type& node_id, const fc::string& key )const {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return get_cached_object(node_id,key); } ).wait();
    }
    // TODO
  }
  */

  uint32_t node::rank()const { return my->_rank; }


  /**
   *  The connection is responsible for updating the node index that maps ids to active connections.
   */
  void                     node::update_dist_index( const id_type& nid, connection* c ) {
    elog( "%s %p", fc::string(nid).c_str(), c );
    auto dist = nid ^ my->_id;
    auto itr = my->_dist_to_con.find(dist);
    if( c ) {
        if( itr == my->_dist_to_con.end() ) {
          my->_kbuckets.add(c);
          my->_dist_to_con[dist] = c; // add it
        } else {
          if( itr->second != c ) {
              itr->second->close();
              wlog( "Already have a connection to node %1%, closing it", nid );
              itr->second = c;
          }
        }
    } else {  // clear the connection
        if( itr != my->_dist_to_con.end() ) {
    //      ep_to_con_map::iterator epitr = m_ep_to_con.find( itr->second->get_endpoint() );
    //      if( epitr != m_ep_to_con.end() ) { 
    //        m_ep_to_con.erase(epitr); 
    //      }
          my->_kbuckets.remove(itr->second);
          my->_dist_to_con.erase(itr);
        }
    }
  }

  /**
   *  Creates a new channel for connection if there is a service with lcn.  Otherwise it
   *  throws an error that no one is listening on that channel for new connections.
   *
   *  @param c   - the connection that packets on this channel are routed over
   *  @param rcn - remote channel number
   *  @param lcn - local channel number
   */
  channel                  node::create_channel( connection* c, uint16_t rcn, uint16_t lcn ) {
    auto itr = my->_services.find( lcn );
    auto e = my->_services.end();
    if( itr == e ) 
      FC_THROW_MSG( "Unable to open channel '%s', no services listening on that port", lcn );
    channel nc( c, rcn, lcn );
    itr->handler( nc );
    return nc;
  }
  void                     node::send( const char* d, uint32_t l, const fc::ip::endpoint& e ) {
    my->_sock.send_to( d, l, e );
  }

  fc::signature_t          node::sign( const fc::sha1& h ) {
    fc::signature_t s;
    my->_priv_key.sign(h,s);
    return s;
  }

  const fc::public_key_t&  node::pub_key()const {
    return my->_pub_key;
  }

  const fc::private_key_t& node::priv_key()const {
    return my->_priv_key;
  }

  uint64_t* node::nonce()const {
    return my->_nonce;
  }

} // namespace tornet
