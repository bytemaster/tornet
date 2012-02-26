#include <tornet/net/detail/node_private.hpp>

namespace tornet {

  typedef detail::node_private node_private;

  node::node( ) {
    boost::cmt::thread* t = boost::cmt::thread::create("node");
    my = new node_private( *this, *t );
  }

  node::~node() {
    wlog( "" );
    delete my;
  }
  boost::cmt::thread& node::get_thread()const { return my->get_thread(); }

  const node::id_type& node::get_id()const { return my->get_id(); }

  void node::close() {
    wlog( "closing.... " );
    if( &boost::cmt::thread::current() != &my->get_thread() )
       my->get_thread().async<void>( boost::bind( &node_private::close, my ) ).wait();
    else
       my->close();
  }
  void node::init( const boost::filesystem::path& ddir, uint16_t port ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
       my->get_thread().async<void>( boost::bind( &node_private::init, my, ddir, port ) ).wait();
    else
       my->init(ddir,port);
  }

  node::id_type node::connect_to( const node::endpoint& ep ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
      return my->get_thread().async<node::id_type>( boost::bind( &node_private::connect_to, my, ep ) ).wait();
    return my->connect_to(ep);
  }

  channel node::open_channel( const id_type& node_id, uint16_t remote_chan_num ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
      return my->get_thread().async<channel>( boost::bind( &node_private::open_channel, my, node_id, remote_chan_num ) ).wait();
    return my->open_channel( node_id, remote_chan_num );
  }

  std::map<node::id_type,node::endpoint> node::find_nodes_near( const node::id_type& target, uint32_t n, const boost::optional<node::id_type>& limit ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
      return my->get_thread().async<std::map<node::id_type,node::endpoint> >( boost::bind( &node_private::find_nodes_near, my, target, n, limit ) ).wait();
    return my->find_nodes_near( target, n, limit );
  }
  std::map<node::id_type,node::endpoint> node::remote_nodes_near( const node::id_type& remote_id, const node::id_type& target, uint32_t n, const boost::optional<node::id_type>& limit ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
      return my->get_thread().async<std::map<node::id_type,node::endpoint> >( 
              boost::bind( &node_private::remote_nodes_near, my, remote_id, target, n, limit ) ).wait();
    return my->remote_nodes_near( remote_id, target, n, limit );
  }

  void node::start_service( uint16_t cn, const std::string& name, const node::new_channel_handler& cb ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
       my->get_thread().async<void>( boost::bind( &node_private::start_service, my, cn, name, cb ) ).wait();
    else
       my->start_service(cn,name,cb);
  }

  void node::close_service( uint16_t cn ) {
    if( &boost::cmt::thread::current() != &my->get_thread() )
       my->get_thread().async<void>( boost::bind( &node_private::close_service, my, cn ) ).wait();
    else
     my->close_service(cn);
  }

  /**
   *  Unlike accessing the peer database, this only returns currently
   *  connected peers and includes data not yet saved in the peer db.
   */
  std::vector<db::peer::record> node::active_peers()const {
    if( &boost::cmt::thread::current() != &my->get_thread() )
       return my->get_thread().async<std::vector<db::peer::record> >( boost::bind( &node_private::active_peers, my ) ).wait();
    return  my->active_peers();
  }

  /// TODO: Flush status from active connections
  db::peer::ptr node::get_peers()const { 
    return my->m_peers;
  }

  /**
   *  Starts a thread looking for nonce's that result in a higher node rank.
   *
   *  @param effort - the percent of a thread to apply to this effort.
   */
  void node::start_rank_search( double effort ) {
    my->rank_search_effort = effort;
    if( !my->rank_search_thread ) {
      my->rank_search_thread = boost::cmt::thread::create("rank");
      my->rank_search_thread->async( boost::bind( &node_private::rank_search, my ) );
    }
  }

  uint32_t node::rank()const { return my->rank(); }


} // namespace tornet
