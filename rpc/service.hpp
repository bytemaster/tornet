#ifndef _TORNET_RPC_SERVICE_HPP_
#define _TORNET_RPC_SERVICE_HPP_
#include <tornet/rpc/connection.hpp>
#include <boost/cmt/thread.hpp>
#include <tornet/net/node.hpp>
#include <boost/any.hpp>
#include <boost/reflect/any_ptr.hpp>
#include <boost/fusion/support/deduce_sequence.hpp>
#include <json/value_io.hpp>

namespace tornet { namespace rpc {


  template<typename Seq, typename Functor>
  struct rpc_recv_functor {
    rpc_recv_functor( Functor f, tornet::rpc::connection&, const char* )
    :m_func(f){}
    std::vector<char> operator()( const std::vector<char>& param ) {
      Seq paramv;
      slog( "param size: %1%", param.size() );
      raw::unpack_vec( param, paramv );
      //slog( "parm json: %1%", json::io::to_json( paramv ) );
      std::vector<char> rtn;
      raw::pack_vec( rtn, m_func(paramv) );
      return rtn;
    }
    Functor m_func;
  };
  

  /**
   *  A services listens for new incoming connections and
   *  spawns a service rpc::connection that implements the RPC
   *  api.  The service rpc::connection has a pointer back to
   *  the service for 'shared state' and the service connection
   *  keeps the per-connection state.
   *
   *  The service will listen on two ports, one UDP and the other UDT.  
   *  The UDT port is always one above the UDP port.
   */
  class service {
    public:
      typedef boost::shared_ptr<service> ptr;

      service( const tornet::node::ptr& node, const std::string& name, uint16_t udp_port,
              boost::cmt::thread* t = &boost::cmt::thread::current() );
      ~service();

      boost::cmt::thread* get_thread()const;
      const tornet::node::ptr& get_node()const;

    protected:
      virtual boost::any init_connection( const rpc::connection::ptr& con );


      template<typename InterfaceType>
      struct visitor {
        visitor( rpc::connection& c, boost::reflect::any_ptr<InterfaceType>& s, uint16_t& mid )
        :m_con(c),m_aptr(s),m_mid(mid){}
      
        template<typename Member, typename VTable, Member VTable::*m>
        void operator()(const char* name )const  {
             typedef typename boost::fusion::traits::deduce_sequence<typename Member::fused_params>::type param_type;
             m_con.add_method( m_mid, rpc_recv_functor<param_type, Member&>( (*m_aptr).*m, m_con, name ) );
             ++m_mid;
        }
        rpc::connection&                        m_con;
        uint16_t&                               m_mid;
        boost::reflect::any_ptr<InterfaceType>& m_aptr;
      };



    private:
      friend class service_private;
      class service_private* my;

  };


} } // tornet::rpc

#endif
