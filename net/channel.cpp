#include <tornet/net/detail/connection.hpp>
#include <tornet/net/channel.hpp>
#include <boost/cmt/log/log.hpp>
#include <boost/cmt/mutex.hpp>

namespace tornet {

  namespace detail {
      class channel_private {
        public:
          channel_private( const detail::connection::ptr& c )
          :con(c),closed(false){}

          detail::connection::wptr con;   
          channel::recv_handler rc;
          bool     closed;
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

  boost::cmt::thread* channel::get_thread()const {
    if( !my ) {wlog("my==0"); return 0;}
    if( my->con.expired() ) elog( "my->con expired" );
    detail::connection::ptr c(my->con);
    return &c->get_thread();
  }
  void channel::close() {
    slog("close!!" );
    if( my ) {
    //  boost::unique_lock<boost::cmt::mutex> lock( my->mtx );
        if( !my->con.expired() ) {
            detail::connection::ptr c(my->con);
            if( c )
              c->close_channel(*this);
        }
      my->rc = channel::recv_handler();
      my.reset();
    }
  }
  void channel::reset() {
    slog( "reset!" );
    my->con.reset(); 
  }

  channel::node_id  channel::remote_node()const { 
    BOOST_ASSERT(my);
    detail::connection::ptr c(my->con);
    if( c )
      return c->get_remote_id();
    TORNET_THROW( "Channel Closed" );  
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
    BOOST_ASSERT(my);
    boost::unique_lock<boost::cmt::mutex> lock( my->mtx );
    my->rc = rc;
  }

  void channel::recv( const tornet::buffer& b, channel::error_code ec ) {
    BOOST_ASSERT(my);
    // race condition between on_recv and recv both accessing
    // the receive callback
    boost::unique_lock<boost::cmt::mutex> lock( my->mtx );
    if( my->rc )  {
      my->rc(b,ec);
    }
    // note, if recv callback calls close() my could be NULL now
    
  }

  void channel::send( const tornet::buffer& b ) {
    if( !my ) 
      TORNET_THROW( "Channel freed!" );
    detail::connection::ptr c(my->con);
    if( c )
      c->send(*this,b);
    else
      TORNET_THROW( "Channel Closed" );  
  }

  channel::operator bool()const {
    if( my ) {
      if( !my->con.expired() ) 
        return true;
      // connection is gone! This channel is bogus!
      const_cast<channel*>(this)->my.reset();
    }
    return false;
  }
  bool channel::operator==(const channel& c )const {
    return my == c.my;
  }

} // namespace tornet
