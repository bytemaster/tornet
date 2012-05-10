#ifndef _TORNET_RPC_UDP_CONNECTION_HPP_
#define _TORNET_RPC_UDP_CONNECTION_HPP_
#include <tornet/rpc/connection.hpp>

namespace tornet { namespace rpc {

  /**
   *  Implements state-less RPC without support for large packets or
   *  retransmission at the stream level.
   */
  class udp_connection : public tornet::rpc::connection {
    public:
      typedef boost::shared_ptr<udp_connection> ptr;
      typedef boost::weak_ptr<udp_connection>   wptr;

      /**
       *  @param t - the thread in which messages will be sent and callbacks invoked
       */
      udp_connection( const tornet::channel& c, 
                      boost::cmt::thread* t = & boost::cmt::thread::current() ); 
      virtual ~udp_connection();

      virtual uint8_t      remote_rank()const;
      virtual scrypt::sha1 remote_node()const;

    protected:
      virtual void send( const tornet::rpc::message& msg );

    private:
      friend class udp_connection_private;
      class udp_connection_private* my;
  };

} } // namespace tornet::rpc


#endif // _TORNET_RPC_UDP_CONNECTION_HPP_

