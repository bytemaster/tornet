#include <fc/string.hpp>
#include <fc/error_report.hpp>
#include <fc/filesystem.hpp>

#include <fc/reflect.hpp>
#include <cafs.hpp>
#include <fc/iostream.hpp>
#include <fc/sha1.hpp>
#include <fc/url.hpp>
#include <fc/http/server.hpp>
#include <fc/sstream.hpp>


#include <fc/json.hpp>

struct published_link {
  fc::string link;
  fc::string domain; 
  fc::string last_update;
};

FC_REFLECT( published_link, (link)(domain)(last_update) )


class tproxy {
  public:
    struct config {
      fc::string data_dir;
      uint16_t   http_proxy_port;
    };

    void configure( const config& c ) {
      links_dir = fc::path(c.data_dir) / "links";
      fc::create_directories(links_dir);

      cache.open( c.data_dir );
      httpd.listen( c.http_proxy_port );

      httpd.on_request( 
        [this]( const fc::http::request& r, const fc::http::server::response& s ) {
          on_http_request( r, s );
        }
      );
    }

    void on_http_request( const fc::http::request& r, const fc::http::server::response& s ) {
       fc::stringstream ss;
       try {
          if( r.domain.size() >= 38 ) { // cafs::link...
              wlog( "domain... handle link...%s" );
              cafs::link l( r.domain );
              fc::url u(r.path);
              fc::string p;
              if( u.path )
                  p = *u.path;
              else p = r.path;
              return handle_link( l, p, s );
          }
          else if( r.domain == "tornet" ) {
             return handle_tornet_request( r, s );
          } else {  // requires dns lookup
             ss << "Hello World!\n";
             ss << "Domain: "<<r.domain<<"\n";
             ss << "Path: "<<r.path<<"\n";
             ss << "Body: "<<fc::string(r.body.begin(), r.body.end())<<"\n";
             fc::string body = ss.str();
             s.set_status( fc::http::reply::OK );
             s.set_length( body.size() );
             s.write( body.c_str(), body.size() );
          }
          return;
       } catch ( const fc::error_report& e ) {
         s.add_header( "Content-Type", "text/html" );
         ss <<"<pre>\n";
         ss << e.to_detail_string() <<"\n"; 
         ss <<"</pre>\n";
         elog( "\n%s", e.to_detail_string().c_str() );
       } catch ( const std::exception& e ) {
         s.add_header( "Content-Type", "text/html" );
         ss << e.what() <<"\n"; 
       }
       fc::string body = ss.str();

       s.set_status( fc::http::reply::InternalServerError );
       s.add_header( "Host", r.domain ); 
       s.set_length( body.size() );
       s.write( body.c_str(), body.size() );

    }

