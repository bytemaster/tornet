#include <tornet/services/chat.hpp>
#include <tornet/error.hpp>


namespace tornet { namespace service {

  chat::chat( const node::ptr& n, uint16_t p )
  :m_node(n) {
    m_thread = boost::cmt::thread::create("chat");  
    m_node->start_service( p, "chat", boost::bind( &chat::add_channel, this, _1 ) );  
  }

  chat::~chat() {
    m_thread->quit();
  }
  
  void chat::add_channel( const channel& c ) {
    slog("");
    if( m_thread != &boost::cmt::thread::current() ) 
      m_thread->async<void>( boost::bind( &chat::add_channel, this, c ) ).wait();
    else {
      m_channels.push_back( c );
      m_channels.back().on_recv( boost::bind( &chat::on_recv, this, _1 ) );
    }
  }

  void chat::send( const std::string& txt ) {
    slog("");
    if( m_thread != &boost::cmt::thread::current() ) {
      m_thread->async<void>( boost::bind( &chat::send, this, txt ) ).wait();
    } else {
      tornet::buffer buf(txt);
      std::list<channel>::iterator itr = m_channels.begin();
      while( itr != m_channels.end() ) {
        itr->send( buf );    
        ++itr;
      }
    }
  }

  void chat::on_recv( const tornet::buffer& b ) {
    slog( "recv '%1%'", std::string(b.data(),b.size() ) );
  }

  /*
  void chat::read_channel( const channel& c ) {
    slog( "started read loop for channel %1%:%2%", c.remote_node(), c.remote_channel_num() );
    try {
        char buf[2048];
        while( size_t s = c.read_some( buf, sizeof(buf) ) ) {
          std::cerr<<"chat: "<<c.remote_node()<<":"<<c.remote_channel_num()<<" sent '"<<std::string(buf,s)<<"'\n";
        }
    } catch ( const boost::exception& e ) {
        wlog( "%1%", boost::diagnostic_information(e) );
    }
    // find channel in m_channels and remove it!
    slog( "exiting read loop for channel %1%:%2%", c.remote_node(), c.remote_channel_num() );
  }
  */



} } // tornet::service
