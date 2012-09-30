#ifndef _NAME_SERVICE_CLIENT_HPP_
#define _NAME_SERVICE_CLIENT_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/fwd.hpp>

#include <tornet/name_service_messages.hpp>
#include <tornet/service_client.hpp>
#include <fc/future.hpp>

namespace tn {
  class node;

  class name_service_client : virtual public tn::service_client {
    public:
      typedef fc::shared_ptr<name_service_client> ptr;
      name_service_client( tn::node& n, const fc::sha1& id );
      ~name_service_client();

      virtual const fc::string& name()const  { return static_name(); }
      static const fc::string& static_name() { static fc::string n("name_service_client"); return n; }

      fc::future<fetch_block_reply> fetch_head();
      fc::future<fetch_block_reply> fetch_block( const fc::sha1& id );
      fc::future<fetch_block_reply> fetch_block_transactions( const fc::sha1& id );
      fc::future<fetch_trxs_reply>  fetch_trxs( const fetch_trxs_request& r );

      void  broadcast( const broadcast_msg& m );

    private:
      class impl;
      fc::fwd<impl,128> my;
  };

}



#endif // _NAME_SERVICE_CLIENT_HPP_
