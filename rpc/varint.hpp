#ifndef _BOOST_RPC_VARINT_HPP_
#define _BOOST_RPC_VARINT_HPP_
#include <boost/reflect/reflect.hpp>

namespace tornet { namespace rpc {

struct unsigned_int {
    unsigned_int( uint32_t v = 0 ):value(v){}

    operator uint32_t()const { return value; }

    template<typename T>
    unsigned_int& operator=( const T& v ) { value = v; return *this; }
    
    uint32_t value;
};

struct signed_int {
    signed_int( int32_t v = 0 ):value(v){}
    operator int32_t()const { return value; }
    template<typename T>
    signed_int& operator=( const T& v ) { value = v; return *this; }

    int32_t value;
};

} }

BOOST_REFLECT_TYPEINFO( tornet::rpc::unsigned_int )
BOOST_REFLECT_TYPEINFO( tornet::rpc::signed_int )

#endif 
