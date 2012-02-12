#ifndef _TORNET_NET_CHUNK_SEARCH_HPP_
#define _TORNET_NET_CHUNK_SEARCH_HPP_
#include <tornet/net/kad.hpp>

namespace tornet {

  /**
   *  @class chunk_search
   *  
   *  The goal is to find the first N nodes that host a chunk and gather
   *  info about how frequently a particular chunk is queried from all nodes
   *  along the search path.   
   *
   *  If N nodes are not found to host the data, then the closest N nodes to
   *  the data should be returned.
   *
   *  This is an asynchronous process that may be cancled by the user and which
   *  ultimately must report progress.
   */
  class chunk_search : public kad_search {
    public:

      /**
       *    Query 
       */
      virtual bool filter( const node::id_type& id );

  };

} // namespace tornet

#endif // _TORNET_NET_CHUNK_SEARCH_HPP_