    void handle_tornet_request( const fc::http::request& r, const fc::http::server::response& s ) {
       try {
         fc::url u(r.path);
         fc::string p;
         if( u.path )
             p = *u.path;
         else p = r.path;

         if( p == "publish" ) {
           return handle_tornet_publish( r, s );
         }
         FC_THROW_REPORT( "Invalid tornet path ${path}", fc::value().set( "path", p ) ); 
      } catch ( fc::error_report& e ) {
          throw FC_REPORT_PUSH( e, "Unable to handle tornet request" );
      }
    }
    void list_directory( const fc::path& p, const cafs::directory& d, const fc::http::server::response& s ) {
       fc::stringstream ss;
       ss<<"<html><head><title>"<< p.generic_string() <<"</title></head>\n";
       ss<<"<body>\n";
       ss<<"<h2>"<<p.generic_string()<<"</h2><hr/>\n";
       ss<<"<ul>\n";
       for( auto itr = d.entries.begin(); itr != d.entries.end(); ++itr ) {
          ss<<"<li><a href=\""<<(p/itr->name).generic_string()<<"\">"<<itr->name;
          ss<<"</a>";
          if( itr->ref.type == cafs::directory_type ) ss <<"/"; 
          else {
            if( itr->ref.size < 1024 )
              ss<<"  ["<<itr->ref.size<<" bytes]";
            else
              ss<<"  ["<<(itr->ref.size/1024)<<" kb]";
          }
          ss<<"</li>\n";
          // look for path root...        
       }
       ss<<"<ul>\n";
       ss<<"</body>\n";
       ss<<"</html>\n";
       fc::string dl = ss.str();
       s.set_status( fc::http::reply::OK );
       s.set_length( dl.size() );
       s.write( dl.c_str(), dl.size() );
    }
    void handle_file_ref( const cafs::file_ref& fr, const fc::path& p, 
                          const fc::string& subpath, const fc::http::server::response& s ) {
      if( fr.type != cafs::directory_type  && subpath.size() ) {
        FC_THROW_REPORT( "${path} is not a directory", fc::value().set("path", p.generic_string() ).set("subpath", subpath ) );
      }
      try {
        fc::optional<cafs::resource> ors = cache.get_resource(fr);
        if( !ors ) {  FC_THROW_REPORT( "Unable to find resource", fc::value().set("file_ref", fr) ); }
          
        cafs::resource& rs = *ors;
        if( rs.get_file_data() != nullptr ) {
            s.set_status( fc::http::reply::OK );
            s.set_length( rs.get_file_size() );
            s.write( rs.get_file_data(), rs.get_file_size() );
            return;
        }
        auto dir = rs.get_directory();
        if( dir ) {
          cafs::directory& d = *dir;
          if( subpath == fc::path("/") || subpath == fc::path()) {
              list_directory( p, d, s );
              return;
          } else {
             fc::string head;
             int i = 0;
             for( ; i < subpath.size(); ++i ) {
               if( subpath[i] == '/' && head.size() ) break;
               if( subpath[i] != '/' ) head += subpath[i];
             }
             fc::string tail = subpath.substr(i);
             for( auto itr = d.entries.begin(); itr != d.entries.end(); ++itr ) {
                if( itr->name == head ) {
                  return handle_file_ref( itr->ref, p / head , tail, s );
                }
             }
             FC_THROW_REPORT( "File ${file} not found in ${dir}", 
                fc::value().set("file", head )
                           .set("dir", p.generic_string())
                           .set("subdir", subpath) );
          }
        }
      } catch ( fc::error_report& e ) {
             throw FC_REPORT_PUSH( e, "Error loading file reference. ${path} / ${subdir}", 
                fc::value().set("path", p.generic_string() )
                           .set("subdir", subpath)
                           .set("ref", fr) );
      }
    }

    void handle_link( const cafs::link& l, const fc::path& p, const fc::http::server::response& s ) {
       try {
         fc::optional<cafs::resource> ors = cache.get_resource(l);
         if( !ors ) {  FC_THROW_REPORT( "Unable to find resource" ); }
         cafs::resource& rs = *ors;
         if( rs.get_file_data() != nullptr ) {
             s.set_status( fc::http::reply::OK );
             s.set_length( rs.get_file_size() );
             s.write( rs.get_file_data(), rs.get_file_size() );
             return;
         }
         auto dir = rs.get_directory();
         if( dir ) {
           cafs::directory& d = *dir;
           if( p == fc::path("/") || p == fc::path()) {
              list_directory( fc::path("/"), d, s );
              return;
           } else {
               fc::string gpath = p.generic_string();     
               auto pos = gpath.find( '/' );
               fc::string head = gpath.substr( 0, pos < 0 ? gpath.size() : pos );
               fc::string tail = gpath.substr(head.size());
               for( auto itr = d.entries.begin(); itr != d.entries.end(); ++itr ) {
                  if( itr->name == head ) {
                    return handle_file_ref( itr->ref, fc::path("/")/head, tail, s );
                  }
               }
               FC_THROW_REPORT( "File ${path} not found", fc::value().set("path", head ) );
           }
         }
         FC_THROW_REPORT( "Not implemented" );
       } catch ( fc::error_report& e ) {
          throw FC_REPORT_PUSH( e, "Unable to handle link ${link} / ${path}", fc::value().set("link", fc::string(l) ).set("path", p.generic_string()) ); 
       } catch ( ... ) {
          FC_THROW_REPORT( "Unhandled Exception", fc::value( fc::except_str() ) );
       }
    }

