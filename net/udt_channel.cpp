#include "udt_channel.hpp"
#include "detail/miss_list.hpp"
#include <boost/cmt/log/log.hpp>
#include <boost/bind.hpp>
#include <boost/rpc/datastream.hpp>
#include <boost/chrono.hpp>

namespace tornet {
  struct packet {
    enum types {
      data = 0,
      ack  = 1,
      nack = 2,
      ack2 = 3
    };
  };

  struct data_packet {
    uint8_t        flags;
    uint16_t       rx_win_start; // the seq of the rx window
    uint16_t       seq;
    tornet::buffer data;
  };

  struct ack_packet {
    uint8_t       flags;
    uint16_t      rx_win_start; // last data packet read
    uint16_t      rx_win_size;  // the size of the rx window
    uint16_t      rx_win_end;   // last packet received
    uint16_t      ack_seq;
    uint64_t      utc_time;
    miss_list     missed_seq;  // any missing seq between

    template<typename Stream>
    friend Stream& operator << ( Stream& s, const ack_packet& n ) {
      s.write( (char*)&n.flags,        sizeof(n.flags) );
      s.write( (char*)&n.rx_win_start, sizeof(n.rx_win_start) );
      s.write( (char*)&n.rx_win_size,  sizeof(n.rx_win_size) );
      s.write( (char*)&n.ack_seq,      sizeof(n.ack_seq) );
      s.write( (char*)&n.utc_time,     sizeof(n.utc_time) );
      s << n.missed_seq;
      return s;
    }
    template<typename Stream>
    friend Stream& operator >> ( Stream& s, ack_packet& n ) {
      s.read( (char*)&n.flags,        sizeof(n.flags) );
      s.read( (char*)&n.rx_win_start, sizeof(n.rx_win_start) );
      s.read( (char*)&n.rx_win_size,  sizeof(n.rx_win_size) );
      s.read( (char*)&n.ack_seq,      sizeof(n.ack_seq) );
      s.read( (char*)&n.utc_time,     sizeof(n.utc_time) );
      s >> n.missed_seq;
      return s;
    }
  };

  struct nack_packet {
    uint8_t   flags;
    uint16_t  rx_win_start;
    uint16_t  start_seq;
    uint16_t  end_seq;

    template<typename Stream>
    friend Stream& operator << ( Stream& s, const nack_packet& n ) {
      s.write( (char*)&n.flags,        sizeof(n.flags) );
      s.write( (char*)&n.rx_win_start, sizeof(n.rx_win_start) );
      s.write( (char*)&n.start_seq,    sizeof(n.start_seq) );
      s.write( (char*)&n.end_seq,      sizeof(n.end_seq) );
      return s;
    }
    template<typename Stream>
    friend Stream& operator >> ( Stream& s, nack_packet& n ) {
      s.read( (char*)&n.flags,        sizeof(n.flags) );
      s.read( (char*)&n.rx_win_start, sizeof(n.rx_win_start) );
      s.read( (char*)&n.start_seq,    sizeof(n.start_seq) );
      s.read( (char*)&n.end_seq,      sizeof(n.end_seq) );
      return s;
    }

  };

  class udt_channel_private {
    public:
      uint16_t               last_rx_seq;    // last rx seq  (received from sender)
      ack_packet             rx_ack_pack;

      typedef std::list<data_packet> dp_list;
      dp_list rx_win;
      dp_list tx_win;

      channel                chan;

      udt_channel_private( const channel& c, uint16_t mwp )
      :chan(c) {
        rx_ack_pack.rx_win_size = mwp;
        chan.on_recv( boost::bind(&udt_channel_private::on_recv, this, _1, _2 ) );
      }
      
      ~udt_channel_private() {
        chan.close();
      }

      void close() {
        chan.close();
        rx_win.clear();
        tx_win.clear();
        rx_ack_pack.missed_seq.clear();
      }
      
      // called from node thread
      void on_recv( const tornet::buffer& b, channel::error_code ec  ) {
         if( ec ) {
             slog( "channel closed!" );
             close();
             return;
         }
         switch( b[0] ) {
           case packet::data: handle_data(b); return;
           case packet::ack:  handle_ack(b);  return;
           case packet::nack: handle_nack(b); return;
           case packet::ack2: handle_ack2(b); return;
           default:
             elog( "Unknown packet type" );
             return;
         }
      }

