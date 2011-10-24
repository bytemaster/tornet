#include "udt_channel.hpp"
#include "detail/miss_list.hpp"
#include <boost/cmt/log/log.hpp>
#include <boost/bind.hpp>
#include <boost/rpc/datastream.hpp>
#include <boost/chrono.hpp>
#include <boost/cmt/signals.hpp>

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
    data_packet():flags(packet::data){}
    data_packet( const tornet::buffer& b )
    :data(b),flags(packet::data){}
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
      uint16_t               next_tx_seq;
      uint16_t               remote_rx_win;  // the maximum amount the remote host can receive
      uint16_t               tx_win_size;    // our max tx window...varies with network
      boost::signal<void()>  tx_win_avail;   // trx buffer can take new inputs
      boost::signal<void()>  rx_win_avail;   // data ready to be read

      ack_packet             rx_ack_pack;
      ack_packet             tx_ack2_pack;

      typedef std::list<data_packet> dp_list;
      dp_list rx_win;
      dp_list tx_win;

      channel                chan;

      udt_channel_private( const channel& c, uint16_t mwp )
      :chan(c) {
        rx_ack_pack.rx_win_size = mwp;
        tx_win_size             = 1;
        remote_rx_win           = 1;
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

        data_packet dp(b.subbuf(5));
        ds >> dp.flags >> dp.seq >> dp.rx_win_start;

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
        if( rx_win.size() && dp.seq == (rx_ack_pack.rx_win_start) ) {
            rx_win_avail();
        }
      }

      void handle_ack( const tornet::buffer& b ) {
         ack_packet ap;
         boost::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> ap;

         remote_rx_win = ap.rx_win_size;

         // increase tx window incrementally
         if( remote_rx_win > tx_win_size ) 
            ++tx_win_size;

         advance_tx( ap.rx_win_start );

         dp_list::iterator i = tx_win.begin();
         dp_list::iterator e = tx_win.end();
         while( i != e && i->seq <= ap.rx_win_end ) {
           if( !ap.missed_seq.contains(i->seq) )  {
             tx_ack2_pack.missed_seq.remove(i->seq);
             i = tx_win.erase(i);
           }
         }

         tx_ack2_pack.flags        = packet::ack2;
         tx_ack2_pack.rx_win_start = next_tx_seq-1;
         tx_ack2_pack.utc_time     = ap.utc_time;
         tx_ack2_pack.ack_seq      = ap.ack_seq;
        
         // send ack2 if our tx buffer is not full
         if( tx_win.size() != tx_win_size ) {
            using namespace boost::chrono;
            tornet::buffer b;
            boost::rpc::datastream<char*> ds(b.data(),b.size());
            ds << tx_ack2_pack;
            b.resize(ds.tellp());
            send(b);
         }
      }
      void advance_tx( uint16_t rx_win_start ) {
         while( tx_win.size() && tx_win.front().seq < rx_win_start ) {
           tx_ack2_pack.missed_seq.remove(tx_win.front().seq);
           tx_win.pop_front();
         }

         // Notify write loop if we advanced the start pos!
         if( tx_win.size() < tx_win_size ) 
            tx_win_avail();
      }

      void handle_nack( const tornet::buffer& b ) {
         nack_packet np;
         boost::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> np;
         advance_tx( np.rx_win_start );         
         
         // TODO: Make Random Decrease
         // TODO: Only decrease once every SYN period (.1 sec)
         // TODO: Update Inter Packet Period to control sending rate
         tx_win_size *= .75;   
         if( tx_win_size == 0 ) tx_win_size = 1;

         dp_list::iterator i = tx_win.begin();
         dp_list::iterator e = tx_win.end();
         while( i != e && i->seq != np.start_seq ) {++i;}
         while( i != e && i->seq != (np.end_seq+1)) {
            send( i->data.subbuf( -5 ) );
            ++i;
         }
      }

      void handle_ack2( const tornet::buffer& b ) {
        // stop sending 10hz acks
        // update rtt
        // TODO: if the miss list contains packets we have not
        // received... add them to the rx_ack_pack and send
        // a NACK
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

        send(b);
      }

      void send_ack() {
        using namespace boost::chrono;
        rx_ack_pack.ack_seq++;
        rx_ack_pack.utc_time = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

        tornet::buffer b;
        boost::rpc::datastream<char*> ds(b.data(),b.size());
        ds << rx_ack_pack;
        b.resize(ds.tellp());
        send(b);
      }
    
     /**
     *  You can only send at the average inter-packet-rate. 
     *  Retransmissions will count against the inter-packet-rate.  
     */
      void send( const tornet::buffer& b ) {
       // TODO check Inter Packet Time... usleep if we are sending
       // too quickly!
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
    if( &boost::cmt::thread::current() != my->chan.get_thread() ) {
      return my->chan.get_thread()->async<size_t>( boost::bind( &udt_channel::read, this, b ) ).wait();
    }
   
    char*       data = boost::asio::buffer_cast<char*>(b);
    uint32_t    len  = boost::asio::buffer_size(b);

    while( len ) {
      while( !my->rx_win.size() || my->rx_win.front().seq != my->rx_ack_pack.rx_win_start ) {
        boost::cmt::wait( my->rx_win_avail );
      }
      data_packet& dp = my->rx_win.front();
      uint32_t clen = (std::min)(size_t(len),size_t(dp.data.size()));
      memcpy( data, dp.data.data(), clen );

      data += clen;
      len  -= clen;

      dp.data.move_start(clen);
      if( dp.data.size() == 0 ) {
          my->rx_ack_pack.rx_win_start++;
          my->rx_win.pop_front();
      }
    }

    return boost::asio::buffer_size(b);
  }

  /**
   *  This method will block until all of the contents of @param b have been sent. 
   */
  size_t udt_channel::write( const boost::asio::const_buffer& b ) {
    if( &boost::cmt::thread::current() != my->chan.get_thread() ) {
      return my->chan.get_thread()->async<size_t>( boost::bind( &udt_channel::write, this, b ) ).wait();
    }
    /*
     *  You can only send if the tx window is not full, otherwise you must wait.
     */

    const char* data = boost::asio::buffer_cast<const char*>(b);
    uint32_t    len  = boost::asio::buffer_size(b);

    while( len ) {
       tornet::buffer  pbuf;
       data_packet     dp( pbuf.subbuf( 5 ) );
       dp.flags        = packet::data;
       dp.rx_win_start = my->rx_ack_pack.rx_win_start;
       dp.seq          = my->next_tx_seq++;
       int  plen = (std::min)(uint32_t(len),uint32_t(1200));
       dp.data.resize(plen);
       memcpy( dp.data.data(), data, plen );
       data += plen;
       len  -= plen;

       pbuf[0] = packet::data;
       memcpy(pbuf.data()+1, &dp.rx_win_start, sizeof(dp.rx_win_start) );
       memcpy(pbuf.data()+3, &dp.seq,          sizeof(dp.seq) );

       // it is possible for other senders (retrans) to wake up 
       // first and steal our slot, so we must check again
       while( my->tx_win.size() >= my->tx_win_size ) {
          slog( "tx win full... wait for ack..." );
          boost::cmt::wait( my->tx_win_avail );
       }
       my->tx_ack2_pack.missed_seq.add(dp.seq,dp.seq);
       my->tx_win.push_back(dp);
       my->send(pbuf);
    }
    return boost::asio::buffer_size(b);
  }

  void udt_channel::close() {
    my->close();
  }

} // tornet
