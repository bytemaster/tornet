#include <tornet/channel.hpp>
#include <tornet/connection.hpp>
#include <fc/log.hpp>
#include <fc/exception.hpp>
#include <fc/mutex.hpp>
#include <fc/unique_lock.hpp>

#include <boost/assert.hpp>

namespace tn {

  class channel::impl : public fc::retainable {
    public:
       impl( connection* c )
       :con(c,true),closed(false){}
       ~impl() {
          wlog( "channel_impl" );
        }

       channel::recv_handler rc;
       connection::ptr       con;   
       bool                  closed;
       uint16_t              rport;
       uint16_t              lport;
       fc::mutex             mtx;
  };

  channel::channel( connection* con, uint16_t r, uint16_t l )
  :my(new impl(con)) {
    my->rport   = r;
    my->lport   = l;
  }
  channel::channel( channel&& c )
  :my(fc::move(c.my)){}

  channel::channel( const channel& c ) 
  :my(c.my){}

  channel::channel() { }
  channel::~channel() {   }
  channel& channel::operator=(const channel& c ) {
    my = c.my;
    return *this;
  }

  node& channel::get_node()const {
    BOOST_ASSERT(my);
    return my->con->get_node();
  }
  void channel::close() {
    slog("close!!" );
    if( my ) {
    //  boost::unique_lock<fc::mutex> lock( my->mtx );
        elog( "close channel ~connection %p", my->con.get() );
        if( my->con ) my->con->close_channel(*this);
        /*
        if( !my->con.expired() ) {
            connection::ptr c(my->con);
            if( c )
              c->close_channel(*this);
        }
        */
      my->rc = channel::recv_handler();
      my->closed = true;
      my.reset();
    }
  }
  void channel::reset() {
    slog( "reset!" );
    my->con.reset(); 
  }

  channel::node_id  channel::remote_node()const { 
    BOOST_ASSERT(my);
    return my->con->get_remote_id();
  } 

  uint8_t  channel::remote_rank()const { 
    BOOST_ASSERT(my);
    return my->con->get_remote_rank();
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
    fc::unique_lock<fc::mutex> lock( my->mtx );
    my->rc = rc;
  }

  void channel::recv( const tn::buffer& b, channel::error_code ec ) {
    BOOST_ASSERT(my);
    // race condition between on_recv and recv both accessing
    // the receive callback
    fc::unique_lock<fc::mutex> lock( my->mtx );
    if( my->rc )  {
      my->rc(b,ec);
    }
    // note, if recv callback calls close() my could be NULL now
    
  }

  void channel::send( const tn::buffer& b ) {
    if( !my ) 
      FC_THROW_MSG( "Channel freed!" );
      my->con->send(*this,b);
  }

  channel::operator bool()const {
    if( !!my ) {
      return !my->closed;
    }
    return false;
  }
  bool channel::operator==(const channel& c )const {
    return my == c.my;
  }

} // namespace tn
