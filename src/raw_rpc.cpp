#include <tornet/raw_rpc.hpp>
#include <tornet/udt_channel.hpp>
#include <boost/unordered_map.hpp>
#include <fc/exception.hpp>
#include <fc/error.hpp>
#include <fc/thread.hpp>
#include <fc/fwd_impl.hpp>

FC_REFLECT( tn::rpc_message, (id)(type)(method)(data) )

namespace tn { 
    class raw_rpc::impl {
      public:
        impl( raw_rpc& s ):_self(s){}

        void recv( rpc_message&& m );
        void read_loop();
        raw_rpc&         _self;
        promise_base*    _pending_head;
        promise_base*    _pending_tail;
        udt_channel      _chan;
        fc::future<void> _read_loop_done;

        boost::unordered_map<uint32_t, method_base::ptr> _methods;

        promise_base* pop_promise( uint16_t id ) {
          promise_base* prev = nullptr;
          promise_base* cur  = _pending_head;
          while( cur ) {
            if( cur->_req_id == id ) {
              if( prev ) {
                prev->_next = cur->_next;
              } else {
                 _pending_head = cur->_next;
              }
              if( cur == _pending_tail ) {
                _pending_tail = prev;
              }
              cur->_next = nullptr;
              return cur;
            }
            prev = cur;
            cur  = cur->_next;
          }
          return nullptr;
        }
    };

    raw_rpc::~raw_rpc() {
      try {
          my->_chan.close();
          if( my->_read_loop_done.valid() ) {
            my->_read_loop_done.cancel();
            slog( "wait for read loop" );
            my->_read_loop_done.wait();
            slog( "... done waiting for read loop" );
          }
      } catch ( ... ) {}
      while( my->_pending_head ) {
          my->_pending_head->set_exception( fc::copy_exception( fc::task_canceled() ) );
          auto n = my->_pending_head->_next;
          my->_pending_head->release();
          my->_pending_head = n;
      }
    }

    raw_rpc::raw_rpc()
    :my(*this) {
      _req_id = 0;
      my->_pending_tail = nullptr;
      my->_pending_head = nullptr;
    }

    void raw_rpc::enqueue_promise( promise_base* r ) {
      r->retain();
      r->_next = 0;
      if( my->_pending_tail ) {
        my->_pending_tail->_next = r;
        my->_pending_tail = r;
      }
      else {
        my->_pending_head = my->_pending_tail = r;
      }
    }

    void raw_rpc::add_method( uint32_t id, method_base::ptr&& m ) {
      my->_methods[id] = fc::move(m);
    }
    

    void raw_rpc::send( rpc_message&& m ) {
      auto dat = fc::raw::pack( m );
      // TODO: implement buffered channel to eliminate
      // extra copy to dat...
      my->_chan.write( fc::const_buffer(dat.data(), dat.size()) );
      my->_chan.flush();
    }

    void raw_rpc::connect( const udt_channel& c ) {
      // TODO: ensure we are not already connected..
      my->_chan = c;
      my->_read_loop_done = fc::async( [&]() { my->read_loop(); }, "raw_rpc::read_loop" ); 
    }

    void raw_rpc::impl::read_loop() {
      try {
      while( true ) {
        rpc_message m;
        fc::raw::unpack( _chan, m );
      //  wlog( "message id %d  method %d", m.id, m.method );

        switch( m.type ) {
          case tn::rpc_message::result: {
            promise_base::ptr p( pop_promise( m.id ) );
            if( p ) p->handle_result( m.data );
            else {
              wlog( "Unexpected reply %d", m.id );
            }
          }  break;
          case rpc_message::error: {
            promise_base::ptr p( pop_promise( m.id ) );
            if( !p ) wlog( "Unexpected reply %d", m.id );

          }  break;
          case rpc_message::notice: {
            rpc_message reply;
            reply.type   = rpc_message::result;
            reply.id     = m.id;
            reply.method = m.method;
            auto itr = _methods.find( m.method );
            if( itr != _methods.end() ) 
              itr->second->call( m.data );
          }  break;
          case rpc_message::call: {
            rpc_message reply;
            reply.type   = rpc_message::result;
            reply.id     = m.id;
            reply.method = m.method;
            auto itr = _methods.find( m.method );
            if( itr != _methods.end() ) {
              reply.data = itr->second->call( m.data );
            } else {
              wlog( "Unknown method id %d", m.method );
            }
            _self.send( fc::move(reply) );
          }  break;
        }
      }
      } catch ( ... ) {
        wlog( "Exit read loop with error %s", fc::current_exception().diagnostic_information().c_str() );
      }
    }
} // namespace tn
