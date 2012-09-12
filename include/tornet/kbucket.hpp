#ifndef _TORNET_KBUCKET_HPP_
#define _TORNET_KBUCKET_HPP_
#include <vector>
#include <fc/fwd.hpp>

namespace fc {
  class sha1;
}

namespace tn {

  class connection;

  /**
   *  @brief keeps connections sorted by distance and priority.
   *
   *  Within each kbucket the connections must be sorted in such a way that it
   *  strengthens the network and prevents 'bad nodes' from slowing things down.
   *
   *  The 'best node' at a given distance (bucket) has provided me the most data, paid me the most money, 
   *  has a public IP address, highest rank, longest connection time, oldest age, 
   *  lowest latency and highest bandwidth. 
   *
   *  Sum their percentials in each of these categories to get their rank in the kbucket.
   *
   *  Within each 'kbucket' the nodes are sorted by the following in order of priority:
   *       Weight
   *    a) 1.0       The ones that have provided me with the most data, because they are most likely to serve me/others again.
   *                   - Assuming a histogram of nodes, what percent of nodes have given less that this node.
   *    b) 1.0       The ones that have paid me the most for my services (least likely to be a leach) 
   *                   - Assuming a historgram of nodes, what percent have paid me less per kb
   *       1.0       Public IP  (not nat).... these nodes are more likely professional / high-bandwidth. 
   *    e) 1.0       The ones that have the highest rank (most invested in their identity, similar to paid)
   *                   - What percent of known nodes have a lower rank?
   *    c) 1.0       The length of time they have been connected... likely to hang around (not be stale)
   *                   - What percent of nodes have been connected for less time.
   *    c) 1.0       The ones with the lowest latency (most likely to accelerate lookups)
   *    d) 1.0       The ones with the hightest bandwidth (probably related to a) 
   *
   */
  class kbucket {
    public:
      kbucket();
      ~kbucket();
      void set_id( const fc::sha1& id );

      void add( connection* c );
      void remove( connection* c );
      void update_priority( connection* c );

      void  resort_buckets();
      std::vector<connection*>&  get_bucket_at_dist( const fc::sha1& dist );
      std::vector<connection*>&  get_bucket_for_target( const fc::sha1& target );

    public:
      class impl;
      fc::fwd<impl,96> my;
  };

}


#endif // _TORNET_KBUCKET_HPP_
