#include <tornet/httpd.hpp>
#include <fc/tcp_socket.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/thread.hpp>
#include <fc/vector.hpp>

#include <iostream>

namespace tn {

   class http_connection : public fc::retainable {
      public:
      typedef fc::shared_ptr<http_connection> ptr;
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
      /*
      fc::shared_ptr<node>          _node;
      fc::shared_ptr<name_service>  _ns; 
      fc::shared_ptr<chunk_service> _cs;
      */

      fc::future<void>              _accept_complete;

      fc::vector<http_connection::ptr> _connections;

      void accept_loop() {
        slog( "accept loop" );
        while( true ) {
          http_connection::ptr c( new http_connection(*this) );
          if( _tcp_serv.accept( c->_sock ) ) {
            elog( "\n\n\aaccept returned!!" );
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
      fc::vector<char> tmp(1024);
      while( true ) {
        wlog( "................" );
         auto r = _sock.readsome(tmp.data(),tmp.size() );
         if( r > 0 && r < tmp.size() ) std::cerr.write( tmp.data(), r );
         std::cerr.flush();
      }
   }

   

   httpd::httpd( ){
      //my->_node = n;
      //my->_ns   = ns;
      //my->_cs   = cs;
   }

   httpd::~httpd() {

   }

   void httpd::listen( uint16_t port ) {
      my->_tcp_serv.listen(port);
      my->_accept_complete = fc::async([=](){this->my->accept_loop();}, "httpd::accept_loop" );
   }

  
} 
