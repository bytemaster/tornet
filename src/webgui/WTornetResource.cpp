#include "WTornetResource.hpp"
#include <Wt/Http/Request>
#include <Wt/Http/Response>
#include <fc/log.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <fc/unique_lock.hpp>
#include <fc/mutex.hpp>
#include <fc/raw.hpp>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <tornet/download_status.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/name_service.hpp>
#include <tornet/tornet_app.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/archive.hpp>

class WTornetResource::impl {
  public:
};

 WTornetResource::WTornetResource( Wt::WObject* p  )
 :WResource(p) {
    setDispositionType( Wt::WResource::Inline );
 }

 WTornetResource::~WTornetResource() {
   beingDeleted();
 }

 struct RequestState : public tn::download_delegate {
    typedef fc::shared_ptr<RequestState> Ptr;

      
    virtual void download_data( fc::vector<char>&& data ) {
      slog( "this: %p resource: %p", this, resource );
      {
        fc::unique_lock<fc::mutex> lock(mut);
        data_avail.push_back( fc::move(data) );
      }
      if( resource ) resource->haveMoreData();
    }
    virtual void download_status( double per, const fc::string& m  ){
      slog( "Status %f  %s", per, m.c_str() );
    }
    virtual void download_complete(){
      wlog( "Complete %p", this );
      state = complete;
      if( resource ) resource->haveMoreData();
    }
    virtual void download_header( const tn::tornet_file& f ) {
      slog( "%p", this );
      if( resource ) resource->haveMoreData();
    }

    RequestState() {
      state = waiting_for_tornet_file;
      wrote = 0;
      wlog( "%p", this );
    }

    enum States {
      waiting_for_tornet_file = 0,
      sending_body            = 2,
      complete                = 3,
      error                   = 4
    };
    std::list<fc::vector<char> >          pop_data() {
      fc::unique_lock<fc::mutex> lock(mut);
      auto tmp = fc::move(data_avail);
      data_avail = std::list<fc::vector<char> >();
      return tmp;
    }
    fc::mutex                             mut;
    WTornetResource*                      resource;
    int                                   state;
    uint64_t                              wrote;
    fc::string                            err_msg;
    tn::download_status::ptr              ds;
    std::list<fc::vector<char> >          data_avail;
    protected:
    ~RequestState() {
      wlog( "%p", this );
    }
 };



