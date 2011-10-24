#ifndef _TORNET_RPC_CONNECTION_HPP_
#define _TORNET_RPC_CONNECTION_HPP_
#include <tornet/rpc/message.hpp>
#include <boost/function.hpp>
#include <vector>
#include <tornet/net/channel.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/rpc/raw.hpp>
#include <boost/signals.hpp>

namespace tornet { namespace rpc {
  typedef boost::function<std::vector<char>( const std::vector<char>& param )> rpc_method;

  /**
   *  Manages RPC call state including:
   *    - sending invokes, setting return codes, and handling promises
   *    - receiving invokes, calling methods, and sending return codes.
   */
  class connection {
    public:
      typedef boost::shared_ptr<connection> ptr;
      typedef boost::weak_ptr<connection>   wptr;

      connection( const tornet::channel& c, boost::cmt::thread* t = &boost::cmt::thread::current()  );
      ~connection();

      void add_method( uint16_t mid, const rpc_method& m );

      template<typename R, typename ParamSeq>
      boost::cmt::future<R> call( uint16_t mid, const ParamSeq& param ) {
        message msg;
        msg.id         = next_method_id();
        msg.type       = message::call;
        msg.method_id  = mid;
        boost::rpc::raw::pack_vec( msg.data, param );

        typename pending_result_impl<R>::ptr pr = boost::make_shared<pending_result_impl<R> >(); 
        send( msg, boost::static_pointer_cast<pending_result>(pr) );
        return pr->prom;
      }

      boost::signal<void()> closed;

      class pending_result {
        public:
          typedef boost::shared_ptr<pending_result> ptr;
          virtual ~pending_result(){}
          virtual void handle_result( const std::vector<char>& data ) = 0;
          virtual void handle_error( const boost::exception_ptr& e  ) = 0;
      };
    private:
      uint16_t next_method_id();

      void send( const tornet::rpc::message& msg, 
                 const connection::pending_result::ptr& pr = connection::pending_result::ptr());


      template<typename R> 
      class pending_result_impl : public pending_result {
        public:
          pending_result_impl():prom(new boost::cmt::promise<R>()){}
          ~pending_result_impl() {
            if( !prom->ready() ) {
              prom->set_exception( boost::copy_exception( boost::cmt::error::broken_promise() ));
            }
          }
          typedef boost::shared_ptr<pending_result_impl> ptr;
          virtual void handle_result( const std::vector<char>& data ) {
            R value;
            boost::rpc::raw::unpack_vec( data, value );
            prom->set_value( value );
          }
          virtual void handle_error( const boost::exception_ptr& e  ) {
            prom->set_exception(e);
          }
          typename boost::cmt::promise<R>::ptr prom;
      };
      class connection_private* my;
  };

} }

#endif
