#ifndef _TORNET_FILE_HPP_
#define _TORNET_FILE_HPP_
#include <fc/string.hpp>
#include <fc/sha1.hpp>
#include <fc/value.hpp>
#include <fc/vector.hpp>
#include <fc/static_reflect.hpp>
#include <fc/reflect.hpp>


namespace tn {

/**
 *  Defines how a tornet file is represented on disk.  For small files < 1MB
 *  the data is kept inline with the chunk.
 */
struct tornet_file {
  struct chunk_data {
    chunk_data():size(0){}
    chunk_data( uint32_t s, const fc::sha1& i  )
    :size(s),id(i){}

    uint32_t            size;
    fc::sha1            id;
    fc::vector<uint32_t>  slices;
  };
  tornet_file():size(0){}
  tornet_file( const fc::string& n, uint64_t s )
  :name(n),size(s){}
  
  fc::sha1                  checksum;
  fc::string                name;
  uint64_t                  size;
  fc::vector< chunk_data >  chunks;
 // fc::value                 properties; 
  fc::vector<char>          inline_data;
};

} // namespace tn

FC_STATIC_REFLECT( tn::tornet_file::chunk_data,
  (size)(id)(slices) )

FC_STATIC_REFLECT( tn::tornet_file,
  (checksum)(name)(size)(chunks)(inline_data) )

FC_REFLECTABLE( tn::tornet_file::chunk_data )

FC_REFLECTABLE( tn::tornet_file )

#endif 