tn::tornet_file find_in_subdir( const tn::tornet_file& tf, const std::string& subdir ) {
   slog( "'%s'", subdir.c_str() );
   std::stringstream ss( subdir );
   std::string dir;
   std::getline( ss, dir,'/' );
   std::string rest;
   std::getline( ss, rest,'\n' );

   tn::archive ar;
   if( tf.mime == "tn::archive" ) {
     ar = fc::raw::unpack<tn::archive>(tf.inline_data);

     auto itr = ar.entries.begin();
     while( itr != ar.entries.end() ) {
       if( itr->name == dir.c_str() ) {
           auto stf = tn::tornet_app::instance()->get_chunk_service()->download_tornet( itr->ref );
           if( rest.size() ) return find_in_subdir( stf, rest );
           return stf;
       }
       ++itr;
     }
   }
   return tf;
}


 void WTornetResource::handleRequest( const Wt::Http::Request& request, Wt::Http::Response& response ) {
    try {
      

      elog( "request server %s pathinfo %s host %s", request.serverName().c_str(), request.pathInfo().c_str(), request.headerValue("Host").c_str() );
      /*
      std::stringstream ss( request.pathInfo() );
      std::getline( ss, tid,'/' );
      std::getline( ss, tid,'-' );
      std::getline( ss, sd,'/' );
      std::getline( ss, subdir,'\n' );
      */
      std::string subdir = request.headerValue( "tornet-path" ).substr(1); // skip '/'
      std::string tornet_site = request.headerValue( "tornet-site" );
      boost::filesystem::path p = tornet_site;



      tn::link ln;
      if( p.extension() == ".chunk" ) 
        ln = tn::link(p.stem().string().c_str());
      else {
        ln = tn::tornet_app::instance()->get_name_service()->get_link_for_name( tornet_site.c_str() );
      }
      wlog( "link %s", fc::string(ln).c_str() );

    //  if( fs::path( 
    //  ln = link( fc::sha1( tid.c_str() ), boost::lexical_cast<uint64_t>(sd) );

      auto tf = tn::tornet_app::instance()->get_chunk_service()->download_tornet( ln );
      if( tf.mime == "tn::archive" ) {
          
        if( !subdir.size() || subdir == "/" )  
            tf = find_in_subdir( tf, "index.html" );
        else
            tf = find_in_subdir( tf, subdir );
      } 
      
      if( tf.mime == "tn::archive" ) {
        auto ar = fc::raw::unpack<tn::archive>(tf.inline_data);
        auto itr = ar.entries.begin();
        response.out() << "<html><body>\n";
        response.out() << "<h1>"<<subdir<<"</h1><br/>\n";
        while( itr != ar.entries.end() ) {
            //response.out() << "<a href=\"/fetch/" << fc::string(itr->ref.id).c_str() << "-"<< itr->ref.seed << "\">";
            response.out() << "<a href=\""<<itr->name.c_str()<<"\">";
            response.out() << itr->name.c_str() << "</a><br/>\n";
          ++itr;
        }
        response.out() << "</body></html>\n";
        return;
      }

      slog( "length %lld  name %s", tf.size, tf.name.c_str() );
      //response.addHeader( "Content-Length", boost::lexical_cast<std::string>(tf.size).c_str() );
      //response.addHeader( "Content-Disposition", ("inline; " + tf.name).c_str() );

      if( tf.inline_data.size() ) {
        response.out().write( tf.inline_data.data(), tf.inline_data.size() );
        return;
      }
     
      auto cont = request.continuation();
      if( cont ) {
          auto down = boost::any_cast<RequestState::Ptr>(cont->data());
     
          switch( down->state ) {
             case RequestState::error:
                elog( "error %s", down->err_msg.c_str() );
                response.out() << std::endl << down->err_msg.c_str() << std::endl;
                down->ds.reset();
                return;
             case RequestState::waiting_for_tornet_file: {
                  /*
                auto tf = down->ds->get_tornet_file();
                if( tf != nullptr ) {
                    wlog( "length %lld  name %s", tf->size, tf->name.c_str() );
                    response.addHeader( "Content-Length", boost::lexical_cast<std::string>(tf->size) );
                    response.addHeader( "Content-Disposition", ("inline; " + tf->name).c_str() );
                }
                */
                down->state = RequestState::sending_body;
             }
             case RequestState::complete: 
             case RequestState::sending_body: {
                std::list<fc::vector<char> > avail;
                fc::swap( avail, down->data_avail );
     
                for( auto i = avail.begin();
                          i != avail.end(); ++i ) {
                  down->wrote += i->size();
                  //slog( "wrote %lld", down->wrote );
                  response.out().write( i->data(), i->size() );
                  if( down->wrote >= down->ds->get_tornet_file()->size ) {
                      down->resource = 0;
                      //down->ds.reset();
                      wlog("wrote everything we promised") ;
                    return;
                  }
                }
                if( down->state == RequestState::complete ) {
                  down->ds.reset();
                  wlog("done") ;
                  return;
                } else {
                    cont = response.createContinuation();
                    cont->setData( down );
                    wlog( "wait for more data" );
                    cont->waitForMoreData();
                    return;
                }
             }break;
             default: 
                elog( "Unknown state %d", down->state );
          }
      } else {
          RequestState::Ptr rs( new RequestState() );
          rs->state    = RequestState::waiting_for_tornet_file;
          rs->resource = this;
          rs->ds.reset( new tn::download_status( tn::tornet_app::instance()->get_chunk_service(), 
                                                 ln,
                                                 tn::download_delegate::ptr( rs.get(), true )  ) );
          cont = response.createContinuation();
          cont->setData( rs );
          wlog( "wait for more data" );
          cont->waitForMoreData();
          rs->ds->start();
      }
   } catch ( ... ) {
    elog("%s", fc::current_exception().diagnostic_information().c_str() );
    response.out() << fc::current_exception().diagnostic_information().c_str() <<std::endl;
   }
 }
