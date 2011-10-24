#include "miss_list.hpp"
#include <iostream>

namespace tornet {

void miss_list::clear() { m_ml.clear(); }

void miss_list::add( seq_num start, seq_num end ) {
  mlist::iterator itr = m_ml.begin();
  while( itr != m_ml.end() && (itr->second+1) < start ) { 
    ++itr;
  }
  if( itr == m_ml.end() ) {
    m_ml.push_back( std::make_pair(start,end) );
    return;
  }
  if( itr->second+1 == start )  {
    itr->second = end;
    return;
  } else { itr++; }

  if( itr == m_ml.end() ) {
    m_ml.push_back( std::make_pair(start,end) );
  } else {
    if( end+1 == itr->first ) 
      itr->first = end;
    else
      m_ml.insert( itr, std::make_pair(start,end) );
  }
}

void miss_list::remove( seq_num seq ) {
  mlist::iterator itr = m_ml.begin();
  while( itr != m_ml.end() ) {
    if( itr->first <= seq && seq <= itr->second ) { 
      if( itr->first == seq && itr->second == seq ) {
        m_ml.erase(itr);
      } else if( itr->first == seq ) {
        itr->first++;
        return;
      } else if( itr->second == seq ) {
        itr->second--;
        return;
      } else if( itr->first < seq && seq < itr->second  ) {
        m_ml.insert( itr, std::make_pair( itr->first, seq-1 ) );
        itr->first = seq+1;
      }
      return;
    } 
    if( itr->first > seq )
      return;
    ++itr;
  }
}

bool miss_list::contains( seq_num seq )const {
  mlist::const_iterator itr = m_ml.begin();
  while( itr != m_ml.end() ) {
    if( itr->first <= seq && itr->second >= seq ) { 
      return true;
    }
    ++itr;
  }
  return false;
}

void miss_list::print()const {
  mlist::const_iterator itr = m_ml.begin();
  while( itr != m_ml.end() ) {
    std::cerr<<"["<<std::string(itr->first)<<", "<<std::string(itr->second)<<"]";
    ++itr;
  }

}

} // tornet
