#ifndef _TN_CHUNK_SERVICE_HPP_
#define _TN_CHUNK_SERVICE_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/fwd.hpp>
#include <fc/vector_fwd.hpp>

namespace fc {
  class path;
  class sha1;
}

namespace tn {

  namespace db {
    class chunk;
    class publish;
  }

  class node;
  class tornet_file;

  /**
   *  Provides an interface to two chunk databases, one local and one cache.
   *
   *  The local chunk database stores the chunks the user imports or downloads without
   *  respect to their distance from the node or access patterns. 
   *
   *  The cache chunk database stores chunks for other people so that it might earn
   *  credit with other nodes.  This cache database stores chunks based upon their
   *  access rate and distance from the node.
   *
   *  All chunks are random data encrypted via blowfish from the original file.  The blowfish
   *  key is the hash of the original file.  To restore a file you must know the hash of the
   *  file description chunk as well as the hash of the resulting file.  
   */
  class chunk_service {
    public:
      chunk_service( const fc::path&      dbdir,
                     const tn::node::ptr& n,
                     const fc::string&    name,
                     uint16_t             port );
      
       
      fc::shared_ptr<tn::db::chunk>&    get_cache_db();
      fc::shared_ptr<tn::db::chunk>&    get_local_db();
      fc::shared_ptr<tn::db::publish>&  get_publish_db();

      /**
       *  Loads infile from disk, creates a tornet file and returns the tornet_id and thechecksum.
       *  Optionally writes the tornet file to disk as outfile.
       */
      void import( const fc::path& infile, 
                   fc::sha1& tornet_id, fc::sha1& checksum,
                   const fc::path& outfile = fc::path() );

      /**
       *  Given a tornet_id and checksum, find the chunk, decrypt the tornetfile then find the
       *  chunks from the tornet file and finally reconstruct the file on disk.
       */
      void export_tornet( const fc::sha1& tornet_id, const fc::sha1& checksum );
     
      /**
       *  Reads the data for the chunk from the cache or local database.
       */
      void fetch_chunk( const fc::sha1& chunk_id, fc::vector<char>& data );
      void fetch_tornet( const fc::sha1& tornet_id, const fc::sha1& checksum, tornet_file& f );

      void publish_tornet( const fc::sha1& tornet_id, const fc::sha1& checksum, uint32_t rep = 3 );

      void enable_publishing( bool state );
      bool publishing_enabled()const;

    private:
      class impl;
      fc::fwd<impl,8*4> my;
  };

}


#endif // _CHUNK_SERVICE_HPP_
