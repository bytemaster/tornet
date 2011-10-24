#ifndef _TORNET_BUFFER_HPP_
#define _TORNET_BUFFER_HPP_
#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <tornet/error.hpp>

namespace tornet {

  typedef boost::array<char,2048> buffer_data;
  typedef boost::shared_ptr<buffer_data> shared_data_ptr;

  /**
   *  To prevent copying data around, the buffer object
   *  maintains a shared pointer to a larger packet structure. 
   */
  struct buffer {
    buffer()
    :shared_data( boost::make_shared<buffer_data>() ){
        start = shared_data->c_array(); 
        len   = shared_data->size();
    }
    buffer( const std::string& d ) 
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( d.size() <= sizeof(buffer_data) );
        start = shared_data->c_array(); 
        memcpy( start, d.c_str(), d.size() );
        len   = d.size();
    }
    
    buffer( uint32_t len )
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( len <= sizeof(buffer_data) );
        start = shared_data->c_array(); 
        len   = len;
    }

    buffer( const char* d, uint32_t dl )
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( dl <= sizeof(buffer_data) );
        start = shared_data->c_array(); 
        memcpy( start, d, dl );
        len   = dl;
    }

    buffer( const buffer& b )
    :start(b.start),len(b.len),shared_data(b.shared_data){}

    buffer subbuf( int32_t s, uint32_t l = -1 )const {
      buffer b(*this);
      b.start = start + s;
      assert( b.start >= shared_data->c_array() );
      if( len == -1 ) {
          assert( s < len );
          b.len   = len-s;
      }
      else
          assert( s+l < len );
      return b;
    }
    void move_start( int32_t sdif ) {
      start += sdif;
      if( sdif > len ) len = 0;
      else len -= sdif;
      assert( start >= shared_data->c_array() );
      assert( start <= shared_data->c_array() + shared_data->size() );
    }
    void resize( uint32_t s ) {
      if( s <= len ) 
        len = s;
      else 
        TORNET_THROW( "Attempt to grow buffer!" );
    }
    const char& operator[](int i)const { return start[i]; }
    char&       operator[](int i)      { return start[i]; }

    const char* data()const { return start;  }
    char* data()            {  return start; }
    size_t size()const      { return len;    }

    private:
      char*    start;
      uint32_t len;
      boost::shared_ptr<buffer_data> shared_data;
  };

} // namespace tornet

#endif 
