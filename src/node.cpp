#include <tornet/node.hpp>
#include <tornet/channel.hpp>
#include "node_impl.hpp"
#include <fc/bigint.hpp>
#include <fstream>

namespace tn {

  typedef detail::node_private node_private;

  node::node( ) {
    my = new node::impl( *this );
  }

  node::~node() {
    wlog( "" );
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
      std::ofstream os;
      os.open( kf.string().c_str(), std::ios::out | std::ios::binary );
      fc::generate_keys( my->_pub_key,my->_priv_key );
      os << my->_pub_key << my->_priv_key;
      os.write( (char*)my->_nonce, sizeof(my->_nonce) );
      os.write( (char*)my->_nonce_search, sizeof(my->_nonce_search) );
    } else {
      std::ifstream ink;
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

    fc::sha1::encoder sha; 
    sha << my->_pub_key;
    my->_id = sha.result();

    // load peers
    my->_peers = new db::peer( my->_id, datadir/"peers" );
    my->_peers->init();
    my->_publish_db = new db::publish( datadir/"publish_db" );
    my->_publish_db->init();

    my->listen(port);
  }

  node::id_type node::connect_to( const node::endpoint& ep ) {
    if( !my->_thread.is_current() ) {
       return my->_thread.async( [&,this](){ return connect_to( ep ); } ).wait();
    }
    // TODO ..
  }

  channel node::open_channel( const id_type& node_id, uint16_t remote_chan_num, bool share ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return open_channel( node_id, remote_chan_num, share ); } ).wait();
    }
    // TODO
  }

  fc::vector<host> node::find_nodes_near( const id_type& target, uint32_t n, const fc::optional<id_type>& limit ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return find_nodes_near( target, n, limit ); } ).wait();
    }
    // TODO
  }

  fc::vector<host> node::remote_nodes_near( const id_type& rnode, const id_type& target, uint32_t n, 
                                          const fc::optional<id_type>& limit  ) {
    if( !my->_thread.is_current() ) {
      return my->_thread.async( [&,this](){ return remote_nodes_near( rnode, target, n, limit ); } ).wait();
    }
    // TODO 
  }

  void node::start_service( uint16_t cn, const fc::string& name, const node::new_channel_handler& cb ) {
    if( !my->_thread.is_current() ) {
      my->_thread.async( [&,this](){ return start_service( cn, name, cb ); } ).wait();
      return;
    }
    // TODO
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
    // TODO
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

  uint32_t node::rank()const { return my->_rank; }


  void                     node::update_dist_index( const id_type& id, connection* c ) {

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
      FC_THROW( "Unable to open channel '%d', no services listening on that port", lcn );
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
