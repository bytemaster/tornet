#include <tornet/buffer.hpp>
#include <boost/array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/string.hpp>
#include <fc/exception.hpp>
#include <string.h>


typedef boost::array<char,2048> buffer_data;

namespace tn {
    buffer::~buffer(){}

    class buffer::impl {
      public:
        template<typename T>
        impl( T&& t ):ptr( fc::forward<T>(t) ){}
        boost::shared_ptr<buffer_data> ptr;
    };

    buffer::buffer()
    :shared_data( boost::make_shared<buffer_data>() ){
        start = shared_data->ptr->c_array(); 
        len   = shared_data->ptr->size();
    }
    buffer::buffer( const fc::string& d ) 
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( d.size() <= sizeof(buffer_data) );
        start = shared_data->ptr->c_array(); 
        memcpy( start, d.c_str(), d.size() );
        len   = d.size();
    }
    
    buffer::buffer( uint32_t len )
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( len <= sizeof(buffer_data) );
        start = shared_data->ptr->c_array(); 
        len   = len;
    }

    buffer::buffer( const char* d, uint32_t dl )
    :shared_data( boost::make_shared<buffer_data>() ){
        BOOST_ASSERT( dl <= sizeof(buffer_data) );
        start = shared_data->ptr->c_array(); 
        memcpy( start, d, dl );
        len   = dl;
    }

    buffer::buffer( const buffer& b )
    :start(b.start),len(b.len),shared_data(b.shared_data){}

    buffer buffer::subbuf( int32_t s, uint32_t l )const {
      buffer b(*this);
      b.start += s;
      if( l == uint32_t(-1) ) 
        b.len -= s;
      else
        b.len = l;

      BOOST_ASSERT( b.start >= b.shared_data->ptr->c_array() );
      BOOST_ASSERT( b.start + b.len <= b.shared_data->ptr->c_array() + b.shared_data->ptr->size() );
      return b;
    }
    void buffer::move_start( int32_t sdif ) {
      start += sdif;
      if( sdif > len ) len = 0;
      else len -= sdif;
      assert( start >= shared_data->ptr->c_array() );
      assert( start <= shared_data->ptr->c_array() + shared_data->ptr->size() );
    }
    void buffer::resize( uint32_t s ) {
      if( s <= len ) 
        len = s;
      else 
        FC_THROW( "Attempt to grow buffer!" );
    }
} // namespace tn
