#ifndef _TN_HOST_HPP_
#define _TN_HOST_HPP_
#include <fc/sha1.hpp>
#include <fc/ip.hpp>

namespace tn {
  struct host {
    host(){}
    host( const fc::sha1& _id, const fc::ip::endpoint& _ep ):id(_id),ep(_ep){}
    fc::sha1                     id;
    fc::ip::endpoint             ep;
    // any hosts required to perform nat handshake on intro.
    fc::vector<fc::ip::endpoint> nat_hosts;
  };
}
#endif
