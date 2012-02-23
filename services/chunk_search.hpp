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
      typedef boost::shared_ptr<chunk_search> ptr;

      chunk_search( const node::ptr& local_node, const scrypt::sha1& target, 
                    uint32_t N, uint32_t P, bool find_near_matches = false  );

      /**
       *  Ask the node if it has the desired data then it returns true, otherwise
       *  it gets put in the near matches if requested.
       */
      virtual void filter( const node::id_type& id );

      /**
       *  How often is this chunk searched for on the network
       */
      double avg_query_rate()const;
      /**
       *  How many nodes were factored into the query rate average;
       */
      double query_rate_weight()const;
      uint32_t get_deadend_count()const;

      /**
       *  Nodes close to the chunk that host the chunk
       */
      const std::map<node::id_type,node::id_type>&  hosting_nodes()const { return m_host_results; }
    private:
      bool                                        find_nm; 
      uint32_t                                    deadend_count;
      double                                      avg_qr;
      double                                      qr_weight;
      std::map<node::id_type, node::id_type>      m_host_results;
  };

} // namespace tornet

#endif // _TORNET_NET_CHUNK_SEARCH_HPP_
