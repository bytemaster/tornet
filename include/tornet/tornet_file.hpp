#ifndef _TORNET_FILE_HPP_
#define _TORNET_FILE_HPP_
#include <fc/string.hpp>
#include <fc/sha1.hpp>
#include <fc/value.hpp>
#include <fc/vector.hpp>
#include <fc/reflect.hpp>


namespace tn {

/**
 *  Defines how a tornet file is represented on disk.  For small files < 1MB
 *  the data is kept inline with the chunk.
 */
struct tornet_file {
  struct chunk_data {
    chunk_data():size(0){}
    chunk_data( uint32_t s, uint64_t se, const fc::sha1& i  )
    :size(s),seed(se),id(i){}

    uint32_t              size;   // the usable size of the chunk (chunks must be a multiple of 8 bytes)

    /**
     *  The random seed used to randomize the encrypted data.  To prevent users from
     *  uploading 'unencrypted' data, only chunks that contain statistically random 
     *  data as measured by the chi squared algorithm may be uploaded.  The threshold for
     *  this is that the chi-sq propbability must be between 20 and 80 which means that
     *  multiple seeds may need to be tried before the desired chi-sq distribution is 
     *  achieved as 40% of all random distributions will fail to satisify this 
     *  measure of randomness. 
     *
     *  The seed is applied after the chunk is 'encrypted' via blowfish and must be
     *  unapplied before decrypting with blowfish. 
     *
     *  The random algorithm used is mt19937
     */
    uint64_t              seed;   

    /**
     *  This is the hash by which the data can be found in the DHT.
     *
     *  sha1( randomize(seed, blowfish( data ) ) )
     */
    fc::sha1              id;
    fc::vector<uint32_t>  slices;
  };
  tornet_file():version(0),compression(0),size(0){}
  tornet_file( const fc::string& n, uint64_t s )
  :name(n),size(s){}
  
  uint8_t                   version;
  uint8_t                   compression;
  fc::string                mime;
  fc::sha1                  checksum;
  fc::string                name;
  uint64_t                  size;
  fc::vector< chunk_data >  chunks;
 // fc::value                 properties; 
  fc::vector<char>          inline_data;
};

} // namespace tn

FC_REFLECT( tn::tornet_file::chunk_data,
  (size)(seed)(id)(slices) )

FC_REFLECT( tn::tornet_file,
  (version)(compression)(mime)(name)(checksum)(size)(chunks)(inline_data) )


#endif 
