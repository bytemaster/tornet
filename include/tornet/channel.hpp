#ifndef _TORNET_CHANNEL_HPP_
#define _TORNET_CHANNEL_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/sha1.hpp>
#include <tornet/buffer.hpp>
#include <fc/function.hpp>


namespace tn { 
  class node;
  class connection;

  /**
   *  @class channel
   *
   *  Manages a stream of communication between two nodes.  Multiple
   *  channels are multi-plexed over a single encrypted connection between
   *  two nodes. Message order and delivery are not gauranteed as everything
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
      enum error_code {
        ok     = 0,
        closed = 1
      };
      typedef fc::sha1                                         node_id;
      typedef fc::function<void(const tn::buffer&,error_code)> recv_handler;

      channel();
      channel( channel&& c );
      channel( const channel& c );
      ~channel();

      channel& operator=(const channel& c );
      bool operator==(const channel& c )const;
      operator bool()const;

      node_id  remote_node()const;
      uint8_t  remote_rank()const;
      uint16_t local_channel_num()const;
      uint16_t remote_channel_num()const;

      /// after calling this the receive handler will no longer be called
      void     close();
      void     send( const tn::buffer& buf );
      void     on_recv( const recv_handler& cb );

      node&    get_node()const;

    private:
      friend class node; // the only one with permission to create channels
      channel( connection* c, uint16_t r, uint16_t l );

      friend class connection;
      // called from connection when a packet comes it, 
      // this method will call the method provided to on_recv() if any.
      void recv( const tn::buffer& b, error_code ec = channel::ok );
      void reset();

      class impl;
      fc::shared_ptr<impl> my;
  };

} // namespace tn

#endif // _TORNET_CHANNEL_HPP_
