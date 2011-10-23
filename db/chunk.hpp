#ifndef _TORNET_DB_CHUNK_HPP_
#define _TORNET_DB_CHUNK_HPP_
#include <stdint.h>
#include <boost/signals.hpp>
#include <scrypt/sha1.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/asio/buffer.hpp>

namespace tornet { namespace db { 

  /**
   *  @class chunk
   *  
   *  Encapsulates access to chunk data in a thread-safe manner.  All access to
   *  this database happens in a dedicated thread so as to avoid locking issues
   *  and blocking disk-access from delaying the main network thread.
   */
  class chunk {
    public:
      /**
       *  Meta information about chunks stored in the DB
       */
      struct meta {
          meta();

          uint64_t first_update;
          uint64_t last_update;
          uint64_t query_count;
          uint32_t size;

          uint64_t access_interval()const;
      };
      chunk( const scrypt::sha1& node_id, const boost::filesystem::path&  dir );
      ~chunk();

      void init();
      void close();

      void store_chunk( const scrypt::sha1& id, const boost::asio::const_buffer& b  );
      bool fetch_chunk( const scrypt::sha1& id, const boost::asio::mutable_buffer& b, uint64_t offset = 0 );

      bool fetch_meta( const scrypt::sha1& id, meta& m );
      bool store_meta( const scrypt::sha1& id, const meta& m );

      bool del_data( const scrypt::sha1& id );
      bool del_meta( const scrypt::sha1& id );
      bool del( const scrypt::sha1& id );

      uint32_t count();
      bool fetch_index( uint32_t recnum, scrypt::sha1& id, meta& m );

      void sync();

      // these signals are emited from the chunk thread, so
      // be sure to delegate to the proper thread and not to
      // perform any lengthy calculations in your handler or you
      // will block ongoing database operations
      boost::signal<void(uint32_t)> record_inserted;
      boost::signal<void(uint32_t)> record_changed;
      boost::signal<void(uint32_t)> record_removed;

    private:
      class chunk_private* my;

  };

} } 


#endif
