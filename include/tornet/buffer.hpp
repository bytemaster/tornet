#ifndef _TORNET_BUFFER_HPP_
#define _TORNET_BUFFER_HPP_
#include <fc/utility.hpp>
#include <fc/fwd.hpp>

namespace fc { class string; }

namespace tn {
  /**
   *  To prevent copying data around, the buffer object
   *  maintains a shared pointer to a larger packet structure. 
   */
  struct buffer {
    buffer();
    buffer( const fc::string& d );
    buffer( uint32_t len );
    buffer( const char* d, uint32_t dl );
    buffer( const buffer& b );
    ~buffer();

    buffer subbuf( int32_t s, uint32_t l = -1 )const;

    void move_start( int32_t sdif );
    void resize( uint32_t s );

    const char& operator[](int i)const { return start[i]; }
    char&       operator[](int i)      { return start[i]; }

    const char* data()const { return start;  }
    char*       data()      {  return start; }
    size_t      size()const { return len;    }

    private:
      char*              start;
      uint32_t           len;

      class impl;
      fc::fwd<impl,16>   shared_data;
  };

} // namespace tornet

#endif 
