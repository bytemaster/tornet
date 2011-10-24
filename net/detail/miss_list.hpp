#ifndef _TORNET_UDT_MISS_LIST_HPP_
#define _TORNET_UDT_MISS_LIST_HPP_
#include <list>
#include <stdint.h>

namespace tornet {
  class miss_list {
    public:
      void add( uint32_t start, uint32_t end );
      void remove( uint32_t seq );
      bool contains( uint32_t seq )const;
      void print()const;

      template<typename Stream>
      friend Stream& operator << ( Stream& s, const miss_list& n ) {
        uint8_t len = (std::min)(uint32_t(128),uint32_t(n.m_ml.size())); 
        s.write( (char*)&len, sizeof(len) );
        mlist::const_iterator itr = n.m_ml.begin();
        while( itr != n.m_ml.end() && len ) {
          s.write( (const char*)&itr->first, sizeof(itr->first) );
          s.write( (const char*)&itr->second, sizeof(itr->second) );
          --len;
          ++itr;
        }
        return s;
      }

      template<typename Stream>
      friend Stream& operator >> ( Stream& s, miss_list& n ) {
        uint8_t len = 0;
        s.read( (char*)&len, sizeof(len) );
        n.m_ml.resize(len);
        mlist::const_iterator itr = n.m_ml.begin();
        while( itr != n.m_ml.end() && len ) {
          s.read( (char*)&itr->first, sizeof(itr->first) );
          s.read( (char*)&itr->second, sizeof(itr->second) );
          --len;
          ++itr;
        }
        return s;
      }

    private:
      typedef std::list<std::pair<uint32_t,uint32_t> > mlist;
      mlist m_ml;
  };

} // tornet 

#endif
