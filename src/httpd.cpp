#include <tornet/httpd.hpp>
#include <fc/tcp_socket.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/thread.hpp>
#include <fc/vector.hpp>
#include <fc/filesystem.hpp>
#include <fc/raw.hpp>

//#include <iostream>
#include <fc/stream.hpp>
#include <fc/blowfish.hpp>

#include <tornet/name_service.hpp>
#include <tornet/download_status.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/node.hpp>
#include <tornet/db/name.hpp>
#include <tornet/tornet_app.hpp>
#include <tornet/archive.hpp>

#include <boost/filesystem/path.hpp>

namespace tn {
   namespace fs = boost::filesystem;
   struct http_not_found {
      fs::path full_path;
      fs::path valid_path;
   };
   struct http_invalid_domain {
      fs::path domain;
   };

   struct http_header {
      fc::string key;
      fc::string val;
   };

   struct http_request {
      http_request():_length(0),_close(true){}
      fc::string              _method;
      fc::path                _domain;
      fc::path                _path;
      fc::vector<http_header> _headers;
      uint64_t                _length;
      bool                    _close;
      fc::vector<char>        _body;
   };

   struct http_reply {
      enum codes {
          OK                  = 200,
          NotFound            = 404,
          Found               = 302,
          InternalServerError = 500
      };
      http_reply():_status(200),_length(0){}
      int                     _status;
      fc::path                _location;
      uint64_t                _length;
      bool                    _close;
      fc::vector<http_header> _headers;
   };

   struct http_found {
      http_found( const fc::string& p ):_path(p){}
      fc::string                _path;
   };


   class http_connection : public fc::retainable {
      public:
      typedef fc::shared_ptr<http_connection> ptr;
      http_connection( httpd::impl& h )
      :_httpd(h){}

      ~http_connection() {
        wlog( "");
      }

      link resolve_path( const link& domain, const boost::filesystem::path& _path );
      void send_header( const http_reply& rep );
      http_request parse_request();
      void send_body( const fc::string& body );
      void send_body( const tornet_file& tf );
      void process();
      void handle_tornet_request(const http_request& req);
      void tornet_names( const http_request& req );

      void send_invalid_domain( const http_request&, const http_invalid_domain& d);
      void send_redirect_to_found( const http_request& ,const http_found& d);
      void send_404(const http_request& , const http_not_found& d);
      void send_server_error( const http_request& ,const fc::string& s );

      fc::future<void> _complete;
      httpd::impl&     _httpd;
      fc::tcp_socket   _sock;
   };
  
   class httpd::impl {
    public:
      impl(uint16_t port)
      :_tcp_serv(port) {
      }
      
      fc::tcp_server                     _tcp_serv; 
      fc::future<void>                   _accept_complete;
      fc::vector<http_connection::ptr>   _connections;

      void accept_loop() {
        while( true ) {
          http_connection::ptr c( new http_connection(*this) );
          if( _tcp_serv.accept( c->_sock ) ) {
            fc::async( [=](){ c->process(); } );
          }
        }
      }
   };

   int read_until( fc::tcp_socket& s, char* buffer, char* end, char c = '\n' ) {
      char* p = buffer;
      try {
      while( p < end && s.readsome(p,1) ) {
        if( *p == c ) {
          *p = '\0';
          return (p - buffer)-1;
        }
        ++p;
      }
      } catch ( ... ) {
        //elog( "%s", fc::except_str().c_str() );
      }
      return (p-buffer);
   }

   http_request http_connection::parse_request() {
      http_request req;
      fc::vector<char> tmp(1024*8);
      char* end = tmp.data() + tmp.size();
      int s = read_until( _sock, tmp.data(), tmp.data()+tmp.size(), ' ' );
      req._method = tmp.data();
      slog( "method '%s'", req._method.c_str() );

      s = read_until( _sock, tmp.data(), end, '/' ); // http:/
      s = read_until( _sock, tmp.data(), end, '/' ); // /
      s = read_until( _sock, tmp.data(), end, '/' ); // domain.chunk/
      req._domain = tmp.data();
      s = read_until( _sock, tmp.data(), tmp.data()+tmp.size(), ' ' );  // path/s/... 
      req._path   = tmp.data();
      s = read_until( _sock, tmp.data(), tmp.data()+tmp.size(), '\n' ); // HTTP/1.1\r

      while( (s = read_until( _sock, tmp.data(), tmp.data()+tmp.size(), '\n' )) > 1 ) {
        slog( "%s", tmp.data() );
        http_header h;
        char* end = tmp.data();
        while( *end != ':' )++end;
        h.key = fc::string(tmp.data(),end);
        ++end; // skip space
        char* skey = end;
        while( *end != '\r' ) ++end;
        h.val = fc::string(skey,end);
        req._headers.push_back(h);
      }
      slog( "... done with header..." );
      return req;
   }

   link resolve_domain( const http_request& req ) {
      if( req._domain.extension() == ".chunk" ) {
        return tn::link( req._domain.stem().string() );     
      } else {
        return tn::get_name_service()->get_link_for_name( req._domain.string() );
      }
   }

