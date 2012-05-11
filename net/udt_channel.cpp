#include "udt_channel.hpp"
#include "detail/miss_list.hpp"
#include <boost/cmt/log/log.hpp>
#include <boost/bind.hpp>
#include <tornet/rpc/datastream.hpp>
#include <boost/chrono.hpp>
#include <boost/cmt/signals.hpp>

namespace tornet {
  typedef sequence::number<uint16_t> seq_num;
  using namespace boost::chrono;
  
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
    uint8_t          flags;
    seq_num          rx_win_start; // the seq of the rx window
    seq_num          seq;
    tornet::buffer   data;
    seq_num          last_sent_ack_seq;
  };

  struct ack_packet {
    ack_packet():flags(packet::ack){}
    uint8_t    flags;
    seq_num    rx_win_start; // last data packet read (by user?)
    uint16_t   rx_win_size;  // the size of the rx window 
    seq_num    rx_win_end;   // last packet received (less than win_start+win_end?)
    seq_num    ack_seq;
    uint64_t   utc_time;
    miss_list  missed_seq;  // any missing seq between

    template<typename Stream>
    friend Stream& operator << ( Stream& s, const ack_packet& n ) {
      s.write( (char*)&n.flags,        sizeof(n.flags) );
      s.write( (char*)&n.rx_win_start, sizeof(n.rx_win_start) );
      s.write( (char*)&n.rx_win_size,  sizeof(n.rx_win_size) );
      s.write( (char*)&n.rx_win_end,  sizeof(n.rx_win_end) );
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
      s.read( (char*)&n.rx_win_end,   sizeof(n.rx_win_end) );
      s.read( (char*)&n.ack_seq,      sizeof(n.ack_seq) );
      s.read( (char*)&n.utc_time,     sizeof(n.utc_time) );
      s >> n.missed_seq;
      return s;
    }
  };

  struct nack_packet {
    uint8_t   flags;
    seq_num   rx_win_start;
    seq_num   start_seq;
    seq_num   end_seq;

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
   //   seq_num               last_rx_seq;    // last rx seq  (received from sender)
      seq_num                next_tx_seq;
      uint16_t               remote_rx_win;  // the maximum amount the remote host can receive
      uint16_t               tx_win_size;    // our max tx window...varies with network
      boost::signal<void()>  tx_win_avail;   // trx buffer can take new inputs
      boost::signal<void()>  rx_win_avail;   // data ready to be read

      miss_list              tx_miss_list;   // packets that have been nacked

      ack_packet             last_rx_ack;    // last received ack

      ack_packet             rx_ack_pack;
      ack_packet             tx_ack2_pack;
      ack_packet             rx_ack2_pack;

      bool                      start_up;
      bool                      dec_on_nack;
      bool                      started_retran;
      bool                      retransmitting;


      bool                      syn_timer_running;
      bool                      m_stop_syn_timer;
      system_clock::time_point  next_syn_time;

      typedef std::list<data_packet> dp_list;
      dp_list rx_win;
      dp_list tx_win;

      channel                chan;

      udt_channel_private( const channel& c, uint16_t mwp )
      :chan(c),syn_timer_running(false),next_tx_seq(0) {
        start_up                  = true;
        dec_on_nack               = true;
        started_retran            = false;
        retransmitting            = false;
                                  
        last_rx_ack.rx_win_start  = 1;
        last_rx_ack.rx_win_end    = 0;
        last_rx_ack.rx_win_size   = 1;



        rx_ack_pack.flags         = packet::ack;
        rx_ack_pack.rx_win_start  = 1;
        rx_ack_pack.rx_win_end    = 0;
        rx_ack_pack.rx_win_size   = mwp;
        tx_ack2_pack.rx_win_start = 1;
        tx_win_size               = 1;
        remote_rx_win             = 1;
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
      bool can_send() {
        // tx_ack2_pack.rx_win_start the last known start of remote recv window.
        //return next_tx_seq < (tx_ack2_pack.rx_win_start + tx_win_size);
        return next_tx_seq < (last_rx_ack.rx_win_start + tx_win_size);
      }
      void stop_syn_timer() {
     //   slog( "stoping syn timer" );
        m_stop_syn_timer = true;
      }

      void start_syn_timer() {
        m_stop_syn_timer = false;
        if( !syn_timer_running ) {
     //     slog( "starting syn timer" );
          next_syn_time = system_clock::now() + milliseconds(100);
          chan.get_thread()->schedule( boost::bind(&udt_channel_private::on_syn,this),next_syn_time);
          syn_timer_running = true;
        }
      }
      void retransmit( bool do_it = false ) {
        if( !do_it ) {
            if( !retransmitting ) {
              if( !started_retran ) { 
                //elog( "\n\nstart retransmit!\n\n" );
                started_retran = true; 
                boost::cmt::thread::current().async<void>( boost::bind( &udt_channel_private::retransmit, this, true ), boost::cmt::priority(2) );
                return;
              } else
                  return;
            } else { slog( "already retan!" ); return; }
            return;
        }
        retransmitting = true;
        started_retran = false;
        //elog( "\n\nretransmitting!\n\n" );
        seq_num sq;
        while( tx_miss_list.pop_front(sq) ) {
         //   elog( "retransmitting       %1%", sq.value() );
          
           dp_list::iterator i = tx_win.begin();
           dp_list::iterator e = tx_win.end();
           while( i != e && i->seq != sq ) {++i;}
           if( i != e && i->seq == sq ) {
          //    elog( "       retransmit %1%", sq.value() );
              if( i->last_sent_ack_seq + 2 < tx_ack2_pack.ack_seq ) {
                  i->last_sent_ack_seq = tx_ack2_pack.ack_seq+1;
                  send( i->data.subbuf( -5 ) );
              }
           } else {
              elog( "unable to retransmit packet %1%, not in tx queue", sq.value() );
              exit(1);
           }
        }
       // elog( "done retransmitting!" );
        retransmitting = false;
      }


      void on_syn() {
         // slog("tx_win_size: %1%  next_tx_seq: %2%  remote_rx_win_start: %3%  remote_rx_win_size: %4%", tx_win_size, (uint32_t)next_tx_seq, (uint32_t)last_rx_ack.rx_win_start, uint32_t(last_rx_ack.rx_win_size));
          send_ack();
          if( !m_stop_syn_timer ) {
             next_syn_time += milliseconds(100);
             chan.get_thread()->schedule( boost::bind(&udt_channel_private::on_syn,this),next_syn_time);
          } else { syn_timer_running = false; m_stop_syn_timer = false; }
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
             elog( "Unknown packet type (%1%)", int(b[0]) );
             return;
         }
      }

      void handle_data( const tornet::buffer& b ) {
        start_syn_timer();
        tornet::rpc::datastream<const char*> ds(b.data(),b.size());

        data_packet dp(b.subbuf(5));
        ds >> dp.flags >> dp.rx_win_start >> dp.seq;

        //slog( "seq %1%  rx win %2%   len %3% rx window: %4%->%5% ", std::string(dp.seq), dp.rx_win_start.value(), dp.data.size(), rx_ack_pack.rx_win_start.value(), rx_ack_pack.rx_win_end.value() );

        advance_tx( dp.rx_win_start );

        rx_ack_pack.missed_seq.remove( dp.seq );
        if( dp.seq == seq_num(rx_ack_pack.rx_win_end+1) ) { // most common case
            if( dp.seq > (rx_ack_pack.rx_win_start+rx_ack_pack.rx_win_size) ) {
                // THIS SHOULD NOT HAPPEN, it means transmitter sent too much
                elog( "Window not big enough for this packet: %1%,  start %2%  size %3%", dp.seq.value(), rx_ack_pack.rx_win_start.value(), rx_ack_pack.rx_win_size );
                return;
             }
            rx_win.push_back(dp);
            rx_ack_pack.rx_win_end = dp.seq;
        } else if( dp.seq > seq_num(rx_ack_pack.rx_win_end+1) ) { // dropped some 
            if( dp.seq > (rx_ack_pack.rx_win_start+rx_ack_pack.rx_win_size) ) {
                // THIS SHOULD NOT HAPPEN, it means transmitter sent too much
                elog( "Window not big enough for this packet: %1%,  start %2%  size %3%", 
                       dp.seq.value(), rx_ack_pack.rx_win_start.value(), rx_ack_pack.rx_win_size );
                return;
            } else {
               rx_win.push_back(dp); 
               seq_num sr = rx_ack_pack.rx_win_end+1;
               rx_ack_pack.rx_win_end = dp.seq;
               rx_ack_pack.missed_seq.add(sr, dp.seq -1);
               
               // imidately notify sender of the loss
               send_nack( sr, dp.seq-1 );
            }
        } else if( dp.seq < rx_ack_pack.rx_win_start ) { 
            wlog( "already received %1%, before rx_win-start %2% ignoring", dp.seq.value(), rx_ack_pack.rx_win_start.value() );
            return;
        } else {
            // insert the packet into the rx win
            dp_list::iterator i = rx_win.begin();
            dp_list::iterator e = rx_win.end();
            while( i != e && i->seq < dp.seq ) {  ++i; }
            if( i != e && i->seq == dp.seq ) {
               wlog( "duplicate packet %1%, ignoring", dp.seq.value() );
               return;
            }
            rx_win.insert(i,dp);
            i = rx_win.begin();
            e = rx_win.end();

            rx_ack_pack.missed_seq.remove( dp.seq );
        }
        if( rx_win.size() && dp.seq == (rx_ack_pack.rx_win_start) ) {
            //elog( "-----------------------  rx win avail  dp.seq %1%   rx_ack_pack.rx_win_start %2%", dp.seq.value(), rx_ack_pack.rx_win_start.value() );
            rx_win_avail();
        }
      }

      void handle_ack( const tornet::buffer& b ) {
         dec_on_nack              = true;
         ack_packet ap;
         tornet::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> ap;

         tx_miss_list    = ap.missed_seq;

         bool could_send = can_send();

         //slog( "this: %4%,  remote rx window [ %1% -> %2% of %3% ] could send: %5%", 
         //           ap.rx_win_start.value(), ap.rx_win_end.value(), ap.rx_win_size, this, could_send );

         //slog( "missing %1%", ap.missed_seq.size() );
        //  slog( "this: %5% next_tx_seq %1%   rx_win_start %2%  rx_win_end %3% missing %4%", next_tx_seq.value(), 
        //        tx_ack2_pack.rx_win_start.value(), 
        //        (tx_ack2_pack.rx_win_start + tx_win_size).value(), 
        //        ap.missed_seq.size(), this );

         if( ap.missed_seq.size() )
             retransmit();
       //  ap.missed_seq.print();
         remote_rx_win = ap.rx_win_size;

         // increase tx window incrementally
         if( start_up ) { 
            tx_win_size = (std::min)(uint16_t(tx_win_size*1.5 + 1), remote_rx_win );
         }
         else if( remote_rx_win > tx_win_size ) 
            ++tx_win_size;

         if( ap.ack_seq >= last_rx_ack.ack_seq )
            last_rx_ack = ap;

         // clear the tx buffer up to rx_win_start, notify that
         // there is room to transmit more data.
         advance_tx( ap.rx_win_start );

         //slog( "this: %1%, can send: %2%  next_tx_seq %3%", this, can_send(), (uint32_t)next_tx_seq );

          /*
         dp_list::iterator i = tx_win.begin();
         dp_list::iterator e = tx_win.end();
         while( i != e && i->seq <= ap.rx_win_end ) {
           if( !ap.missed_seq.contains(i->seq) )  {
             tx_ack2_pack.missed_seq.remove(i->seq);
             i = tx_win.erase(i);
             continue;
           }
           ++i;
         }
         */

         tx_ack2_pack.flags        = packet::ack2;
         tx_ack2_pack.rx_win_start = next_tx_seq; 
          // let the remote host know the last data we sent
          // so that it can detect a dropped packet... and nack
         tx_ack2_pack.utc_time     = ap.utc_time;
         tx_ack2_pack.ack_seq      = ap.ack_seq;
        
         // send ack2 if our tx buffer is not full
         if( could_send ) {
            //slog( "sending ack2 rx_win_start %1% ack_seq %2%", 
            //       (uint32_t)next_tx_seq, (uint32_t)ap.ack_seq );
            using namespace boost::chrono;
            tornet::buffer b;
            tornet::rpc::datastream<char*> ds(b.data(),b.size());
            ds << tx_ack2_pack;
            b.resize(ds.tellp());
            send(b);
         } else {
            //slog( "Not sending ack2, tx buffer is full" );
         }
      }

      /**
       * Remove everything from this misslist before rx_win_start
       */
      void advance_tx( uint16_t rx_win_start ) {
         tx_ack2_pack.rx_win_start = rx_win_start;
         while( tx_win.begin()!=tx_win.end() && tx_win.front().seq < rx_win_start ) {
           tx_ack2_pack.missed_seq.remove(tx_win.front().seq);
           tx_win.pop_front();
         }

         // Notify write loop if we advanced the start pos!
         // if( tx_win.size() < tx_win_size ) 
         if( can_send() ) {
            //slog( "tx_win_avail!" );
            tx_win_avail();
         } else {
          //slog( "cannot send!" );
         }
      }

      void handle_nack( const tornet::buffer& b ) {
         nack_packet np;
         tornet::rpc::datastream<const char*> ds(b.data(),b.size());
         ds >> np;
         advance_tx( np.rx_win_start );         

         elog( "nack win start %1%  dropped %2% -> %3%", 
            np.rx_win_start.value(), np.start_seq.value(), np.end_seq.value() );
         
         // TODO: Make Random Decrease
         // TODO: Only decrease once every SYN period (.1 sec)
         // TODO: Update Inter Packet Period to control sending rate
         if( dec_on_nack ) {
             elog( "Scale back by 10%" );
             dec_on_nack = false;
             if( start_up ) 
                tx_win_size *= .75;
             else
                 tx_win_size *= .90;   
             start_up    = false;
             if( tx_win_size == 0 ) tx_win_size = 1;
         }
         tx_miss_list.add( np.start_seq, np.end_seq );
         retransmit();
      }

      uint64_t utc_now_us() {
        return duration_cast<microseconds>(
                  system_clock::now().time_since_epoch()).count();
      }

      void handle_ack2( const tornet::buffer& b ) {
        tornet::rpc::datastream<const char*> ds(b.data(), b.size() );
        ds >> rx_ack2_pack;
        uint64_t utc_now = utc_now_us();
        //slog( "RTT: %3%  rx_ack2_pack.rx_win_start %1%  next_tx_seq %2%",
        //      (uint16_t)rx_ack2_pack.rx_win_start, (uint16_t)next_tx_seq , utc_now - rx_ack2_pack.utc_time );
        // TODO: update rtt with weighted avg

        // stop sending 10hz acks
        stop_syn_timer();

        // TODO: if the miss list contains packets we have not
        // received... add them to the rx_ack_pack and send
        // a NACK  
      }

      void send_nack( seq_num st_seq, seq_num end_seq ) {
        nack_packet np;
        np.flags = packet::nack;
        np.rx_win_start = rx_ack_pack.rx_win_start;
        np.start_seq = st_seq;
        np.end_seq   = end_seq;

        tornet::buffer b;
        tornet::rpc::datastream<char*> ds(b.data(),b.size());
        ds << np;
        b.resize(ds.tellp());
        //wlog( "send nack %1% -> %2%  rx_win_start %3%", st_seq.value(), end_seq.value(), np.rx_win_start.value() );
        send(b);
      }

      void send_ack() {
        using namespace boost::chrono;
        rx_ack_pack.ack_seq++;
        rx_ack_pack.utc_time = utc_now_us();

        tornet::buffer b;
        tornet::rpc::datastream<char*> ds(b.data(),b.size());
        ds << rx_ack_pack;
        b.resize(ds.tellp());

    //    slog( "send ack  ack_seq: %1%    rx_win_start %2%  rx_win_end %3%", rx_ack_pack.ack_seq.value(),
    //                rx_ack_pack.rx_win_start.value(), rx_ack_pack.rx_win_end.value() );
        send(b);
      }
    
     /**
     *  You can only send at the average inter-packet-rate. 
     *  Retransmissions will count against the inter-packet-rate.  
     */
      void send( const tornet::buffer& b ) {
       // TODO check Inter Packet Time... usleep if we are sending
       // too quickly!
       //  slog( "send %1%", b.size() );
        chan.send(b);
      }
  };



  udt_channel::udt_channel( const channel& c, uint16_t rx_win_size )
  :my(new udt_channel_private( c, rx_win_size ) ) {
  }

  udt_channel::~udt_channel() {
  }


  size_t udt_channel::read( const boost::asio::mutable_buffer& b ) {
    if( &boost::cmt::thread::current() != my->chan.get_thread() ) {
      return my->chan.get_thread()->async<size_t>( boost::bind( &udt_channel::read, this, b ) ).wait();
    }
   
    char*       data = boost::asio::buffer_cast<char*>(b);
    uint32_t    len  = boost::asio::buffer_size(b);

    while( len ) {
      while( !my->rx_win.size() || my->rx_win.front().seq != my->rx_ack_pack.rx_win_start ) {
//        slog( "waiting for data!  %1% != %2%", my->rx_win.front().seq.value(),  my->rx_ack_pack.rx_win_start.value() );
        boost::cmt::wait( my->rx_win_avail );
//        slog( "data avail!" );
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
//    slog( "Start write %1% bytes", len );

    while( len ) {
       tornet::buffer  pbuf;
       data_packet     dp( pbuf.subbuf( 5 ) );
       dp.flags        = packet::data;
       dp.rx_win_start = my->rx_ack_pack.rx_win_start;
       dp.seq          = ++my->next_tx_seq;
       dp.last_sent_ack_seq = my->tx_ack2_pack.ack_seq - 5;
       int  plen = (std::min)(uint32_t(len),uint32_t(1200));
       dp.data.resize(plen);
       memcpy( dp.data.data(), data, plen );
       data += plen;
       len  -= plen;

       pbuf[0] = packet::data;
       memcpy(pbuf.data()+1, &dp.rx_win_start, sizeof(dp.rx_win_start) );
       memcpy(pbuf.data()+3, &dp.seq,          sizeof(dp.seq) );
      
       pbuf.resize( 5 + plen );

       // it is possible for other senders (retrans) to wake up 
       // first and steal our slot, so we must check again
       while( !my->can_send() ) {
//          wlog( "tx win full... wait for ack...  tx_win.used: %1%  tx_win size %2%  ", my->tx_win.size(), my->tx_win_size );
          //slog( "next_tx_seq %1%   rx_win_start %2%  rx_win_end %3%", my->next_tx_seq.value(), 
          //      my->tx_ack2_pack.rx_win_start.value(), (my->tx_ack2_pack.rx_win_start + my->tx_win_size).value() );
          boost::cmt::wait( my->tx_win_avail );
//          wlog( "Done waiting... tx_win_avail!" );
       }
       my->tx_ack2_pack.missed_seq.add(dp.seq,dp.seq);
       my->tx_win.push_back(dp);

//       slog( "send seq %1%  size: %2% ", dp.seq.value(), dp.data.size() );
       my->send(pbuf);
    }
//    slog( "Wrote %1%", boost::asio::buffer_size(b) );
    return boost::asio::buffer_size(b);
  }


  void udt_channel::close() {
    my->close();
  }


   scrypt::sha1 udt_channel::remote_node()const {
    return my->chan.remote_node(); 
   }
   uint8_t             udt_channel::remote_rank()const {
    return my->chan.remote_rank();
   }

} // tornet
