#ifndef _TORNET_RPC_RAW_HPP_
#define _TORNET_RPC_RAW_HPP_
#include <fc/static_reflect.hpp>

#include <tornet/rpc/varint.hpp>
#include <tornet/rpc/required.hpp>
#include <tornet/rpc/errors.hpp>
#include <scrypt/base64.hpp>
#include <sstream>
#include <iostream>
#include <map>
#include <iomanip>

#include <boost/fusion/container/vector.hpp>
#include <boost/fusion/algorithm.hpp>
#include <boost/fusion/support/is_sequence.hpp>
#include <boost/fusion/sequence/intrinsic/size.hpp>
#include <tornet/rpc/datastream.hpp>
#include <fc/value.hpp>
#include <fc/json.hpp>

namespace tornet { namespace rpc { namespace raw {


    template<typename Stream, typename T>
    void unpack( Stream& s, boost::optional<T>& v );
    template<typename Stream, typename T>
    void pack( Stream& s, const boost::optional<T>& v );

    template<typename Stream>
    void unpack( Stream& s, fc::value& );
    template<typename Stream>
    void pack( Stream& s, const fc::value& );
    template<typename Stream>
    void unpack( Stream& s, std::string& );
    template<typename Stream>
    void pack( Stream& s, const std::string& );

    template<typename Stream> inline void unpack( Stream& s, std::string& v );

    template<typename Stream, typename T>
    inline void pack( Stream& s, const T& v );
    template<typename Stream, typename T>
    inline void unpack( Stream& s, T& v );

    template<typename Stream, typename T, template<typename> class Alloc, template<typename,typename> class Container>
    inline void pack( Stream& s, const Container<T,Alloc<T> >& value );
    template<typename Stream, typename T, template<typename> class Alloc, template<typename,typename> class Container>
    inline void unpack( Stream& s, Container<T,Alloc<T> >& value );


    template<typename Stream, typename T>
    void pack( Stream&, const std::set<T>& value );
    template<typename Stream, typename T>
    void unpack( Stream&, std::set<T>& value );


    template<typename Stream, typename Key, typename Value>
    void pack( Stream& s, const std::map<Key,Value>& value );
    template<typename Stream, typename Key, typename Value>
    void unpack( Stream& s, std::map<Key,Value>& value );

    template<typename Stream> inline void pack( Stream& s, const signed_int& v ) {
      uint32_t val = (v.value<<1) ^ (v.value>>31);
      do {
        uint8_t b = uint8_t(val) & 0x7f;
        val >>= 7;
        b |= ((val > 0) << 7);
        s.put(b);
      } while( val );
    }
    template<typename Stream> inline void pack( Stream& s, const unsigned_int& v ) {
      uint64_t val = v.value;
      do {
        uint8_t b = uint8_t(val) & 0x7f;
        val >>= 7;
        b |= ((val > 0) << 7);
        s.put(b);
      }while( val );
    }


    template<typename Stream> inline void pack( Stream& s, const bool& v ) { pack( s, uint8_t(v) );     }
    template<typename Stream> inline void pack( Stream& s, const char* v ) { pack( s, std::string(v) ); }



    template<typename Stream, typename T> 
    inline void pack( Stream& s, const required<T>& v ) { pack( s, *v); }

    template<typename Stream, typename T> 
    inline void pack( Stream& s, const boost::optional<T>& v ) {
      pack( s, bool(!!v) );
      if( !!v ) 
        pack( s, *v );
    }
    template<typename Stream> inline void pack( Stream& s, const std::vector<char>& value ) { 
      pack( s, unsigned_int(value.size()) );
      if( value.size() )
        s.write( &value.front(), value.size() );
    }
    template<typename Stream> inline void pack( Stream& s, const std::string& v )  {
      pack( s, unsigned_int(v.size()) );     
      if( v.size() )
        s.write( v.c_str(), v.size() );
    }
    template<typename Stream>
    void unpack( Stream& s, fc::value& v ) {
      std::string str;
      unpack(s, str);
      if( str.size() ) json::from_string( str, v );
    }

    template<typename Stream>
    void pack( Stream& s, const fc::value& v ) {
      pack( s, fc::json::to_string( v ) ); 
    }

    template<typename Stream> inline void unpack( Stream& s, signed_int& vi ) {
      uint32_t v = 0; char b = 0; int by = 0;
      do {
        s.get(b);
        v |= uint32_t(uint8_t(b) & 0x7f) << by;
        by += 7;
      } while( uint8_t(b) & 0x80 );
      vi.value = ((v>>1) ^ (v>>31)) + (v&0x01);
      vi.value = v&0x01 ? vi.value : -vi.value;
      vi.value = -vi.value;
    }
    template<typename Stream> inline void unpack( Stream& s, unsigned_int& vi ) {
      uint64_t v = 0; char b = 0; uint8_t by = 0;
      do {
          s.get(b);
          v |= uint32_t(uint8_t(b) & 0x7f) << by;
          by += 7;
      } while( uint8_t(b) & 0x80 );
      vi.value = v;
    }
    template<typename Stream> inline void unpack( Stream& s, bool& v ) {
      uint8_t b; 
      unpack( s, b ); 
      v=b;    
    }
    template<typename Stream> inline void unpack( Stream& s, std::vector<char>& value ) { 
      unsigned_int size; unpack( s, size );
      value.resize(size.value);
      if( value.size() )
        s.read( &value.front(), value.size() );
    }
    template<typename Stream> inline void unpack( Stream& s, std::string& v )  {
      std::vector<char> tmp; unpack(s,tmp);
      v = std::string(tmp.begin(),tmp.end());
    }

