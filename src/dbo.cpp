#include "persist.hpp"
#include <fc/string.hpp>
#include <fc/sha1.hpp>


namespace tn {
    TornetLink::TornetLink( const fc::sha1& _id, const fc::sha1& check, uint64_t _s, const fc::string& _name, uint64_t _size, uint64_t _tsize ) 
    :tid( fc::string(_id).c_str()), check( fc::string(check).c_str()),seed(_s), name(_name.c_str()), tornet_size(_tsize), file_size(_size)
    {
      
    }
    Torsite::Torsite() {
      seed         = 0;
      replicate    = 0;
      availability = 0;
    }
}
