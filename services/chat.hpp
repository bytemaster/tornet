#ifndef _TORNET_CHAT_HPP_
#define _TORNET_CHAT_HPP_
#include <tornet/net/node.hpp>
#include <boost/cmt/thread.hpp>

namespace tornet { namespace service {

  /**
   *  Opens the chat port on a node and accepts new
   *  chat connections.  Listens to std::cin and sends
   *  a message to all connected channels.  Displays
   *  messages from all channels.
   *
   */
  class chat {
    public:
      typedef boost::shared_ptr<chat> ptr;

      chat( const node::ptr& n, uint16_t chat_port = 100 );
      ~chat();

      void send( const std::string& text );

      void add_channel( const channel& c );
    private:
      void on_recv( const tornet::buffer& b );

      node::ptr           m_node;
      boost::cmt::thread* m_thread;
      std::list<channel>  m_channels;
  };

} }

#endif
