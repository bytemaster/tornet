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
   *
   *  Maintains a database with the following tables:
   *
   *  chunk meta: - changes every time a query comes in
   *    - sha1           - distance from node (primary)
   *    - int            - query count
   *    - int            - last update    (index)
   *    - int            - query_interval (index)
   *    - int            - size;
   *
   *  New chunks have a query interval of 0xffffffff ms
   *  When a new query comes in you now have 2 time stamps with which you
   *  can calculate an interal.  
   *
   *  new_interval = (old_interval * query_count + latest_interval) / (query_count + 1)
   *
   *  This will cause the query interval to approach the average over time.
   *
   *  Periodically the least recently updated chunk can recalculate its interval by averaging in a '0'.
   *
   *  chunk   - stores chunk data
   *    - sha1           - distance from node
   *    - char[128*1024] - data
   *
   *  references: - stores pointers to chunk data
   *    - sha1 - distance from node
   *    - sha1 - node id publishing the node.
   *
   *
   *  Maintains a database of a maximum size and range.
   */
  class chunk {
    public:
      typedef boost::shared_ptr<chunk> ptr;
      /**
       *  Meta information about chunks stored in the DB
       */
      struct meta {
          meta();

          uint64_t first_update;
          uint64_t last_update;
          uint64_t query_count;
          uint32_t size;
          uint8_t  distance_rank;

          uint64_t now()const;
          uint64_t access_interval()const;
          int64_t  price()const;
          int64_t  annual_query_count()const;
          int64_t  annual_revenue_rate()const;
      };
      struct meta_record {
        scrypt::sha1 key;
        meta         value;
      };

      chunk( const scrypt::sha1& node_id, const boost::filesystem::path&  dir );
      ~chunk();

      scrypt::sha1 get_maximum_range()const;
      uint64_t     get_maximum_db_size()const;

      void fetch_worst_performance( std::vector<meta_record>& m, int n = 1 );
      void fetch_best_opportunity( std::vector<meta_record>& m, int n = 1 );

      void init();
      void close();

      bool store_chunk( const scrypt::sha1& id, const boost::asio::const_buffer& b  );
      bool fetch_chunk( const scrypt::sha1& id, const boost::asio::mutable_buffer& b, uint64_t offset = 0 );

      bool fetch_meta( const scrypt::sha1& id, meta& m, bool auto_inc );
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