    template<typename Stream, typename T>
    void unpack( Stream& s, boost::optional<T>& v ) {
      bool b; unpack( s, b ); 
      if( b ) { v = T(); unpack( s, *v ); }
    }
    template<typename Stream, typename T>
    void unpack( const Stream& s, required<T>& v ) {
      v = T(); unpack( s, *v );
    }
    //template<typename Stream, typename T, template<typename> class Alloc, template<typename,typename> class Container>
    //void unpack( const Stream& s, Container<T,Alloc<T> >& value );

    template<typename Stream, typename Key, typename Value>
    void unpack( const Stream& s, std::map<Key,Value>& value );
    template<typename Stream, typename Key, typename Value>
    void unpack( const Stream& s, std::pair<Key,Value>& value );
    template<typename Stream, typename Value>
    void unpack( const Stream& s, std::map<std::string,Value>& val );

    namespace detail {
    
      template<typename Stream, typename Class>
      struct pack_object_visitor {
        pack_object_visitor(const Class& _c, Stream& _s)
        :c(_c),s(_s){}

        template<typename T, typename C, T(C::*p)>
        inline void operator()( const char* name )const {
          tornet::rpc::raw::pack( s, c.*p );
        }
        private:            
          const Class& c;
          Stream&      s;
      };

      template<typename Stream, typename Class>
      struct unpack_object_visitor {
        unpack_object_visitor(Class& _c, Stream& _s)
        :c(_c),s(_s){}

        template<typename T, typename C, T(C::*p)>
        inline void operator()( const char* name )const {
          tornet::rpc::raw::unpack( s, c.*p );
        }
        private:            
          Class&  c;
          Stream& s;
      };

      template<typename Stream>
      struct pack_sequence {
         pack_sequence( Stream& _s ):s(_s){}
         template<typename T>
         void operator() ( const T& v )const { tornet::rpc::raw::pack(s,v); }
         Stream&    s;
      };

      template<typename Stream>
      struct unpack_sequence {
         unpack_sequence( Stream& _s ):s(_s){}
         template<typename T>
         void operator() ( T& v )const { tornet::rpc::raw::unpack(s,v); }
         Stream&  s;
      };

      template<typename IsPod=boost::false_type>
      struct if_pod{
        template<typename Stream, typename T>
        static inline void pack( Stream& s, const T& v ) { 
          s << v;
        }
        template<typename Stream, typename T>
        static inline void unpack( Stream& s, T& v ) { 
          s >> v;
        }
      };

      template<>
      struct if_pod<boost::true_type> {
        template<typename Stream, typename T>
        static inline void pack( Stream& s, const T& v ) { 
          s.write( (char*)&v, sizeof(v) );   
        }
        template<typename Stream, typename T>
        static inline void unpack( Stream& s, T& v ) { 
          s.read( (char*)&v, sizeof(v) );   
        }
      };

      template<typename IsReflected=boost::false_type>
      struct if_reflected {
        template<typename Stream, typename T>
        static inline void pack( Stream& s, const T& v ) { 
          if_pod<typename boost::is_pod<T>::type>::pack(s,v);
        }
        template<typename Stream, typename T>
        static inline void unpack( Stream& s, T& v ) { 
          if_pod<typename boost::is_pod<T>::type>::unpack(s,v);
        }
      };
      template<>
      struct if_reflected<boost::true_type> {
        template<typename Stream, typename T>
        static inline void pack( Stream& s, const T& v ) { 
          fc::static_reflector<T>::visit( pack_object_visitor<Stream,T>( v, s ) );
        }
        template<typename Stream, typename T>
        static inline void unpack( Stream& s, T& v ) { 
          fc::static_reflector<T>::visit( unpack_object_visitor<Stream,T>( v, s ) );
        }
      };

      template<typename IsFusionSeq> 
      struct if_fusion_seq {
        template<typename Stream, typename T> 
        inline static void pack( Stream& s, const T& v ) {
          pack_sequence<Stream> pack_vector( s );
          boost::fusion::for_each( v, pack_vector );
        }
        template<typename Stream, typename T> 
        inline static void unpack( Stream& s, T& v ) {
          unpack_sequence<Stream> unpack_vector(s);
          boost::fusion::for_each( v, unpack_vector );
        }
      };

