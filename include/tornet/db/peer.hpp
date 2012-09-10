#ifndef _TORNET_DB_PEER_HPP_
#define _TORNET_DB_PEER_HPP_
#include <stdint.h>
#include <fc/function.hpp>
#include <fc/sha1.hpp>
#include <fc/filesystem.hpp>
#include <fc/shared_ptr.hpp>
#include <fc/signals.hpp>

namespace fc { 
  namespace ip {
    class endpoint;
  } 
}

namespace tn { namespace db {


  /**
   *  @class db::peer
   *
   *  Provides a dedicated thread for accessing the peer database.  This database
   *  sorts peers by endpoint and distance from node id.
   */
  class peer : public fc::retainable {
    public:
      typedef fc::shared_ptr<peer> ptr;
      struct record {
        record();

        /// Valid of Public Key is not 0
        bool valid()const;
        fc::sha1 id()const;

        uint32_t  last_ip;
        uint16_t  last_port;
        uint16_t  availability;  // percent avail
        uint32_t  est_bandwidth; 
        uint64_t  nonce[2];
        uint64_t  first_contact;
        uint64_t  last_contact;
        uint32_t  avg_rtt_us;
        uint64_t  sent_credit; // how much did we provide them
        uint64_t  recv_credit; // how much did this peer provide us 
        uint8_t   firewalled;
        uint16_t  buddy_port;      
        uint32_t  buddy_ip;    // public, non-firewalled ip of buddy 
        char      connected;
        uint8_t   rank;           // the rank of this node
        uint8_t   published_rank; // the rank last sent to this node of my node
        char      public_key[256];
        char      bf_key[56];
        char      recv_btc[40]; // address to recv money from node id
        char      send_btc[40]; // address to send money to node id
      };

      peer( const fc::sha1& nid, const fc::path& dir );
      ~peer();

      void init();
      void close();
      void sync();
      uint32_t count();

      bool fetch_by_endpoint( const fc::ip::endpoint& ep, fc::sha1& id, record& m );
      bool fetch_index( uint32_t recnum, fc::sha1& id, record& r );
      bool fetch( const fc::sha1& id, record& m );
      bool store( const fc::sha1& id, const record& m );
      bool exists( const fc::sha1& id );

      // these signals are emited from the chunk thread, so
      // be sure to delegate to the proper thread and not to
      // perform any lengthy calculations in your handler or you
      // will block ongoing database operations
      boost::signal<void(uint32_t)> record_inserted;
      boost::signal<void(uint32_t)> record_changed;
      boost::signal<void(uint32_t)> record_removed;
   private:
      class peer_private* my;
  };

} } // tn::db

#endif
