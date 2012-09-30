#ifndef _TORNET_LINK_HPP_
#define _TORNET_LINK_HPP_
#include <fc/sha1.hpp>
#include <fc/static_reflect.hpp>


namespace tn {
  /**
   *  The information required to download a
   *  file from the network.  
   */
  struct link {
    link( const fc::string& b58 ); // convert from base58
    operator fc::string()const; // convert to base58

    link( const fc::sha1& i, uint64_t s )
    :id(i),seed(s){}
    link():seed(0){}


    fc::sha1   id;
    uint64_t   seed;
  };
}
FC_STATIC_REFLECT( tn::link,(id)(seed) )
FC_REFLECTABLE( tn::link )

#endif // _TORNET_LINK_HPP_
