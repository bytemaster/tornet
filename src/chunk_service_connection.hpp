#ifndef _CHUNK_SERVICE_CONNECTION_HPP_
#define _CHUNK_SERVICE_CONNECTION_HPP_
#include <fc/shared_ptr.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/chunk_service.hpp>

#include <tornet/chunk_service_messages.hpp>

namespace tn {


    class chunk_service_connection : virtual public fc::retainable{
      public:
        typedef fc::shared_ptr<chunk_service_connection> ptr;
        chunk_service_connection( const tn::udt_channel& c, chunk_service& cs ) 
        :_cs(cs),_chan(c){ }

        /**
         *  @param bytes - if -1 then the entire chunk will be returned starting from offset
         *  
         *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
         */
        fetch_response fetch( const fc::sha1& id, int32_t bytes = -1, uint32_t offset = 0 );
   
   
        chunk_service& _cs;
        udt_channel    _chan;
    };


}


#endif// _CHUNK_SERVICE_CONNECTION_HPP_
