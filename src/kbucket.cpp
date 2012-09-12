#include <tornet/kbucket.hpp>
#include <tornet/connection.hpp>
#include <tornet/db/peer.hpp>
#include <fc/bigint.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/sha1.hpp>

namespace tn {

  class kbucket::impl {
    public:
      impl():_buckets(161){}

      uint32_t                                _count;
      fc::sha1                                _node_id;
      std::vector< std::vector<connection*> > _buckets;


      int calc_bucket( const fc::sha1& dist ) {
        return 161 - fc::bigint( dist.data(), sizeof(dist) ).log2();
      }
  };

  kbucket::kbucket(){}

  kbucket::~kbucket(){}

  void kbucket::set_id(const fc::sha1& id) {
    my->_node_id = id;
  }

  std::vector<connection*>&  kbucket::get_bucket_for_target( const fc::sha1& target ) {
    return my->_buckets[ my->calc_bucket( target ^ my->_node_id )];
  }

  std::vector<connection*>&  kbucket::get_bucket_at_dist( const fc::sha1& dist ) {
    return my->_buckets[ my->calc_bucket( dist ) ];
  }

  typedef std::vector<connection*>& bref;
  void kbucket::add( connection* c ) {
    bref buck = get_bucket_for_target( c->get_remote_id() );
    buck.push_back(c);
    ++my->_count;
  }

  void kbucket::remove( connection* c ) {
    bref buck = get_bucket_for_target( c->get_remote_id() );
    auto itr  = std::find( buck.begin(), buck.end(), c );
    if( itr != buck.end() ) {
      buck.erase(itr);
      --my->_count;
    }
  }


  /**
   *  This method will update the priority for the given 
   *  connection relative to its peers.
   */
  void kbucket::update_priority( connection* c ) {
    uint64_t total_credit  = 0;
    uint64_t total_btc     = 0;
    uint64_t total_rank    = 0;
    uint64_t total_latency = 0;
    uint64_t total_bw      = 0;
    uint64_t total_uptime  = 0;
    uint64_t total_nat     = 0;

    const db::peer::record& r = c->get_db_record();

    bool nnat = !r.firewalled;
    auto i = my->_buckets.begin();
    auto e = my->_buckets.end();
    while( i != e ) {
      auto b = i->begin();
      auto be = i->end();
      while( b != be ) {
        const db::peer::record& br = (*b)->get_db_record();
        total_credit  += br.recv_credit < r.recv_credit;
        total_btc     += br.total_btc_recv < r.total_btc_recv;
        total_rank    += br.rank < r.rank;
        total_uptime  += br.last_contact > r.last_contact;
        total_latency += br.avg_rtt_us > r.avg_rtt_us;
        total_bw      += br.est_bandwidth < r.est_bandwidth;
        total_nat     += br.firewalled && nnat; // +1 for every host that is behind a nat when c is not
        ++b;
      }
      ++i;
    }

    uint64_t total = total_credit + total_btc + total_rank + total_latency + total_bw + total_uptime + total_nat;
    float    prio  = total / (7.0* my->_count);
    c->set_priority( prio );
  }

  void swap( tn::connection*& a, tn::connection*& b ){ std::swap(a,b); }
  /**
   *  Sorts the buckets 
   */
  void kbucket::resort_buckets() {
      auto b = my->_buckets.begin();
      auto e = my->_buckets.end();
      while( b != e ) {
        std::sort( b->begin(), b->end(),
          []( const connection* l, const connection* r ) -> bool {
            return l->priority() > r->priority();
          } 
        );
      }
  }

} // namespace tn
