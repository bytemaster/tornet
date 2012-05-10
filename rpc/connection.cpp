#include <tornet/rpc/connection.hpp>
#include <boost/unordered_map.hpp>
#include <json/value.hpp>
#include <json/value_io.hpp>


namespace tornet { namespace rpc {
  typedef std::map<uint16_t,rpc_method>                                  method_map;

  class connection_private {
    public:
      typedef boost::unordered_map<uint16_t,connection::pending_result::ptr> pending_result_map;

      connection_private( rpc::connection& s, boost::cmt::thread* t )
      :self(s), m_thread(t), next_method_id(0) { }

      rpc::connection&     self;
      method_map           methods;
      pending_result_map   pending_results;
      boost::cmt::thread*  m_thread;
      uint16_t             next_method_id;

      void break_promises() {
        pending_result_map::iterator itr = pending_results.begin();
        while( itr != pending_results.end() ) {
            itr->second->handle_error( boost::copy_exception(boost::cmt::error::broken_promise()) );
            ++itr;
        }
      }
  };

  connection::connection( boost::cmt::thread* t ) {
    my = new connection_private(*this, t);
  }

  connection::~connection() {
    slog( "%1%", this );
    delete my;
  }

  void connection::break_promises() {
    my->break_promises();
  }


  // called by some thread... 
  void connection::send( const tornet::rpc::message& msg, 
                         const connection::pending_result::ptr& pr  ) {
    if( &boost::cmt::thread::current() != my->m_thread ) {
      my->m_thread->async<void>( 
          boost::bind( &connection::send, this, boost::cref(msg), pr ) ).wait();
      return;
    }
    send(msg); 
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


  uint16_t connection::next_method_id()             { return ++my->next_method_id; }
  boost::cmt::thread* connection::get_thread()const { return my->m_thread; }

  void connection::handle_call( const tornet::rpc::message& m ) {
    message reply;
    reply.id = m.id;
    method_map::iterator itr = my->methods.find(m.method_id);
    if( itr != my->methods.end()  ) {
      try {
    //      slog( "call method %1% with data size %2%,  %3%", m.method_id, m.data.size(), scrypt::to_hex( &m.data.front(),  std::min(int(m.data.size()), int(8)) ) );
          reply.data = itr->second(m.data);
          reply.type = message::result;
      } catch ( const boost::exception& e ) {
          tornet::rpc::raw::pack_vec( reply.data, std::string(boost::diagnostic_information(e)) );
          reply.type = message::error;
      } catch ( const std::exception& e ) {
          tornet::rpc::raw::pack_vec( reply.data, std::string(boost::diagnostic_information(e)) );
          reply.type = message::error;
      }
    } else {
          tornet::rpc::raw::pack_vec( reply.data, std::string("Invalid Method ID") );
          reply.type = message::error;
    }
    send( reply );
  }
  void connection::handle_notice( const tornet::rpc::message& m ) {
    method_map::iterator itr = my->methods.find(m.method_id);
    if( itr != my->methods.end()  ) {
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
  void connection::handle_result( const tornet::rpc::message& m ) {
    connection_private::pending_result_map::iterator itr = my->pending_results.find(m.id);
    if( itr != my->pending_results.end() ) {
      itr->second->handle_result(m.data);
      my->pending_results.erase(itr);
    } else {
      wlog( "Unexpected Result Message" );
    }
  }
  void connection::handle_error( const tornet::rpc::message& m ) {
    connection_private::pending_result_map::iterator itr = my->pending_results.find(m.id);
    if( itr != my->pending_results.end() ) {
       std::string emsg;
       tornet::rpc::raw::unpack_vec(m.data,emsg);
       itr->second->handle_error( boost::copy_exception( tornet_exception() << err_msg(emsg) ));
       my->pending_results.erase(itr);
    } else {
      wlog( "Unexpected Error Message" );
    }
  }


} } // namespace tornet::rpc