    /**
     *  There exists a set of 'published' files/directories that can be accessed given a cafs link (hash+seed).
     *
     *  
     *  
     *
     */
    void handle_tornet_publish( const fc::http::request& r, const fc::http::server::response& s ) {
       if( r.method == "POST" ) {
          // add a new link to be published... because this refers to a local file it could be a 
          // security risk to allow machines other than local host to publish data.

          if( r.get_header( "Content-Type" ) != "application/x-www-form-urlencoded" ) {
            FC_THROW_REPORT( "Content-Type ${content-type} is not supported", fc::value().set("content-type",r.get_header("Content-Type")) );
          }
          auto b = fc::string(r.body.data(), r.body.size());
          auto h = fc::http::parse_urlencoded_params( fc::string(r.body.data(), r.body.size()) );
          fc::stringstream ss;
          fc::string fpath;
          fc::string dom;
          for( auto i = h.begin(); i != h.end(); ++i ) {
            if( i->key == "path" ) fpath = i->val;
            else if( i->key == "domain" ) dom = i->val;
          }
          if( !fc::exists( fpath ) ) {
            FC_THROW_REPORT( "Unable to publish path ${path}, file or directory does not exist",
                              fc::value().set( "path", fpath ) );
          }
          auto l = cache.import( fpath );
          ss << "<a href=\"http://tornet/"<<fc::string( l ) <<"\">";
          ss << fc::string( l ) <<"</a>\n";

          fc::string body = ss.str();
          s.set_status( fc::http::reply::OK );
          s.set_length( body.size() );
          s.write( body.c_str(), body.size() );
       } else if( r.method == "GET" ) {
          fc::directory_iterator d(links_dir), e;
          fc::stringstream ss;
          ss<<"<h1>Published Links</h1><br/>\n";
          ss<<"<form method=\"post\">\n";
          ss<<"Path: <input type=\"text\" name=\"path\"/><br/>\n";
          ss<<"Domain: <input type=\"text\" name=\"domain\"/><br/>\n";
          ss<<"<input type=\"submit\" value=\"Publish\"><br/>\n";
          ss<<"</form><p/>\n";
          // list all of the known links that are being published.
          // links are stored as json objects in files named after the link...
          fc::vector<published_link> links;
          while( d != e ) {
            fc::string fn = (*d).filename().generic_string();
            if( fn.size() > 2 && fn[0] != '.' ) {
              ss << "<a href=\"/"<<fn<<"\">"<<fn<<"</a><br/>\n"; 
            }
            ++d;
          }

          fc::string body = ss.str();
          s.set_status( fc::http::reply::OK );
          s.set_length( body.size() );
          s.write( body.c_str(), body.size() );
       } else {
          FC_THROW_REPORT( "Method ${method} is not supported", fc::value().set("method",r.method) );
       }
    }

    fc::http::server httpd;
    cafs             cache;
    fc::path         links_dir;
};



FC_REFLECT( tproxy::config, (data_dir)(http_proxy_port) )
int main( int argc, char** argv ) {
  if( argc < 2 ) {
    fc::cout<<"Usage "<<argv[0]<<" CONFIG\n";
    return -1;
  }

  try {
    auto cfg = fc::json::from_file<tproxy::config>( argv[1] );
    slog( "%s", fc::json::to_string(cfg).c_str() );

    tproxy p;
    p.configure( cfg );

/*
    cafs fs;
    fs.open( cfg.data_dir );

    fc::http::server httpd( cfg.http_proxy_port );
    httpd.on_request( on_http_request );
*/

    char c;
    fc::cin>>c;
     
  } catch ( ... ) {
    fc::cerr<<fc::except_str()<<"\n";

  }
  fc::cout<<"Goodbye World\n";
}
