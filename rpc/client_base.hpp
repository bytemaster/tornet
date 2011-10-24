#ifndef  _TORNET_RPC_CLIENT_BASE_HPP
#define  _TORNET_RPC_CLIENT_BASE_HPP
#include <tornet/rpc/connection.hpp>

namespace tornet { namespace rpc {

class client_base {
  public:
    client_base( const tornet::rpc::connection::ptr& c )
    :m_con(c){}

    template<typename R, typename ParamSeq>
    boost::cmt::future<R> call( uint16_t mid, const ParamSeq& param ) {
      return m_con->call<R,ParamSeq>( mid, param );
    }

  protected:
      tornet::rpc::connection::ptr m_con;
};

}} // namespace tornet::rpc 
#endif //  _TORNET_RPC_CLIENT_BASE_HPP
