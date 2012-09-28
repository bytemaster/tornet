#include <tornet/httpd.hpp>
#include <fc/tcp_socket.hpp>
#include <fc/tcp_server.hpp>

namespace tn {

   class http_connection {
      public:
      http_connection( httpd::impl& h )
      :_httpd(h){}

      void process();

      fc::future<void> _complete;
      httpd::impl&     _httpd;
      fc::tcp_socket   _sock;
   };
  
   class httpd::impl {
    public:
      impl() {
      }

      
      fc::tcp_server                _tcp_serv; 
      fc::shared_ptr<node>          _node;
      fc::shared_ptr<name_service>  _ns; 
      fc::shared_ptr<chunk_service> _cs;

      fc::future<void>              _accept_complete;

      fc::vector<fc::connection::ptr> _connections;

      void accept_loop() {
        while( true ) {
          http_connection::ptr c( new http_connection(*this) );
          if( _tcp_serv.accept( c.socket ) ) {
            _connections.push_back( c );
            c->process();
          }
        }
      }
   };

   void http_connection::process() {
      if( !_complete.valid() ) {
        _complete = fc::async( [=](){process();} );
        return;
      }
      // parse the data coming from the socket here

   }

   

   httpd::httpd( const fc::shared_ptr<node>& n,
             const fc::shared_ptr<name_service>& ns, 
             const fc::shared_ptr<chunk_service>& cs 
           ) {
      my->_node = n;
      my->_ns   = ns;
      my->_cs   = cs;
   }

   httpd::~httpd() {

   }

   void httpd::listen( uint16_t port ) {
      my->_tcp_serv.listen(port);
      my->_accept_complete = fc::async([=](){this->my->accept_loop();}, "httpd::accept_loop" );
   }

  
} 
