#ifndef _TN_DB_CHUNK_HPP_
#define _TN_DB_CHUNK_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/vector_fwd.hpp>
//#include <fc/signals.hpp>
#include <fc/filesystem.hpp>
#include <fc/sha1.hpp>

namespace fc {
  class const_buffer;
  class mutable_buffer;
}

namespace tn { namespace db {

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
  class chunk : public fc::retainable {
    public:
      typedef fc::shared_ptr<chunk> ptr;
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
        fc::sha1 key;
        meta     value;
      };

      chunk( const fc::sha1& node_id, const fc::path&  dir );
      ~chunk();

      //fc::sha1 get_maximum_range()const;
      //uint64_t     get_maximum_db_size()const;

      void fetch_worst_performance( fc::vector<meta_record>& m, int n = 1 );
      void fetch_best_opportunity( fc::vector<meta_record>& m, int n = 1 );

      void init();
      void close();

      bool store_chunk( const fc::sha1& id, const fc::vector<char>& b  );
      bool store_chunk( const fc::sha1& id, const fc::const_buffer& b  );
      bool fetch_chunk( const fc::sha1& id, const fc::mutable_buffer& b, uint64_t offset = 0 );

      bool fetch_meta( const fc::sha1& id, meta& m, bool auto_inc );
      bool store_meta( const fc::sha1& id, const meta& m );

      bool del_data( const fc::sha1& id );
      bool del_meta( const fc::sha1& id );
      bool del( const fc::sha1& id );

      uint32_t count();
      bool fetch_index( uint32_t recnum, fc::sha1& id, meta& m );

      void sync();

      // these signals are emited from the chunk thread, so
      // be sure to delegate to the proper thread and not to
      // perform any lengthy calculations in your handler or you
      // will block ongoing database operations
     /*
      boost::signal<void(uint32_t)> record_inserted;
      boost::signal<void(uint32_t)> record_changed;
      boost::signal<void(uint32_t)> record_removed;
      */

    private:
      class chunk_private* my;

  };

} } 

#endif // _TN_DB_CHUNK_HPP_
