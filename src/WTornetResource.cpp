#include "WTornetResource.hpp"
#include <Wt/Http/Request>
#include <Wt/Http/Response>
#include <fc/log.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <fc/unique_lock.hpp>
#include <fc/mutex.hpp>

#include <boost/lexical_cast.hpp>
#include <tornet/download_status.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/tornet_app.hpp>
#include <tornet/chunk_service.hpp>

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


 void WTornetResource::handleRequest( const Wt::Http::Request& request, Wt::Http::Response& response ) {
    try {
      slog( "request pathinfo %s", request.pathInfo().c_str() );
        std::stringstream ss( request.pathInfo() );
        std::string tid, sum, sd;
        std::getline( ss, tid,'/' );
        std::getline( ss, tid,'/' );
        std::getline( ss, sd,'/' );
      auto cont = request.continuation();
      tn::link ln( fc::sha1( tid.c_str() ), boost::lexical_cast<uint64_t>(sd) );

      auto tf = tn::tornet_app::instance()->get_chunk_service()->download_tornet( ln );

      slog( "length %lld  name %s", tf.size, tf.name.c_str() );
      response.addHeader( "Content-Length", boost::lexical_cast<std::string>(tf.size).c_str() );
      response.addHeader( "Content-Disposition", ("inline; " + tf.name).c_str() );
     
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
