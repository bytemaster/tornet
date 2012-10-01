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

      fc::future<publish_name_reply> publish_name( const publish_name_request& );
      fc::future<resolve_name_reply> resolve_name( const resolve_name_request& );

    private:
      class impl;
      fc::fwd<impl,128> my;
  };

}



#endif // _NAME_SERVICE_CLIENT_HPP_