   tornet_file fetch_header( const link& ln ) {
      return get_chunk_service()->download_tornet( ln );
   }

   namespace fs = boost::filesystem;
   link find_entry( const tornet_file& tf, const fs::path& p ) {
     auto ar = fc::raw::unpack<tn::archive>(tf.inline_data);
     auto itr = ar.entries.begin();
     while( itr != ar.entries.end() ) {
       if( itr->name == p.string().c_str() )
          return itr->ref;
       ++itr;
     }
     throw http_not_found();
   }

   /**
    *  Assuming the request contains a path, recursively fetch the contents of
    *  the domain, search for the folder/file and return the result.
    *
    *  If no file is found, a http_not_found exception will be thrown that contains
    *  the 'path found' and the first file not found.  It will also contain the
    *  archive for the found path.
    */
   link http_connection::resolve_path( const link& domain, const boost::filesystem::path& _path ) {
      slog( "%s", _path.string().c_str() );
      link result = domain;

      fs::path valid_path;
      try {
          auto itr = _path.begin();
          while( itr != _path.end() ) {
              auto tf = fetch_header( result );
              slog( "find_entry %s", fs::path(*itr).string().c_str() );
              if( *itr == fs::path(".") ) 
                return result;
              result  = find_entry( tf, *itr );
              valid_path /= *itr;
              ++itr;
          }
      } catch ( http_not_found& d ) {
        d.valid_path = valid_path;
        d.full_path  = _path;
        throw;
      }
      return result;
   }


   void http_connection::send_header( const http_reply& rep ) {
      fc::stringstream ss;

      switch( rep._status ) {
        case http_reply::OK:
          ss<<"HTTP/1.1 200 OK\r\n";
          break;
        case http_reply::NotFound:
          ss<<"HTTP/1.1 404 Not Found\r\n";
          break;
        case http_reply::Found:
          ss<<"HTTP/1.1 302 Found\r\n";
          break;
        case http_reply::InternalServerError:
          ss<<"HTTP/1.1 500 Internal Server Error\r\n";
          break;
        default:
          ss<<"HTTP/1.1 200 OK\r\n";
          break;
      }
      // TODO: send other headers in the request
      ss<<"Location: "<<rep._location.string()<<"\r\n";
      ss<<"Content-Length: "<<rep._length<<"\r\n";
      ss<<"\r\n";

      auto header = ss.str();
      _sock.write( header.c_str(), header.size() );
      fc::cerr<<header.c_str();
      fc::cerr.flush();
   }

   void http_connection::send_body( const fc::string& body ) {
      _sock.write( body.c_str(), body.size() );
      fc::cerr<<body.c_str();
      fc::cerr.flush();
   }

   void http_connection::send_body( const tornet_file& tf ) {
      if( tf.inline_data.size() ) {
        _sock.write( tf.inline_data.data(), tf.inline_data.size() );
        return;
      } else {
          auto cs = tn::tornet_app::instance()->get_chunk_service();

          fc::blowfish bf;
          bf.start( (unsigned char*)tf.checksum.data(), sizeof(tf.checksum) );
          fc::sha1::encoder sha1_enc;

        // download and send chunks here
          auto itr = tf.chunks.begin();
          while( itr != tf.chunks.end() ) {
             // TODO: start parallel download here, I can be downloading one
             // chunk while I am writing the other... 
             
             auto cdata = cs->download_chunk( itr->id ); 
             if( itr->size > cdata.size() ) {
                //FC_THROW_MSG( "Chunk size '%s' smaller than expected (%s)", fc::to_string(cdata.size), fc::to_string(itr->size) );
                FC_THROW_MSG( "Chunk size than expected" );
             }
             derandomize( itr->seed, cdata );

             bf.reset_chain();
             bf.decrypt( (unsigned char*)cdata.data(), cdata.size(), fc::blowfish::CBC );

             _sock.write( cdata.data(), itr->size );
             ++itr;
          }
      }
   }

   bool is_archive( const tornet_file& f ) {
    return f.mime == "tn::archive";
   }

  fc::string get_directory_listing( const tornet_file& tf, const fc::path& domain, const fc::path& dir )  {
     auto ar = fc::raw::unpack<tn::archive>(tf.inline_data);
     
     fc::stringstream ss;
     ss << "<html>\n";
     ss << "<head>\n";
     ss << "<title>//"<<domain.string() <<"/"<<dir.string()<<"</title>\n";
     ss << "</head>\n";
     ss << "<body>\n";
     ss << "<h1>//"<<domain.string() <<"/"<<dir.string()<<"</h1>\n";
       ss << "<table>\n";
         auto itr = ar.entries.begin();
         while( itr != ar.entries.end() ) {
           ss<<"<tr>\n";
              ss<<"<td><a href=\""<<itr->name<<"\">"<<itr->name<<"</a></td>\n";
           ss<<"</tr>\n";
           ++itr;
         }
       ss << "</table>\n";
     ss << "</body>\n";
     ss << "</html>\n";
     
     return ss.str();
  }

