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
#include <fc/time.hpp>
#include <fc/thread.hpp>
#include <fc/fstream.hpp>
#include <tornet/kad.hpp>
#include <tornet/chunk_service.hpp>


#include <fc/json.hpp>
#include <fc/pke.hpp>

#include <tornet/node.hpp>

struct published_link {
  fc::string link;
  fc::string domain; 
  fc::string last_update;
};

FC_REFLECT( published_link, (link)(domain)(last_update) )


/**
 *  To publish a name, it must provide a nonce such that its hash is
 *  sufficiently (TBD) low and the expiration date must be within
 *  the next 6 months.  A name must be updated from time to time or
 *  it expires.
 *
 *  When it comes time to resolve a name, the client will perform a
 *  kad lookup on the
 *  
 *
 */
struct name_entry {
  bool     is_valid()const;                     // validates the hash + signature
  fc::sha1 calc_hash( uint64_t nonce )const;    // given a nonce, calculate the hash
  void     sign();                              // calculates the hash + signature

  fc::string                        name;         // may only contain [a-z0-9_-], 30 bytes
  fc::public_key_t                  public_key;   // base58 encoded
  fc::string                        link;         // hex encoded cafs link
  fc::time_point                    expires;      // date/time upon which this expires
  uint64_t                          nonce;        // used to determine rank
  fc::sha1                          hash;         // sha1::hash( nonce + name + key + link + expires  ) -> hex
  fc::signature_t                   signature;    // private_key.sign(hash)
  fc::optional<fc::private_key_t>   private_key;  // private_key base
};

FC_REFLECT( name_entry, (name)(public_key)(link)(expires)(nonce)(hash)(signature)(private_key) )


class tproxy {
  public:
    struct config {
      fc::string             data_dir;
      uint16_t               http_proxy_port;
      uint16_t               tornet_port;
      fc::vector<fc::string> bootstrap_hosts;
    };

    config _cfg;

    void configure( const config& c ) {
      _cfg = c;
      links_dir = fc::path(c.data_dir) / "links";
      links_dir = fc::path(c.data_dir) / "names";
      fc::create_directories(links_dir);

      cache.open( c.data_dir );
      httpd.listen( c.http_proxy_port );


      tnode.reset( new tn::node() );
      tnode->init( fc::path(c.data_dir) / "nodes", c.tornet_port );

      httpd.on_request( 
        [this]( const fc::http::request& r, const fc::http::server::response& s ) {
          on_http_request( r, s );
        }
      );

      chunk_serv.reset( new tn::chunk_service( fc::path(c.data_dir) / "chunk_service", cache, tnode ) );
      fc::async( [=](){ bootstrap(); } ); // TODO: wait for this to finish before destructing..
    }
    void bootstrap() {
      for( uint32_t i = 0; i < _cfg.bootstrap_hosts.size(); ++i ) {
        try {
            wlog( "Attempting to connect to %s", _cfg.bootstrap_hosts[i].c_str() );
            fc::sha1 id = tnode->connect_to( fc::ip::endpoint::from_string( _cfg.bootstrap_hosts[i]) );
            wlog( "Connected to %s", fc::string(id).c_str() );
        } catch ( ... ) {
            wlog( "Unable to connect to node %s, %s", 
                  _cfg.bootstrap_hosts[i].c_str(), fc::current_exception().diagnostic_information().c_str() );
        }
      }
      
      fc::sha1 target = tnode->get_id();
      target.data()[19] += 1;
      slog( "Bootstrap, searching for self...", fc::string(tnode->get_id()).c_str(), fc::string(target).c_str()  );
      tn::kad_search::ptr ks( new tn::kad_search( tnode, target ) );
      ks->start();
      ks->wait();
      slog( "Bootstrap complete" );
   }

    void on_http_request( const fc::http::request& r, const fc::http::server::response& s ) {
       fc::stringstream ss;
       try {
          if( r.domain.size() >= 38 ) { // cafs::link...
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

          {  // save published link...
             published_link pl;
             pl.link   = l;
             pl.domain = dom;
             pl.last_update = fc::time_point::now();
             
             fc::string jpl = fc::json::to_string(pl);
             fc::string jpl_filename(fc::sha1::hash( jpl.c_str(), jpl.size() ));
             
             fc::path jpl_path = links_dir / jpl_filename;
             fc::ofstream jplf( jpl_path.generic_string().c_str() );
             jplf.write( jpl.c_str(), jpl.size() );
          }


          ss << "<a href=\"http://"<<fc::string( l ) <<"\">";
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
          ss<<"<pre>\n";
          // list all of the known links that are being published.
          // links are stored as json objects in files named after the link...
          fc::vector<published_link> links;
          while( d != e ) {
            fc::string fn = (*d).filename().generic_string();
            published_link pl = fc::json::from_file<published_link>( (*d).generic_string() );
            if( fn.size() > 2 && fn[0] != '.' ) {
              ss << "<a href=\"http://"<<pl.link<<"/\">"<<pl.domain<<"</a><br/>\n"; 
            }
            ++d;
          }
          ss<<"</pre>\n";

          fc::string body = ss.str();
          s.set_status( fc::http::reply::OK );
          s.set_length( body.size() );
          s.write( body.c_str(), body.size() );
       } else {
          FC_THROW_REPORT( "Method ${method} is not supported", fc::value().set("method",r.method) );
       }
    }

    fc::shared_ptr<tn::node>       tnode;
    fc::http::server               httpd;
    cafs                           cache;
    fc::path                       links_dir;
    fc::path                       names_dir;
    tn::chunk_service::ptr         chunk_serv;
};



FC_REFLECT( tproxy::config, (data_dir)(http_proxy_port)(tornet_port)(bootstrap_hosts) )
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
