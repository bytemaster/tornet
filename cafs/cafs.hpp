#ifndef _CAFS_HPP_
#define _CAFS_HPP_
#include <fc/vector.hpp>
#include <fc/string.hpp>
#include <fc/shared_ptr.hpp>
#include <fc/reflect.hpp>
#include <fc/filesystem.hpp>
#include <fc/sha1.hpp>
#include <fc/buffer.hpp>

/**
 *  Content Addressable File System
 *
 *  Chunks are stored on disk using the structure
 *
 *  chunks/aa/bb/cc/ddeeffgghh... for the sha1 hash.
 *  files/aa/bb/cc/ddeeffgghh... for the sha1 hash.
 *
 *  File references are stored in the same way as chunk hashs.
 */
class cafs  {
  public:
    cafs();
    ~cafs();

    enum file_type {
      file_data_type   = 0, // the chunks comprise a regular file
      directory_type   = 1, // the chunks comprise a directory file
      file_header_type = 2  // the chunks comprise an even bigger file_header structure
    };
  

    /**
     *  A reference to a chunk that starts with a 
     *  uint8_t type before the data of the chunk.
     *
     *  This allows us to decode the chunk and interpret 
     *  its contents.
     */
    struct link {
      link( const fc::string& b58 ); // convert from base58
      operator fc::string()const; // convert to base58

      link( const fc::sha1& i, uint64_t s )
      :id(i),seed(s){}
      link():seed(0){}
      
      friend bool operator==(const link& a, const link& b ) { return a.seed == b.seed && a.id == b.id; }
      friend bool operator!=(const link& a, const link& b ) { return !(a==b); }

      fc::sha1 id;
      uint64_t seed;
    };
    
    /**
     *  A chunk is made up of many slices;
     */
    struct chunk_header {
      struct slice {
        slice( uint32_t s = 0, const fc::sha1& h = fc::sha1()):size(s),hash(h){}
        uint32_t size; 
        fc::sha1 hash;
      };
      // fc::raw::pack( fc::sha1::encoder, *this )
      fc::sha1 calculate_id()const;

      fc::vector<slice> slices;
    };

    /**
     *  Small files are mapped to individual slices
     */
    struct file_ref {
      file_ref();
      file_ref( file_type t, uint32_t slice );

      fc::sha1 content; // hash of file contents (for validation/addressable/caching purposes)
      fc::sha1 chunk;   // chunk where it is located
      uint64_t seed;    // how to decode the chunk
      uint32_t pos;     // the position in the chunk
      uint32_t size;    // the size of the file
      uint8_t  type;    // file_data, file_header, directory 
    };
    
    /**
     *  Large files are broken into multiple chunks.
     */
    struct file_header {
       uint64_t             file_size;
       uint8_t              type; // file_data, directory(large directory obj), file_header (nested header...)

       void add_chunk( const fc::sha1& cid, uint64_t seed, const chunk_header& h );
       struct chunk{
          chunk( const fc::sha1& h = fc::sha1(), uint64_t s = 0 );
          fc::sha1     hash;
          uint64_t     seed;
       };
       fc::vector<chunk> chunks;
    };

    /**
     *  Given a name you can quickly find the file content without
     *  having to sequentially scan the entire directory nor
     *  jump over all of the unnecessary file_ref data.
     */
    struct directory {
        struct entry {
            entry( const fc::string& n = fc::string() ):name(n){}
            fc::string name;
            file_ref   ref;     // one location where the file can be found (Where to find it)
        };
        fc::vector<entry>  entries; // sorted by name
    };


    void open( const fc::path& dir );
    void close();

    link         import( const fc::path& p );

    /**
     *  Creates chunks for the file located at p.
     *
     *  @pre fc::is_regular_file(p) 
     */
    file_header  import_file( const fc::path& p );
    /**
     *  @pre fc::is_directory(p) 
     */
    directory    import_directory( const fc::path& p );

    /**
     *  The ID of a chunk is the sha1(slices) of that chunk.  
     */
    fc::sha1 reserve_chunk( const chunk_header& ch );

    /**
     *  sha1(data) must be a valid slice as defined by the chunk header.
     */
    void     write_slice( const fc::sha1& chunk_id, const fc::vector<char>& data );
    fc::sha1 store_chunk( const chunk_header& h, const fc::vector<char>& data );

    /**
     *  @return an empty vecotr if slice ID is not set within chunk_id
     */
    fc::vector<char> read_chunk_slice( const fc::sha1& chunk_id, const fc::sha1& slice_id );

    /**
     *  @return the slice at the given location.
     */
    fc::vector<char> read_chunk_slice( const fc::sha1& chunk_id, uint32_t index );

    
    /**
     *  @param the hash of the orignal file
     *  @param r - if not null, is populated with the file reference
     *  
     *  @return true if a file reference is known for fhash
     */
    bool get_file_ref( const fc::sha1& fhash, file_ref* r = nullptr );

    /**
     *  Given the hash of a file, return its contents.
     */
    fc::vector<char> get_file( const fc::sha1& fhash );

    /**
     *  Return the contents of a raw chunk.
     */
    fc::vector<char> get_chunk( const fc::sha1& id, uint32_t pos = 0, uint32_t size = -1 );

  private:
    struct impl;
    fc::shared_ptr<impl> my;
};

bool     is_random( const fc::vector<char>& data );
uint64_t randomize( fc::vector<char>& data, uint64_t seed );
void     derandomize( uint64_t seed, fc::vector<char>& data );
void     derandomize( uint64_t seed, const fc::mutable_buffer& b );

FC_REFLECT( cafs::link,                (id)(seed)                              )
FC_REFLECT( cafs::chunk_header::slice, (size)(hash)                            )
FC_REFLECT( cafs::chunk_header,        (slices)                                )
FC_REFLECT( cafs::file_ref,            (content)(chunk)(seed)(pos)(size)(type) )
FC_REFLECT( cafs::file_header::chunk,  (hash)(seed)                            )
FC_REFLECT( cafs::file_header,         (chunks)                                )
FC_REFLECT( cafs::directory::entry,    (name)(ref)                             )           
FC_REFLECT( cafs::directory,           (entries)                               )



#endif // _CAFS_HPP_
