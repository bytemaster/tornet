#include <tornet/udt_test_service.hpp>
#include <tornet/node.hpp>
#include <tornet/udt_channel.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/thread.hpp>
#include <fc/signals.hpp>
#include <tornet/service_ports.hpp>

namespace tn {
  class udt_test_connection : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<udt_test_connection> ptr;

      udt_test_connection( udt_channel&& c );
      void read_loop();

      boost::signal<void()> closed;

    protected:
      virtual ~udt_test_connection(){}
      fc::future<void> _read_done;
      udt_channel      _chan;
  };

  class udt_test_service::impl : public fc::retainable{
    public:
      float         _effort;      
      fc::path      _dir;
      tn::node::ptr _node;
      // nodes subscribed to us
      fc::vector<udt_test_connection::ptr> _subscribers;

      // nodes we are subscribed to
      fc::vector<udt_test_connection::ptr> _sources;

      void subscribe_to_network();
      void on_new_connection( const channel& c );
  };


  udt_test_service::udt_test_service( const fc::shared_ptr<tn::node>& n )
  :my(new impl()){
    my->_node = n;

    my->_node->start_service( udt_test_port, "udt_test", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
  }

  udt_test_service::~udt_test_service() {
    my->_node->close_service( udt_test_port );
  }

  

  udt_test_connection::udt_test_connection( tn::udt_channel&& c )
  :_chan(fc::move(c)) {
    _read_done = fc::async([this](){ read_loop(); } );
  }
  
  void udt_test_connection::read_loop() {
    slog( "starting read loop" );
    try {
        size_t total = 0;
        while( true ) {
            fc::vector<char> buf(2048);
            size_t s = _chan.read( buf.data(), buf.size() );
            total += s;
           // slog( "read %d, echoing it total %d", s, total );
           // _chan.write( fc::const_buffer(buf.data(), buf.size()) );
        }
    } catch ( ... ) {
      elog( "exit read loop due to exception %s", fc::current_exception().diagnostic_information().c_str() );
    }
    closed();
  }

  void udt_test_service::impl::on_new_connection( const tn::channel& c ) {
      slog( "on new connection" );
      udt_test_connection* tc = new udt_test_connection( udt_channel(c) ); 
      tc->closed.connect( [=]() { 
        auto i = _subscribers.begin();
        while( i != _subscribers.end() ) {
          if( i->get() == tc ) {
            _subscribers.erase(i);
            return;
          }
          ++i;
        } } );
      _subscribers.push_back(tc);
  }
  

}