   void http_connection::process() {
       auto req          = parse_request();
       try {
          if( req._domain == "tornet" ) {
            handle_tornet_request( req );
            return;
          }
          auto domain_link  = resolve_domain( req );
          auto path_link    = resolve_path( domain_link, req._path );
          auto cheader      = fetch_header( path_link );
      
          http_reply rep;
          rep._status      = http_reply::OK;
          rep._location    = req._path;
          rep._close       = req._close;
      
          if( is_archive( cheader ) ) {
             auto content = get_directory_listing( cheader, req._domain, req._path );      
             rep._length = content.size();            
             rep._status = http_reply::OK;

             fc::string s = rep._location.string();
             if( s.size() && s[s.size()-1] != '/' ) { throw http_found( "http://"+(req._domain/s).string()+"/" ); }
             
             send_header( rep );
             send_body( content );
          } else {
             rep._length = cheader.size;
             rep._status = http_reply::OK;
             send_header( rep );
             send_body( cheader );
          }

       } catch ( const http_invalid_domain& d ) { 
           send_invalid_domain(req,d); 
       } catch ( const http_found& d ) { 
           send_redirect_to_found(req,d); 
       } catch ( const http_not_found& d ) { 
           send_404( req, d );
       } catch (...) {
           send_server_error( req, fc::except_str() );
       }
  }     
  void http_connection::handle_tornet_request(const http_request& req) {
    if( req._path == "names" ) {
      tornet_names(req); 

    } else {

      http_reply rep;
      rep._status = http_reply::OK;
      rep._location    = req._path;
      
      fc::stringstream ss;
      ss<<"<a href=\"http://tornet/names\">names</a><br/>";

      auto body = ss.str();
      rep._length = body.size();
      send_header( rep );
      send_body(body);

    }
  }

  void http_connection::tornet_names( const http_request& req ) {
      http_reply rep;
      rep._status = http_reply::OK;
      rep._location    = req._path;
      
      fc::stringstream ss;

      auto ndb = get_name_service()->get_name_db();
      auto count = ndb->record_count();
      for( int i = 1; i <= count; ++i ) {
        fc::string name;
        db::name::record rec;
        ndb->fetch( i, name, rec );
        ss<<"<a href=\"http://"<<name<<"\">"<<name<<"</a><br/>\n";
      }
      auto body = ss.str();
      rep._length = body.size();
      send_header( rep );
      send_body(body);
  }



  void http_connection::send_invalid_domain( const http_request& req, const http_invalid_domain& d) {
     fc::stringstream ss;
     ss << "<html>\n";
     ss << "<head>\n";
     ss << "<title>Unknown Domain "<<req._domain.string().c_str()<<"</title>\n";
     ss << "</head>\n";
     ss << "<body>\n";
     ss << "<h1>Domain Not Found</h1>\n";
     ss << req._domain.string().c_str();
     ss << "</body>\n";
     ss << "</html>\n";

     auto cont = ss.str();

     http_reply rep;
     rep._status      = http_reply::NotFound;
     rep._location    = req._path;
     rep._length      = cont.size();

     send_header( rep );
     send_body( cont );
  }
  void http_connection::send_redirect_to_found( const http_request& req, const http_found& d) {
     http_reply rep;
     rep._status      = http_reply::Found;
     rep._location    = d._path;
     rep._length      = 0;

     send_header( rep );
  }
  void http_connection::send_404( const http_request& req, const http_not_found& d) {
     fc::stringstream ss;
     ss << "<html>\n";
     ss << "<head>\n";
     ss << "<title>404 - Not Found</title>\n";
     ss << "</head>\n";
     ss << "<body>\n";
     ss << "<h1>404 Not Found</h1>\n";
     ss << req._path.string().c_str();
     ss << "</body>\n";
     ss << "</html>\n";

     auto cont = ss.str();

     http_reply rep;
     rep._status      = http_reply::NotFound;
     rep._location    = req._path;
     rep._length      = cont.size();

     send_header( rep );
     send_body( cont );

  }
  void http_connection::send_server_error( const http_request& req, const fc::string& s ) {
     fc::stringstream ss;
     ss << "<html>\n";
     ss << "<head>\n";
     ss << "<title>500 Internal Server Error</title>\n";
     ss << "</head>\n";
     ss << "<body>\n";
     ss << "<h1>500 Internal Server Error</h1>\n";
     ss << s;
     ss << "</body>\n";
     ss << "</html>\n";

     auto cont = ss.str();

     http_reply rep;
     rep._status      = http_reply::InternalServerError;
     rep._location    = req._path;
     rep._length      = cont.size();

     send_header( rep );
     send_body( cont );
  }


   httpd::httpd(uint16_t port ):my(port){
      //my->_node = n;
      //my->_ns   = ns;
      //my->_cs   = cs;
      my->_accept_complete = fc::async([=](){this->my->accept_loop();}, "httpd::accept_loop" );
   }

   httpd::~httpd() {

   }
/*
   void httpd::listen( uint16_t port ) {
      wlog( "httpd listening on port %d", port );
      my->_tcp_serv.listen(port);
   }
   */

  
} 
