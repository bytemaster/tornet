#ifndef _TORNET_CALC_SERVICE_HPP_
#define _TORNET_CALC_SERVICE_HPP_
#include <tornet/rpc/service.hpp>

namespace tornet { namespace service {


  class calc_service : public tornet::rpc::service {
    public:
      calc_service( const tornet::node::ptr& node, const std::string& name, uint16_t port,
              boost::cmt::thread* t = &boost::cmt::thread::current() ) 
      :service(node,name,port,t){}
      
      int add( int n ) { slog( "%1%", n); return grand_total += n; } 

    protected:
      virtual boost::any init_connection( const rpc::connection::ptr& con );
      
      int grand_total; 
  };


  class calc_connection {
    public:
      calc_connection( calc_service* cs ):calc_s(cs){};

      int add( int n ) {  return sum += calc_s->add(n); }
      int sum;

      calc_service* calc_s;
  };

} }


#endif
