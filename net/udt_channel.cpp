#include "udt_channel.hpp"
#include "detail/miss_list.hpp"
#include <boost/cmt/log/log.hpp>
#include <boost/bind.hpp>
#include <boost/rpc/datastream.hpp>

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
      uint16_t               last_read_seq;  // last read seq (pulled from rx window)
      uint16_t               last_rx_seq;    // last rx seq  (received from sender)

      typedef std::list<data_packet> dp_list;
      dp_list rx_win;
      dp_list tx_win;

      channel                chan;
      uint16_t               max_win;

      miss_list              m_miss_list;

      udt_channel_private( const channel& c, uint16_t mwp )
      :chan(c),max_win(mwp) {
        chan.on_recv( boost::bind(&udt_channel_private::on_recv, this, _1, _2 ) );
      }
      
      ~udt_channel_private() {
        chan.close();
      }

      void close() {
        chan.close();
        rx_win.clear();
        tx_win.clear();
        m_miss_list.clear();
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

        while( tx_win.size() && tx_win.front().seq <= dp.rx_win_start ) { // TODO: Wrap!
          tx_win.pop_front();
          // TODO: Notify senders of more space!
        }

        if( dp.seq <= last_read_seq ) { // TODO: Handle Wrap
          wlog( "already received %1% ignoring", dp.seq );
          return;
        } else if( dp.seq == last_rx_seq+1 ) { // most common case
          rx_win.push_back(dp);
          last_rx_seq = dp.seq;
        } else if( dp.seq > (last_rx_seq+1) ) { // dropped some
           if( dp.seq > (last_read_seq+max_win) ) {
               wlog( "Window not big enough for this packet: %1%", dp.seq );
               return;
           } else {
              rx_win.push_back(dp); 
              uint16_t sr = last_rx_seq+1;
              last_rx_seq = dp.seq;
              m_miss_list.add(sr, dp.seq -1);
              
              // immidately notify sender of the loss
              send_nack( sr, dp.seq-1 );
           }
        } else {
            // insert the packet into the rx win
            dp_list::iterator i = rx_win.begin();
            dp_list::iterator e = rx_win.end();
            while( i != e && i->seq <= dp.seq ) { ++i; }
            if( i != e && i->seq == dp.seq ) {
               wlog( "duplicate packet, ignoring" );
               return;
            }
            rx_win.insert(i,dp);
            m_miss_list.remove( dp.seq );
        }
        if( dp.seq == (last_read_seq+1) ) {
          // TODO: Notify read loop if read pending!
        }

      }


      void handle_ack( const tornet::buffer& b ) {

      }

      void handle_nack( const tornet::buffer& b ) {

      }

      void handle_ack2( const tornet::buffer& b ) {

      }

      void send_nack( uint16_t st_seq, uint16_t end_seq ) {

      }

      void send_ack() {

      }



  };






  udt_channel::udt_channel( const channel& c, uint16_t max_window_packets ) {
    my = new udt_channel_private( c, max_window_packets ); 
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
