#ifndef _TORNET_HTTPD_HPP_
#define _TORNET_HTTPD_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/fwd.hpp>

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
      httpd(uint16_t port);
      ~httpd();

      class impl;
    private:
      fc::fwd<impl,48> my;
  };

}


#endif // _TORNET_HTTPD_HPP_
