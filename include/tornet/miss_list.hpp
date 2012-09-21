#ifndef _TORNET_UDT_MISS_LIST_HPP_
#define _TORNET_UDT_MISS_LIST_HPP_
#include <list>
#include <stdint.h>
#include <tornet/sequence_number.hpp>

namespace tn {
  class miss_list {
    public:
      typedef sequence::number<uint16_t> seq_num;
      bool pop_front( seq_num& seq );
      uint32_t size()const;

      void add( seq_num start, seq_num end );
      void remove( seq_num seq );
      bool contains( seq_num seq )const;
      void print()const;
      void clear();

      template<typename Stream>
      friend Stream& operator << ( Stream& s, const miss_list& n ) {
        uint8_t len = (std::min)(seq_num(128),seq_num(n.m_ml.size())); 
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
      typedef std::list<std::pair<seq_num,seq_num> > mlist;
      mlist m_ml;
  };

} // tornet 

#endif
