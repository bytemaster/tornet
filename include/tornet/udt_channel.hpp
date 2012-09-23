#ifndef _TORNET_UDT_CHANNEL_HPP_
#define _TORNET_UDT_CHANNEL_HPP_
#include <tornet/channel.hpp>
#include <fc/buffer.hpp>

namespace tn {

  /**
   *  Provides in-order, gauranteed delivery of streams of
   *  data using a protocol similar to UDT.  All processing is
   *  done in the node thread.  Calls to write/read are posted
   *  to the node thread to complete. 
   */
  class udt_channel {
    public:
      udt_channel();
      udt_channel( const channel& c, uint16_t max_window_packets = 4096 );
      udt_channel( const udt_channel& u );
      udt_channel( udt_channel&& u );
      ~udt_channel();

      void close();

      /**
       *  Blocks until all of @param b has been sent
       *
       *  Throws on error.
       *
       *  @return bytes read
       */
      size_t write( const fc::const_buffer& b );
      
      /**
       *  Blocks until all of @param b has been filled.
       *
       *  Throws on error.
       *
       *  @return bytes read
       */
      size_t read( const fc::mutable_buffer& b );
      size_t read( char* d, uint32_t s ) { return read( fc::mutable_buffer(d,s) ); }
      bool   get( char& c )              { return read( fc::mutable_buffer(&c,sizeof(c))); }
      bool   get( unsigned char& c )     { return read( fc::mutable_buffer((char*)&c,sizeof(c))); }

      fc::sha1 remote_node()const;
      uint8_t  remote_rank()const;

      udt_channel& operator=(udt_channel&& );
      udt_channel& operator=(const udt_channel& c);

      void flush() {} // todo implement buffered udt_channel

    private:
      fc::shared_ptr<class udt_channel_private> my;
  };

}

#endif
