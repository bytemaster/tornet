
#include <tornet/rpc/udt_connection.hpp>

namespace tornet { namespace rpc {

class udt_connection_private {
  public:
      udt_connection&          self;
      tornet::udt_channel      udt_chan;
      boost::cmt::future<void> read_done;
      bool                     m_done;

      udt_connection_private( rpc::udt_connection& s, const tornet::udt_channel& c ) 
      :self( s ), udt_chan( c ) {
        m_done    = false;
        read_done = self.get_thread()->async<void>( boost::bind(&udt_connection_private::read_loop,this) );
      }

      ~udt_connection_private() {
        m_done = true;
        read_done.wait(); 
      }

      void read_loop() {
        while( !m_done ) {
          slog( "starting read loop... " );
          tornet::rpc::message msg;
          slog( "starting unpack\n" );
          tornet::rpc::raw::unpack(udt_chan,msg);
          slog( "finished unpack\n" );
          switch( msg.type ) {
            case message::notice: self.handle_notice(msg); return;
            case message::call:   self.handle_call(msg);   return;
            case message::result: self.handle_result(msg); return;
            case message::error:  self.handle_error(msg);  return;
            default: 
              wlog( "invalid message type %1%", int( msg.type ) );
          };
        }
      }
};

udt_connection::udt_connection( const tornet::udt_channel& c, 
                                boost::cmt::thread* t ) 
:connection(t){
  my = new udt_connection_private(*this, c );
}

udt_connection::~udt_connection() {
  my->udt_chan.close();
  delete my;
}

void udt_connection::send( const tornet::rpc::message& msg ) {
    std::vector<char> d;
    tornet::rpc::raw::pack_vec( d, msg );
    slog( "starting to write message of size %1%", d.size() );
    my->udt_chan.write( boost::asio::buffer(d) ); // posts to node thread if necessary
    slog( "done writing message" );
}
uint8_t      udt_connection::remote_rank()const { return my->udt_chan.remote_rank(); }
scrypt::sha1 udt_connection::remote_node()const { return my->udt_chan.remote_node(); }

} } // namespace tornet::rpc
