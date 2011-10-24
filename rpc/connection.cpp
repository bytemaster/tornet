#include <tornet/rpc/connection.hpp>
#include <boost/unordered_map.hpp>
#include <boost/rpc/json/value_io.hpp>


namespace tornet { namespace rpc {
  typedef boost::unordered_map<uint16_t,connection::pending_result::ptr> pending_result_map;
  typedef std::map<uint16_t,rpc_method>                                  method_map;

  class connection_private {
    public:
      connection_private( rpc::connection& s, const tornet::channel c, boost::cmt::thread* t )
      :self(s), chan(c),m_thread(t),next_method_id(0) {
        chan.on_recv( boost::bind(&connection_private::on_recv, this, _1, _2 ) );
      }
      ~connection_private() {
        slog( " wiii " );
        chan.close();
      }

      // called from node thread...
      void on_recv( const tornet::buffer& b, channel::error_code ec  ) {
        if( &boost::cmt::thread::current() != m_thread ) {
          m_thread->async( boost::bind( &connection_private::on_recv, this, b, ec ) );
          return;
        }
        slog( "%1% bytes  ec %2%", b.size(), int(ec) );

        if( !ec ) {
          boost::rpc::datastream<const char*> ds(b.data(),b.size());
          tornet::rpc::message msg;
          boost::rpc::raw::unpack(ds,msg);
        
        
          slog("Recv: %1%", boost::rpc::json::to_string(msg));

          switch( msg.type ) {
            case message::notice: handle_notice(msg); return;
            case message::call:   handle_call(msg); return;
            case message::result: handle_result(msg); return;
            case message::error:  handle_error(msg); return;
            default: 
              wlog( "invalid message type %1%", int( msg.type ) );
          };
        } else {
          wlog( "RPC Session Closed" );
          break_promises();
          self.closed();
          // break promises
        }
      }

      void handle_call( const tornet::rpc::message& m ) {
        message reply;
        reply.id = m.id;
        method_map::iterator itr = methods.find(m.method_id);
        if( itr != methods.end()  ) {
          try {
              reply.data = itr->second(m.data);
              reply.type = message::result;
          } catch ( const boost::exception& e ) {
              boost::rpc::raw::pack_vec( reply.data, std::string(boost::diagnostic_information(e)) );
              reply.type = message::error;
          } catch ( const std::exception& e ) {
              boost::rpc::raw::pack_vec( reply.data, std::string(boost::diagnostic_information(e)) );
              reply.type = message::error;
          }
        } else {
              boost::rpc::raw::pack_vec( reply.data, std::string("Invalid Method ID") );
              reply.type = message::error;
        }
        send( reply );
      }
      void handle_notice( const tornet::rpc::message& m ) {
        method_map::iterator itr = methods.find(m.method_id);
        if( itr != methods.end()  ) {
          try {
              itr->second(m.data);
          } catch ( const boost::exception& e ) {
            wlog( "%1%", boost::diagnostic_information(e) );
          } catch ( const std::exception& e ) {
            wlog( "%1%", boost::diagnostic_information(e) );
          } catch ( ... ) {
            wlog( "Unhandled exception calling method %1%", m.method_id );
          }
        } else {
            wlog( "Invalid method %1%", m.method_id );
        }
      }
      void handle_result( const tornet::rpc::message& m ) {
        pending_result_map::iterator itr = pending_results.find(m.id);
        if( itr != pending_results.end() ) {
          itr->second->handle_result(m.data);
          pending_results.erase(itr);
        } else {
          wlog( "Unexpected Result Message" );
        }
      }
      void handle_error( const tornet::rpc::message& m ) {
        pending_result_map::iterator itr = pending_results.find(m.id);
        if( itr != pending_results.end() ) {
           std::string emsg;
           boost::rpc::raw::unpack_vec(m.data,emsg);
           itr->second->handle_error( boost::copy_exception( tornet_exception() << err_msg(emsg) ));
           pending_results.erase(itr);
        } else {
          wlog( "Unexpected Error Message" );
        }
      }

      void break_promises() {
        pending_result_map::iterator itr = pending_results.begin();
        while( itr != pending_results.end() ) {
            itr->second->handle_error( boost::copy_exception(boost::cmt::error::broken_promise()) );
            ++itr;
        }
      }

      void send( const tornet::rpc::message& msg ) {
        tornet::buffer b;
        boost::rpc::datastream<char*> ds(b.data(),b.size());
        boost::rpc::raw::pack( ds, msg );
        b.resize( ds.tellp() ); 
        slog( "rpc con send %1%", b.size() );
        chan.send( b ); // posts to node thread if necessary
      }


      rpc::connection&     self;
      method_map           methods;
      pending_result_map   pending_results;
      tornet::channel      chan;
      boost::cmt::thread*  m_thread;
      uint16_t             next_method_id;
  };

  connection::connection( const tornet::channel& c, boost::cmt::thread* t ) {
    my = new connection_private(*this, c, t);
  }

  connection::~connection() {
    slog( "%1%", this );
    delete my;
  }

  // called by some thread... 
  void connection::send( const tornet::rpc::message& msg, 
                         const connection::pending_result::ptr& pr  ) {

    if( &boost::cmt::thread::current() != my->m_thread ) {
      my->m_thread->async<void>( 
          boost::bind( &connection::send, this, boost::cref(msg), pr ) ).wait();
      return;
    }
    my->send(msg); 
    if( pr ) 
        my->pending_results[msg.id] = pr;
  }

  // what thread??? 
  void connection::add_method( uint16_t mid, const rpc_method& m ) {
    if( &boost::cmt::thread::current() != my->m_thread ) {
      my->m_thread->async( boost::bind( &connection::add_method, this, mid, m ) );
    } else {
      my->methods[mid] = m;
    }
  }


  uint16_t connection::next_method_id() { return ++my->next_method_id; }


} } // namespace tornet::rpc
