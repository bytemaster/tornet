#include <tornet/download_status.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/chunk_search.hpp>
#include <tornet/chunk_service_client.hpp>
#include <fc/thread.hpp>
#include <fc/stream.hpp>
#include <fc/sha1.hpp>
#include <fc/string.hpp>
#include <fc/optional.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/blowfish.hpp>

#include <cafs.hpp>

namespace tn {

  class download_status::impl {
    public:
      impl( download_status& s  )
      :_self(s){}

      download_status&           _self;
      fc::string                 _status;
      double                     _per_comp;
      chunk_service::ptr         _cs;
      cafs::link                 _link;
      fc::optional<tornet_file>  _tf;
      fc::string                 _fail_str;

      download_delegate::ptr     _delegate;

      fc::future<void>   _download_fiber_complete;

      /**
       *  Downloads the chunk 'id' and stores it in local cache if
       *  it does not already exist there.
       *
       *  For simplicity sake, we will look up the chunk synchronously and
       *  in serial.  This should increase 'latency', but will allow me
       *  to focus on higher-level features. 
       *
       *  Ultimately, the lookup / download should occur async and in parallel to 
       *  reduce latency and increase performance.
       *
       *  @post chunk 'id' is stored locally in either the local_db or cache_db (or both)
       */
      #if 0
      bool download_chunk( const fc::sha1& id ) {
        slog( "%s", fc::string(id).c_str() );
        tn::db::chunk::meta met;
        try {
          if( _cs->get_local_db()->fetch_meta(id, met, false ) && met.size ) return true;
        } catch ( ... ) {}
        /*
        try {
        if( _cs->get_cache_db()->fetch_meta(id, met, false ) && met.size ) return true;
        } catch ( ... ) {}
        */
        
        tn::chunk_search::ptr csearch( new tn::chunk_search(_cs->get_node(), id, 5, 1, true ) );  
        csearch->start();
        csearch->wait();
        
        auto hn = csearch->hosting_nodes().begin();
        while( hn != csearch->hosting_nodes().end() ) {
           auto csc = _cs->get_node()->get_client<chunk_service_client>(hn->second);

           try {
               // give the node 5 minutes to send 1 MB
               fetch_response fr = csc->fetch( id, -1 ).wait( fc::microseconds( 1000*1000*300 ) );
               slog( "Response size %d", fr.data.size() );

               auto fhash = fc::sha1::hash( fr.data.data(), fr.data.size() );
               if( fhash == id ) {
                  _cs->get_local_db()->store_chunk(id,fr.data);
                  return true;
               } else {
                   wlog( "Node failed to return expected chunk" );
                   wlog( "Received %s of size %d  expected  %s",
                          fc::string( fhash ).c_str(), fr.data.size(),
                          fc::string( id ).c_str() );
               }
           } catch ( ... ) {
              wlog( "Exception thrown while attempting to fetch %s from %s", fc::string(id).c_str(), fc::string(hn->second).c_str() );
              wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
           }
          ++hn;
        }
        return false;
      }
      #endif
      
      void download_fiber() {
        try {
          _tf = _cs->download_tornet( _link );

          fc::blowfish bf;
          bf.start( (unsigned char*)_tf->checksum.data(), sizeof(_tf->checksum) );
          fc::sha1::encoder sha1_enc;


          size_t downloaded = 0;
          // download every chunk that is part of the tornet file
          auto itr = _tf->chunks.begin();
          while( itr != _tf->chunks.end() ) {
            _status   = "Downloading file chunk " + fc::string(itr->id);
            _delegate->download_status( _per_comp, _status );
            
            try {
              auto cdata = _cs->download_chunk( itr->id ); 
              downloaded += itr->size;
              _per_comp  = double(downloaded) / _tf->size;
              derandomize( itr->seed, cdata );

              bf.reset_chain();
              bf.decrypt( (unsigned char*)cdata.data(), cdata.size(), fc::blowfish::CBC );

              sha1_enc.write( cdata.data(), itr->size );

              // the decoded chunk size may be different than the DB size because 
              // blowfish requires 8 byte blocks.
              cdata.resize( itr->size ); 
              wlog( "...... sock write %d", cdata.size() );
              _delegate->download_data( fc::move(cdata) );
            } catch ( ... ) {
               _delegate->download_error( -1, "Unable to download chunk "+ fc::string(itr->id) + ": " + fc::current_exception().diagnostic_information() );
            }

            ++itr;
          }
        } catch( ... ) {
           wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
           _delegate->download_error( -1, fc::current_exception().diagnostic_information() );
        }
        wlog( "complete" );
        _delegate->download_complete();
      }
  };

  download_status::download_status( const fc::shared_ptr<chunk_service>& cs, const cafs::link& ln, const download_delegate::ptr&  del )
  :my(*this) {
    my->_cs = cs;
    my->_link = ln;
    my->_per_comp = 0;
    my->_status   = "idle";
    my->_delegate = del;
  }

  download_status::~download_status() {
    wlog( "%p", this );
    if( my->_download_fiber_complete.valid() ) {
      my->_download_fiber_complete.cancel();
      my->_download_fiber_complete.wait();
    }
  }

  void  download_status::pause() {
  }

  void  download_status::start() {
    my->_download_fiber_complete = 
      my->_cs->get_node()->get_thread().async( [this](){ my->download_fiber(); }, "download_fiber" );
  }
  
  void  download_status::cancel() {
    my->_download_fiber_complete.cancel();
  }
  
 const cafs::link&  download_status::get_link()const { return my->_link; }

 const fc::string& download_status::fail_string()const { return my->_fail_str; }


  /**
   *  If the tornet description file has been fetched, this will return it.
   *
   *  @return nullptr if the file is not known, otherwise a pointer to the file.
   */
  const tornet_file* download_status::get_tornet_file()const {
    if( !!my->_tf ) return &*my->_tf;
    return nullptr;
  }

  double       download_status::percent_complete()const {
    return my->_per_comp;
  }

}
