#ifndef _TORNET_RPC_UDT_CONNECTION_HPP_
#define _TORNET_RPC_UDT_CONNECTION_HPP_
#include <tornet/rpc/connection.hpp>
#include <tornet/net/udt_channel.hpp>

namespace tornet { namespace rpc {

class udt_connection : public tornet::rpc::connection {
  public:
      typedef boost::shared_ptr<udt_connection> ptr;
      typedef boost::weak_ptr<udt_connection>   wptr;

      /**
       *  @param t - the thread in which messages will be sent and callbacks invoked
       */
      udt_connection( const udt_channel& c, 
                      boost::cmt::thread* t = &boost::cmt::thread::current() );

      virtual ~udt_connection();

      virtual uint8_t      remote_rank()const;
      virtual scrypt::sha1 remote_node()const;


  protected:
      friend class udt_connection_private;
      virtual void send( const tornet::rpc::message& msg );

      class udt_connection_private* my;
};

} } // namespace tornet::rpc

#endif //_TORNET_RPC_UDT_CONNECTION_HPP_
