#ifndef _TORNET_DB_PEER_HPP_
#define _TORNET_DB_PEER_HPP_
#include <stdint.h>
#include <boost/signals.hpp>
#include <scrypt/sha1.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/asio.hpp>

namespace tornet { namespace db {


  /**
   *  @class db::peer
   *
   *  Provides a dedicated thread for accessing the peer database.  This database
   *  sorts peers by endpoint and distance from node id.
   */
  class peer {
    public:
      typedef boost::shared_ptr<peer> ptr;
      struct record {
        record();

        /// Valid of Public Key is not 0
        bool valid()const;
        scrypt::sha1 id()const;

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
        uint32_t  buddy_ip;    // public, non-firewalled ip of buddy 
        uint16_t  buddy_port;      
        char      connected;
        uint8_t   rank;
        char      public_key[256];
        char      bf_key[56];
        char      recv_btc[40]; // address to recv money from node id
        char      send_btc[40]; // address to send money to node id
      };

      struct endpoint {
        endpoint():ip(0),port(0){}
        endpoint( const boost::asio::ip::udp::endpoint& ep )
        :ip( ep.address().to_v4().to_ulong() ),port(ep.port()){}

        operator boost::asio::ip::udp::endpoint()const {
          return boost::asio::ip::udp::endpoint( boost::asio::ip::address_v4( (unsigned long)ip ), port ); 
        }
        bool operator==(const endpoint& ep)const { return ep.ip == ip && ep.port== port; }

        std::string to_string()const { 
          std::stringstream ss; ss << boost::asio::ip::address_v4(ip).to_string()<<":"<<port;
          return ss.str();
        }
        uint32_t ip;
        uint16_t port;
      };

      peer( const scrypt::sha1& nid, const boost::filesystem::path& dir );
      ~peer();

      void init();
      void close();
      void sync();
      uint32_t count();

      bool fetch_by_endpoint( const peer::endpoint& ep, scrypt::sha1& id, record& m );
      bool fetch( const scrypt::sha1& id, record& m );
      bool store( const scrypt::sha1& id, const record& m );
      bool exists( const scrypt::sha1& id );

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

} }

#endif