      void handle_data( const tornet::buffer& b ) {
        boost::rpc::datastream<const char*> ds(b.data(),b.size());

        data_packet dp;
        ds >> dp.flags >> dp.seq >> dp.rx_win_start;
        dp.data = b.subbuf(5);

        advance_tx( dp.rx_win_start );

        if( dp.seq == (last_rx_seq+1) ) { // most common case
            rx_win.push_back(dp);
            last_rx_seq = dp.seq;
        } else if( dp.seq > (last_rx_seq+1) ) { // dropped some TODO: Handle Wrap
            if( dp.seq > (rx_ack_pack.rx_win_start+rx_ack_pack.rx_win_size) ) {
                wlog( "Window not big enough for this packet: %1%", dp.seq );
                return;
            } else {
               rx_win.push_back(dp); 
               uint16_t sr = last_rx_seq+1;
               last_rx_seq = dp.seq;
               rx_ack_pack.missed_seq.add(sr, dp.seq -1);
               
               // immidately notify sender of the loss
               send_nack( sr, dp.seq-1 );
            }
        } if( dp.seq <= rx_ack_pack.rx_win_start ) { // TODO: Handle Wrap
            wlog( "already received %1% ignoring", dp.seq );
            return;
        } else {
            // insert the packet into the rx win
            dp_list::iterator i = rx_win.begin();
            dp_list::iterator e = rx_win.end();
            while( i != e && i->seq < dp.seq ) { ++i; }
            if( i != e && i->seq == dp.seq ) {
               wlog( "duplicate packet, ignoring" );
               return;
            }
            rx_win.insert(i,dp);
            rx_ack_pack.missed_seq.remove( dp.seq );
        }


        if( rx_win.size() && rx_win.front().seq == (rx_ack_pack.rx_win_start) ) {
            // TODO: Notify read loop if read pending!
        }
      }

      void handle_ack( const tornet::buffer& b ) {
         ack_packet ap;
         boost::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> ap;

         advance_tx( ap.rx_win_start );

         dp_list::iterator i = tx_win.begin();
         dp_list::iterator e = tx_win.end();
         while( i != e && i->seq <= ap.rx_win_end ) {
           if( !ap.missed_seq.contains(i->seq) )  {
             i = tx_win.erase(i);
           }
         }
         // TODO Notify write loop if we advanced the start pos!
         
         // TODO Potentially send ack2 if write buf is empty and
         // nothing is waiting
      }
      void advance_tx( uint16_t rx_win_start ) {
         while( tx_win.size() && tx_win.front().seq < rx_win_start )
           tx_win.pop_front();
      }

      void handle_nack( const tornet::buffer& b ) {
         nack_packet np;
         boost::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> np;
         advance_tx( np.rx_win_start );         
      }

      void handle_ack2( const tornet::buffer& b ) {
        // stop sending 10hz acks
        // update rtt
      }

      void send_nack( uint16_t st_seq, uint16_t end_seq ) {
        nack_packet np;
        np.flags = packet::nack;
        np.rx_win_start = rx_ack_pack.rx_win_start+1;
        np.start_seq = st_seq;
        np.end_seq   = end_seq;

        tornet::buffer b;
        boost::rpc::datastream<char*> ds(b.data(),b.size());
        ds << np;
        b.resize(ds.tellp());

        chan.send(b);
      }

      void send_ack() {
        using namespace boost::chrono;
        rx_ack_pack.ack_seq++;
        rx_ack_pack.utc_time = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

        tornet::buffer b;
        boost::rpc::datastream<char*> ds(b.data(),b.size());
        ds << rx_ack_pack;
        b.resize(ds.tellp());
        chan.send(b);
      }
  };



  udt_channel::udt_channel( const channel& c, uint16_t rx_win_size ) {
    my = new udt_channel_private( c, rx_win_size );
  }

  udt_channel::~udt_channel() {
    delete my;
  }


  size_t udt_channel::read( const boost::asio::mutable_buffer& b ) {

    return 0;
  }

  size_t udt_channel::write( const boost::asio::const_buffer& b ) {

    return 0;
  }

  void udt_channel::close() {
    my->close();
  }

} // tornet
