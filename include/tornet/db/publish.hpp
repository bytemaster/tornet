#ifndef _TORNET_DB_PUBLISH_HPP_
#define _TORNET_DB_PUBLISH_HPP_
#include <stdint.h>
#include <fc/signals.hpp>
#include <fc/sha1.hpp>
#include <fc/filesystem.hpp>
#include <fc/shared_ptr.hpp>

namespace tn { namespace db {


  /**
   *  @class db::publish
   *
   *  Provides a dedicated thread for accessing the publish database. 
   *
   *  For each [chunk_id] store the last time that chunk was 
   *  queried and how popular that chunk is
   *  chunk_id was hosted on node_id and how often it was accessed 
   *  on that node.
   *
   *  A secondary index is maintained for next_update which lets us
   *  find the least recently queried chunk and do a query;
   *
   *  A secondary index is maintained for node_id so that all chunks
   *  hosted on a particular node may be queried at the same time. 
   */
  class publish : public fc::retainable {
    public:
      typedef fc::shared_ptr<publish> ptr;
      typedef fc::sha1 id;
      struct record {
        record(uint64_t nu=0, uint64_t ai=0, uint32_t hc = 0, uint32_t dhc = 0);
        uint64_t next_update;        // the next time we need to query this chunk
                                     // this should vary based upon existing host count
                                     // there is no need to 'check' popular content as often
                                     // as unpopular content.
        uint64_t access_interval;    // average access rate
        uint32_t host_count;         // number of nodes hosting data
        uint32_t desired_host_count;
      };

      publish( const fc::path& dir );
      ~publish();

      void     init();
      void     close();
      void     sync();
      uint32_t count();

      void remove( const id& chunk_id, const id& node_id );
      bool fetch( const id& chunk_id, record& r );
      bool fetch_next( id& chunk_id, record& r);
      bool fetch_index( uint32_t recnum, fc::sha1& id, record& m );
      bool exists( const fc::sha1& id );
      bool store( const id& chunk_id, const record& m );

      // these signals are emited from the chunk thread, so
      // be sure to delegate to the proper thread and not to
      // perform any lengthy calculations in your handler or you
      // will block ongoing database operations
      boost::signal<void(uint32_t)> record_inserted;
      boost::signal<void(uint32_t)> record_changed;
      boost::signal<void(uint32_t)> record_removed;
   private:
      class publish_private* my;
  };

} } // tn::db

#endif