      template<> 
      struct if_fusion_seq<boost::mpl::false_> {
        template<typename Stream, typename T> 
        inline static void pack( Stream& s, const T& v ) {
          if_reflected<typename boost::static_reflector<T>::is_defined>::pack(s,v);
        }
        template<typename Stream, typename T> 
        inline static void unpack( Stream& s, T& v ) {
          if_reflected<typename boost::static_reflector<T>::is_defined>::unpack(s,v);
        }
      };
    } // namesapce detail


    template<typename Stream, typename T>
    inline void pack( Stream& s, const std::set<T>& value ) {
         pack( s, unsigned_int(value.size()) );
         typename std::set<T>::const_iterator itr = value.begin();
         typename std::set<T>::const_iterator end = value.end();
         while( itr != end ) {
           tornet::rpc::raw::pack( s, *itr );
           ++itr;
         }
    }

    template<typename Stream, typename T>
    inline void unpack( Stream& s, std::set<T>& value ) {
        unsigned_int ss;
        unpack( s, ss );
        for( uint32_t i = 0; i < ss.value; ++i ) {
            T v;
            unpack( s, v );
            value.insert(v);
        }
    }

    template<typename Stream, typename T, template<typename> class Alloc, template<typename,typename> class Container>
    inline void pack( Stream& s, const Container<T,Alloc<T> >& value ) {
      pack( s, unsigned_int(value.size()) );
      typename Container<T,Alloc<T> >::const_iterator itr = value.begin();
      typename Container<T,Alloc<T> >::const_iterator end = value.end();
      while( itr != end ) {
        tornet::rpc::raw::pack( s, *itr );
        ++itr;
      }
    }

    template<typename Stream, typename T, template<typename> class Alloc, template<typename,typename> class Container>
    inline void unpack( Stream& s, Container<T,Alloc<T> >& value ) {
      unsigned_int size; unpack( s, size );
      value.resize(size.value);
      typename Container<T,Alloc<T> >::iterator itr = value.begin();
      typename Container<T,Alloc<T> >::iterator end = value.end();
      while( itr != end ) {
        tornet::rpc::raw::unpack( s, *itr );
        ++itr;
      }
    }

    // support for pair!
    template<typename Stream, typename Key, typename Value>
    inline void pack( Stream& s, const std::pair<Key,Value>& val ) {
      tornet::rpc::raw::pack( s, val.first );
      tornet::rpc::raw::pack( s, val.second );
    }
    // support for pair!
    template<typename Stream, typename Key, typename Value>
    void unpack( Stream& s, std::pair<Key,Value>& val ) {
      tornet::rpc::raw::unpack( s, val.first );
      tornet::rpc::raw::unpack( s, val.second );
    }


    // support arbitrary key/value containers as an array of pairs
    template<typename Stream, typename Key, typename Value>
    void pack( Stream& s, const std::map<Key,Value>& value ) {
      pack( s, unsigned_int(value.size()) );
      typename std::map<Key,Value>::const_iterator itr = value.begin();
      typename std::map<Key,Value>::const_iterator end = value.end();
      while( itr != end ) {
        tornet::rpc::raw::pack( s, *itr );
        ++itr;
      }
    }

    template<typename Stream, typename Key, typename Value>
    inline void unpack( Stream& s, std::map<Key,Value>& value ) {
      unsigned_int size; unpack( s, size );
      value.clear();
      for( uint32_t i = 0; i < size.value; ++i ) {
        Key k; 
        tornet::rpc::raw::unpack(s,k);
        tornet::rpc::raw::unpack(s,value[k]);
      }
    }

    template<typename Stream, typename T> 
    inline void pack( Stream& s, const T& v ) {
      detail::if_fusion_seq< typename boost::fusion::traits::is_sequence<T>::type >::pack(s,v);
    }
    template<typename Stream, typename T> 
    inline void unpack( Stream& s, T& v ) {
      detail::if_fusion_seq< typename boost::fusion::traits::is_sequence<T>::type >::unpack(s,v);
    }





    template<typename T>
    inline void pack_vec( std::vector<char>& s, const T& v ) {
      datastream<size_t> ps; 
      raw::pack(ps,v );
      s.resize(ps.tellp());
      if( s.size() ) {
        datastream<char*>  ds( &s.front(), s.size() ); 
        raw::pack(ds,v);
      }
    }

    template<typename T>
    inline void unpack_vec( const std::vector<char>& s, T& v ) {
      if( s.size() ) {
        datastream<const char*>  ds( &s.front(), s.size() ); 
        raw::unpack(ds,v);
      }
    }

    template<typename T>
    inline void pack( char* d, uint32_t s, const T& v ) {
      datastream<char*> ds(d,s); 
      raw::pack(ds,v );
    }

    template<typename T>
    inline void unpack( const char* d, uint32_t s, T& v ) {
      datastream<const char*>  ds( d, s );
      raw::unpack(ds,v);
    }
    
} } } // namespace tornet::rpc::raw

#endif // BOOST_RPC_RAW_HPP
