#include <tornet/net/detail/connection.hpp>
#include <tornet/net/channel.hpp>
#include <boost/cmt/log/log.hpp>
#include <boost/cmt/mutex.hpp>

namespace tornet {

  namespace detail {
      class channel_private {
        public:
          channel_private( const detail::connection::ptr& c )
          :con(c){}

          detail::connection::ptr con;   
          channel::recv_handler rc;
          uint16_t rport;
          uint16_t lport;
          boost::cmt::mutex mtx;
      };
  }

  channel::channel( const detail::connection::ptr& con, uint16_t r, uint16_t l )
  :my(new detail::channel_private(con)) {
    my->rport   = r;
    my->lport   = l;
  }

  channel::channel() { }

  channel::~channel() { }

  void channel::close() {
    if( my ) 
      my->con->close_channel(*this);
    my.reset();
  }

  channel::node_id  channel::remote_node()const { 
    BOOST_ASSERT(my);
    return my->con->get_remote_id(); 
  } 
  uint16_t channel::local_channel_num() const {
    BOOST_ASSERT(my);
    return my->lport;
  }
  uint16_t channel::remote_channel_num() const {
    BOOST_ASSERT(my);
    return my->rport;
  }

  void channel::on_recv( const recv_handler& rc ) {
    boost::unique_lock<boost::cmt::mutex> lock( my->mtx );
    BOOST_ASSERT(my);
    my->rc = rc;
  }

  void channel::recv( const tornet::buffer& b ) {
    // race condition between on_recv and recv both accessing
    // the receive callback
    boost::unique_lock<boost::cmt::mutex> lock( my->mtx );
    slog( "" );
    BOOST_ASSERT(my);
    if( my->rc )  {
      slog( " rhandler " );
      my->rc(b);
    }
  }

  void channel::send( const tornet::buffer& b ) {
    BOOST_ASSERT(my);
    my->con->send( *this, b );
  }

} // namespace tornet
