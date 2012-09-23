#ifndef _TORNET_SERVICE_CLIENT_HPP_
#define _TORNET_SERVICE_CLIENT_HPP_
#include <fc/string.hpp>
#include <fc/shared_ptr.hpp>
namespace tn {
  class service_client : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<service_client> ptr;

      virtual const fc::string& name()const = 0;

    protected:
      virtual ~service_client(){}
  };
}
#endif// _TORNET_SERVICE_CLIENT_HPP_
