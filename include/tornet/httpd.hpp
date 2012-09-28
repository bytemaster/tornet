#ifndef _TORNET_HTTPD_HPP_
#define _TORNET_HTTPD_HPP_
#include <fc/shared_ptr.hpp>

namespace tn {
  class name_service;
  class chunk_service;
  class node;

  /**
   *  A simple http server that provides content from
   *  tornet.
   */
  class httpd : public fc::retainable {
    public:
      httpd( const fc::shared_ptr<node>& n,
             const fc::shared_ptr<name_service>& ns, 
             const fc::shared_ptr<chunk_service>& cs 
           );
      ~httpd();

      void listen( uint16_t port );

    private:
      class impl
      fc::fwd<impl,8> my;
  };

}


#endif // _TORNET_HTTPD_HPP_
