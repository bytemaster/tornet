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

namespace tn {

  class download_status::impl {
    public:
      impl( download_status& s, fc::ostream& o )
      :_self(s),_out(o){}

      download_status&           _self;
      fc::ostream&               _out;    
      fc::string                 _status;
      double                     _per_comp;
      chunk_service::ptr         _cs;
      fc::sha1                   _tid;
      fc::sha1                   _checksum;
      uint64_t                   _seed;
      fc::optional<tornet_file>  _tf;


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
      
      void download_fiber() {
        try {

          // download the tornet chunk
          if( download_chunk( _tid ) ) {
              _status   = "Downloading file chunks";
              uint64_t downloaded = 0;
              // decode the tornet chunk
              _tf = _cs->fetch_tornet( _tid, _checksum, _seed );

              // download every chunk that is part of the tornet file
              auto itr = _tf->chunks.begin();
              while( itr != _tf->chunks.end() ) {
                _status   = "Downloading file chunk " + fc::string(itr->id);
                _self.progress(_per_comp,_status);
                if( !download_chunk( itr->id ) )  {
                   elog( "Unable to download chunk %s", fc::string(itr->id).c_str() );
                } else {
                   downloaded += itr->size;
                   _per_comp  = double(downloaded) / _tf->size;
                }
                ++itr;
              }
              // if data is not imbedded within the tornet file

              // fetch each cunk from the database, decrypt it, and write it to the output stream
              
              fc::blowfish bf;
              fc::string fhstr = _checksum;
              bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );
              

              fc::sha1::encoder sha1_enc;

              itr = _tf->chunks.begin();
              while( itr != _tf->chunks.end() ) {
                auto cdata = _cs->fetch_chunk( itr->id );
                derandomize( itr->seed, cdata );
                bf.decrypt( (unsigned char*)cdata.data(), cdata.size(), fc::blowfish::CBC );
                // the decoded chunk size may be different than the DB size because 
                // blowfish requires 8 byte blocks.
                _out.write( cdata.data(), itr->size );
                sha1_enc.write( cdata.data(), itr->size );
                ++itr;
              }

              if( sha1_enc.result() != _checksum ) {
                elog( "Checksum mismatch" );
              }
          } else {
              _status   = "Unable to find chunk";
              _per_comp = 0;
          }
        } catch( ... ) {
           wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
        }
        _self.complete();
      }
  };

  download_status::download_status( const fc::shared_ptr<chunk_service>& cs, 
                   const fc::sha1& tornet_id, 
                   const fc::sha1& check_sum, uint64_t seed, fc::ostream& out )
  :my(*this,out) {
    my->_cs = cs;
    my->_tid = tornet_id;
    my->_checksum = check_sum;
    my->_seed = seed;
    my->_per_comp = 0;
    my->_status   = "idle";
  }

  download_status::~download_status() {
  }

  void  download_status::pause() {
  }

  void  download_status::start() {
    my->_download_fiber_complete = fc::async( [this](){ my->download_fiber(); }, "download_fiber" );
  }
  
  void  download_status::cancel() {
    my->_download_fiber_complete.cancel();
  }
  
  const fc::sha1&  download_status::tornet_id()const {
    return my->_tid;
  }

  const fc::sha1&  download_status::tornet_checksum()const {
    return my->_checksum;
  }

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
