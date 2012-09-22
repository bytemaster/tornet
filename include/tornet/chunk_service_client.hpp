#ifndef _TORNET_CHUNK_SERVICE_CLIENT_HPP_
#define _TORNET_CHUNK_SERVICE_CLIENT_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/fwd.hpp>
#include <tornet/node.hpp>

#include <tornet/chunk_service_messages.hpp>

namespace tn {
    class chunk_service_client : virtual public fc::retainable {
      public:
        typedef fc::shared_ptr<chunk_service_client> ptr;
        chunk_service_client( const tn::node::ptr& n, const fc::sha1& remote_id );
        ~chunk_service_client();

        /**
         *  @param bytes - if -1 then the entire chunk will be returned starting from offset
         *  
         *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
         */
        fetch_response fetch( const fc::sha1& id, int32_t bytes = -1, uint32_t offset = 0 );
      private:
        class impl;
        fc::fwd<impl,8> my;
    };
}

#endif // _TORNET_CHUNK_SERVICE_CLIENT_HPP_
