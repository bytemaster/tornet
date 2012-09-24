#ifndef _TORNET_CHUNK_SERVICE_CLIENT_HPP_
#define _TORNET_CHUNK_SERVICE_CLIENT_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/fwd.hpp>
#include <tornet/node.hpp>
#include <tornet/service_client.hpp>
#include <fc/future.hpp>

#include <tornet/chunk_service_messages.hpp>

namespace tn {
    class connection;

    class chunk_service_client : virtual public tn::service_client {
      public:
        typedef fc::shared_ptr<chunk_service_client> ptr;
        chunk_service_client( tn::node& n, const fc::sha1& id );

        virtual const fc::string& name()const  { return static_name(); }
        static const fc::string& static_name() { static fc::string n("chunk_service_client"); return n; }

        /**
         *  @param bytes - if -1 then the entire chunk will be returned starting from offset
         *  
         *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
         */
        fc::future<fetch_response> fetch( const fc::sha1& id, int32_t bytes = -1, uint32_t offset = 0 );
        fc::future<store_response> store( fc::vector<char>& d );

      protected:
        ~chunk_service_client();
      private:
        class impl;
        fc::fwd<impl,128> my;
    };
}

#endif // _TORNET_CHUNK_SERVICE_CLIENT_HPP_
