#include <tornet/rpc/udp_connection.hpp>

namespace tornet { namespace rpc {

class udp_connection_private {
  public:
      udp_connection&      self;
      tornet::channel      chan;

      udp_connection_private( rpc::udp_connection& s, const tornet::channel& c ) 
      :self( s ), chan( c ) {
        chan.on_recv( boost::bind(&udp_connection_private::on_recv, this, _1, _2 ) );
      }

      // called from node thread...
      void on_recv( const tornet::buffer& b, channel::error_code ec  ) {
        if( &boost::cmt::thread::current() != self.get_thread() ) {
          self.get_thread()->async( boost::bind( &udp_connection_private::on_recv, this, b, ec ) );
          return;
        }
        //slog( "%1% bytes  ec %2%", b.size(), int(ec) );

        if( !ec ) {
          tornet::rpc::datastream<const char*> ds(b.data(),b.size());
          tornet::rpc::message msg;
          tornet::rpc::raw::unpack(ds,msg);
        
       //   slog("Recv: %1%", json::io::to_string(msg));

          switch( msg.type ) {
            case message::notice: self.handle_notice(msg); return;
            case message::call:   self.handle_call(msg);   return;
            case message::result: self.handle_result(msg); return;
            case message::error:  self.handle_error(msg);  return;
            default: 
              wlog( "invalid message type %1%", int( msg.type ) );
          };
        } else {
          wlog( "RPC Session Closed" );
          self.break_promises();
          self.closed();
        }
      }
};

udp_connection::udp_connection( const tornet::channel& c, 
                                boost::cmt::thread* t ) 
:connection(t){
  my = new udp_connection_private(*this, c );
}

udp_connection::~udp_connection() {
  my->chan.close();
  delete my;
}

void udp_connection::send( const tornet::rpc::message& msg ) {
    tornet::buffer b;
    tornet::rpc::datastream<char*> ds(b.data(),b.size());
    tornet::rpc::raw::pack( ds, msg );
    b.resize( ds.tellp() ); 
    my->chan.send( b ); // posts to node thread if necessary
}
uint8_t      udp_connection::remote_rank()const { return my->chan.remote_rank(); }
scrypt::sha1 udp_connection::remote_node()const { return my->chan.remote_node(); }

} } // namespace tornet::rpc
