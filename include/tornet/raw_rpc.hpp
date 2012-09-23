#ifndef _TORNET_RAW_RPC_HPP_
#define _TORNET_RAW_RPC_HPP_
#include <fc/raw.hpp>
#include <fc/fwd.hpp>
#include <fc/future.hpp>
#include <fc/static_reflect.hpp>

namespace tn {
  class udt_channel;

  struct rpc_message {
    enum types {
      notice = 1,
      call   = 2,
      result = 3,
      error  = 4
    };
    uint16_t          id;   ///< Return Code / Reference Number
    uint8_t           type; 
    fc::unsigned_int  method;
    fc::vector<char>  data;
  };

  class promise_base : virtual public fc::promise_base {
    public:
      typedef fc::shared_ptr<promise_base> ptr;
      promise_base( uint16_t id, const fc::time_point& s )
      :_req_id(id),_req_start(s){}

      virtual void handle_result( const fc::vector<char>& data ) = 0;

      uint16_t        _req_id;
      fc::time_point  _req_start;
      promise_base*   _next;
  };

  template<typename Result>
  class promise : virtual public promise_base, virtual public fc::promise<Result> {
    public:
      promise( uint16_t rid, const fc::time_point& start = fc::time_point::now() )
      :promise_base( rid, start ){}

      virtual void handle_result( const fc::vector<char>& data ) {
         this->set_value( fc::raw::unpack<Result>( data.data(), data.size() ) );
      }
  };

  class method_base : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<method_base> ptr;
      virtual ~method_base(){}
      virtual fc::vector<char> call( const fc::vector<char>& arg_data ) = 0;
  };

  template<typename Arg, typename Functor>
  class method : virtual public method_base {
    public:

      template<typename F>
      method( F&& f ):_func( fc::forward<F>(f) ){} 

      virtual fc::vector<char> call( const fc::vector<char>& arg_data ) {
        auto result = _func( fc::raw::unpack<Arg>(arg_data) );
        return fc::raw::pack(result);
      }
    private:
      Functor _func;
  };

  class raw_rpc {
    public:
      raw_rpc();
      ~raw_rpc();

      template<typename Result, typename Arg>
      fc::future<Result> invoke( uint32_t method_id, const Arg& a ) {
        promise<Result>* r = new promise<Result>( ++_req_id, fc::time_point::now() );
        enqueue_promise( r );

        rpc_message m;
        m.type      = rpc_message::call;
        m.method    = method_id;
        m.id        = r->_req_id;
        m.data      = fc::raw::pack( a );

        send( fc::move(m) );
        return fc::future<Result>(r);
      }

      void connect( const udt_channel& c );

      template<typename Arg, typename Functor>
      void add_method( uint32_t method_id, Functor&& f ) {
          method_base::ptr m( new method<Arg,Functor>(f) );
          add_method(method_id, fc::move(m));
      }
  
    private:
      void enqueue_promise( promise_base* b );
      void add_method( uint32_t method_id, method_base::ptr&& m );
      void send( rpc_message&& m );

      class impl;
      fc::fwd<impl,72>  my;

      uint16_t         _req_id;
  };
}


#endif // _TORNET_RAW_RPC_HPP_
