#include <tornet/link.hpp>
#include <fc/base58.hpp>
#include <fc/raw.hpp>
namespace tn {
    link::link( const fc::string& b58 ) {
      char buffer[sizeof(id)+sizeof(seed)];
      fc::from_base58( b58, buffer, sizeof(buffer) );

      fc::datastream<const char*> ds(buffer,sizeof(buffer) );
      fc::raw::unpack( ds, *this );
    }
    link::operator fc::string()const  {
      char buffer[sizeof(id)+sizeof(seed)];
      fc::datastream<char*> ds(buffer,sizeof(buffer) );
      fc::raw::pack( ds, *this );
      return fc::to_base58( buffer, sizeof(buffer) );
    }
} // namespace tn;
