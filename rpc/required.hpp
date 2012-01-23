#ifndef __TORNET_RPC_REQUIRED_HPP__
#define __TORNET_RPC_REQUIRED_HPP__
#include <boost/optional.hpp>

namespace tornet { namespace rpc { 
    template<typename T>
    struct required : public boost::optional<T>
    {
        typedef boost::optional<T> base;
        required(){}
        required( const T& v )
        :boost::optional<T>(v){}

        operator T& ()            { return **this; }
        operator const T& ()const { return **this; }
        using base::operator=;
        using base::operator*;
        using base::operator!;
    };

} } // namespace boost::rpc 
#endif // __BOOST_RPC_REQUIRED_HPP__
