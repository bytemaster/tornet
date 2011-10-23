#include <tornet/net/detail/node_private.hpp>

namespace tornet {

  typedef detail::node_private node_private;

  node::node( ) {
    boost::cmt::thread* t = boost::cmt::thread::create("node");
    my = new node_private( *this, *t );
  }

  node::~node() {
    delete my;
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

} // namespace tornet
