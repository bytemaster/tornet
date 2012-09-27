#ifndef _CHUNK_SERVICE_CONNECTION_HPP_
#define _CHUNK_SERVICE_CONNECTION_HPP_
#include <fc/shared_ptr.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/raw_rpc.hpp>
#include <tornet/chunk_service_messages.hpp>
#include <tornet/db/chunk.hpp>
#include <fc/json.hpp>
extern "C" {
double pochisq(
    	const double ax,    /* obtained chi-square value */
     	const int df	    /* degrees of freedom */
     	);
}

namespace tn {


    class chunk_service_connection : virtual public fc::retainable{
      public:
        typedef fc::shared_ptr<chunk_service_connection> ptr;
        chunk_service_connection( const tn::udt_channel& c, chunk_service& cs ) 
        :_cs(cs),_chan(c){ 
          _rpc.add_method( fetch_method_id, this, &chunk_service_connection::fetch );
          _rpc.add_method( store_method_id, this, &chunk_service_connection::store );
          _rpc.connect(_chan);
        }

        /**
         *  @param bytes - if -1 then the entire chunk will be returned starting from offset
         *  
         *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
         */
        fetch_response fetch( const fetch_request& r ) {
          slog( "fetch %s", fc::json::to_string(r).c_str() );
          fetch_response reply;
          tn::db::chunk::meta met;
          bool found = _cs.get_cache_db()->fetch_meta( r.target, met, true );
          if( !found ) {
            reply.result = chunk_session_result::unknown_chunk;
            reply.query_interval = 0;
            reply.offset = 0;
          //  slog( "response %s", fc::json::to_string(reply).c_str() );
            return reply;
          }

          reply.result = met.size ? chunk_session_result::available : chunk_session_result::ok;
          reply.query_interval = met.access_interval();
          
          if( r.length != 0 && reply.result == chunk_session_result::available ) {
              if( r.length < 0 ) { // send it all
                if( r.offset >= met.size ) {
                  reply.result = chunk_session_result::invalid_range;
                } else {
                  slog( "met size %d - offset %d", met.size, r.offset );  
                  reply.data.resize( met.size - r.offset );
                }
              } else {
                if( r.offset >= met.size ) {
                  reply.result = chunk_session_result::invalid_range;
                } else {
                  reply.data.resize( fc::min( size_t(r.length), size_t(met.size - r.offset) ) );
                }
              }
              if(reply.result != chunk_session_result::invalid_range ) {
                  slog( "%s size %d  off %d ", fc::string( r.target ).c_str(), reply.data.size(), r.offset );
                  _cs.get_cache_db()->fetch_chunk( r.target, fc::mutable_buffer( reply.data.data(), reply.data.size() ), r.offset ); 
                  slog( "fetched %s from DB", fc::string(fc::sha1::hash(reply.data.data(), reply.data.size() ) ).c_str() );
              }
          }
          _cs.get_cache_db()->store_meta( r.target, met );

//          slog( "response %s", fc::json::to_string(reply).c_str() );
          return reply;
        }


        store_response store( const fc::vector<char>& data ) {  
            // verify entropy of data, only store data of high-entropy. 
            // to prevent 'hackers' from distributing unencrypted data that
            // could get the individual hosting the data in trouble. 
            if( !is_random( data ) )
              return store_response( chunk_session_result::data_not_random );

            auto cdb = _cs.get_cache_db();
            
            if( data.size() > 1024 * 1024 ) {
              wlog( "Data size too big %lld", data.size() );
              return store_response( chunk_session_result::invalid_size );
            }
            
            fc::sha1 cid = fc::sha1::hash( data.data(), data.size() );
            slog( "store chunk %s of size %d", fc::string(cid).c_str(), data.size() );
            
            db::chunk::meta met;
            cdb->fetch_meta( cid, met, true );

            // TODO: verify that the individual storing the chunk has equal or higher rank
            //
            // TODO: optionally only allow paying customers to 'store' data

            // TODO: determine whether this chunk is 'profitable to store' and how
            //       much we will charge the individual uploading the chunk for the 
            //       storage.  
            
            if( met.size == 0 )
                cdb->store_chunk( cid, fc::const_buffer(data.data(), data.size() ) );
            
            return store_response( chunk_session_result::ok );
        }
   
   
        chunk_service& _cs;
        raw_rpc        _rpc;
        udt_channel    _chan;
    };


}


#endif// _CHUNK_SERVICE_CONNECTION_HPP_
