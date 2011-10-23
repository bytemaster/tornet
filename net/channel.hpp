#ifndef _TORNET_CHANNEL_HPP_
#define _TORNET_CHANNEL_HPP_
#include <boost/shared_ptr.hpp>
#include <scrypt/sha1.hpp>
#include <tornet/net/buffer.hpp>
#include <boost/function.hpp>

namespace tornet { 
  namespace detail {
      class connection;
      class channel_private;
      class node_private;
  }

  /**
   *  @class channel
   *
   *  Manages a stream of communication between two nodes.  Multiple
   *  channels are multi-plexed over a single encrypted connection between
   *  two nodes. Message order and deliver are not gauranteed as everything
   *  is sent over UDP.  If you want gauranteeed delivery then wrap the
   *  channel with a udt_channel which implements the UDT protocol. 
   *
   *  Channels are asynchronous and data is received with
   *  via a callback which will be called by the node's thread. Your message
   *  handler should not block because it will disrupt all other datastreams. If
   *  your service is unable to keep up with the incoming data then it should
   *  drop packets before blocking.
   *
   *  In this way there are minimal expectations on how services are implemented.
   *
   *  Some services could be RPC based, others stream based (radio/video), others
   *  could simply be a proxy service routing all received data to another
   *  node.
   *
   *  Channels are wrappers on a internal reference and therefore two copies 
   *  still refer to the same channel.  Calling close will invalidate the
   *  channel.
   */
  class channel {
    public:
      typedef scrypt::sha1                                 node_id;
      typedef boost::function<void(const tornet::buffer&)> recv_handler;

      channel();
      ~channel();

      operator bool()const { return my; }

      node_id  remote_node()const;
      uint16_t local_channel_num()const;
      uint16_t remote_channel_num()const;

      void     close();
      void     send( const tornet::buffer& buf );
      void     on_recv( const recv_handler& cb );

    private:
      friend class detail::node_private; // the only one with permission to create channels
      channel( const boost::shared_ptr<detail::connection>& c, uint16_t r, uint16_t l );

      friend class detail::connection;
      // called from connection when a packet comes it, 
      // this method will call the method provided to on_recv() if any.
      void recv( const tornet::buffer& b );

      boost::shared_ptr<detail::channel_private> my;
  };

};

#endif // _TORNET_CHANNEL_HPP_
