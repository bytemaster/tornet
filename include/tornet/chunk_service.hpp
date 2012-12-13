#pragma once
#include <fc/shared_ptr.hpp>
#include <fc/vector.hpp>

#include <cafs.hpp>

namespace fc {
  class path;
  class sha1;
  class ostream;
  class mutable_buffer;
}

namespace tn {

  namespace db {
    class chunk;
    class publish;
  }
  class node;
  class tornet_file;
  class chunk_service;
  class download_status;



  /**
   *  Provides an interface to two chunk databases, one local and one cache.
   *  Handles requests from other nodes for chunks from the cache.
   *  Provides interface to upload and download chunks.
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
  class chunk_service : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<chunk_service> ptr;

      chunk_service( const fc::path&      dbdir, const fc::shared_ptr<tn::node>& n );
      virtual ~chunk_service();

      void shutdown();
       
      fc::shared_ptr<tn::db::chunk>&    get_cache_db();
      fc::shared_ptr<tn::db::chunk>&    get_local_db();
      fc::shared_ptr<cafs>              get_cafs();
      fc::shared_ptr<tn::db::publish>&  get_publish_db();
      fc::shared_ptr<tn::node>&         get_node();

      /**
       *  Loads the file from disk, turns it into chunks and then
       *  starts publishing those chunks on the network at the
       *  desired replication rate.
       *
       *  All published files show up in the link database. Once
       *  the file has been published, local changes are not reflected,
       *  it will have to be removed from the link DB.
       */
      cafs::link publish( const fc::path& p, uint32_t rep = 3 );
     
      /**
       *  Reads the data for the chunk from the cache or local database.
       *
       *  @return an empty vector if no chunk was found
       *  This data can be gotten directly from the cafs... 
      fc::vector<char> fetch_chunk( const fc::sha1& chunk_id );
       */

      /**
       *  Assuming the data is stored locally, returns a tornet file,
       *  otherwise throws an exception if the chunk has not been 
       *  downloaded.
      tornet_file fetch_tornet( const tn::link& ln );
       */

      /**
       *  Performs a synchronous download of the specified chunk
       */
      fc::vector<char> download_chunk( const fc::sha1& chunk_id );

      /**
       *  Effectively the same as download_chunk(); fetch_tornet();
      tornet_file download_tornet( const tn::link& ln );
       */

      void enable_publishing( bool state );
      bool is_publishing()const;
    private:
      class impl;
      fc::shared_ptr<impl> my;
  };

}

