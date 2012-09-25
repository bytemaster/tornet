#include <tornet/chunk_search.hpp>
#include "chunk_service_connection.hpp"
#include <fc/bigint.hpp>
#include <tornet/chunk_service_client.hpp>

namespace tn {

chunk_search::chunk_search( const node::ptr& local_node, const fc::sha1& target, 
                            uint32_t N, uint32_t P, bool fn )
:kad_search( local_node, target, N, P ), find_nm(fn), avg_qr(0), qr_weight(0), deadend_count(0), chunk_size(0) {

}


/**
 *  Stops the search after N hosts have been found with the desired chunk.
 *
 *  TODO: create a common database of chunk_session clients so that we do not
 *  need to create new channels/connections every time
 *
 *  TODO: Find out why things do get cleaned up
 */
void chunk_search::filter( const fc::sha1& id ) {
   auto csc = get_node()->get_client<chunk_service_client>(id);

   //elog( "fetch target %s on node %s", to_string(target()).c_str(), to_string(id).c_str() );
   /// TODO: UDP connections may drop the request, this would cause this strand to block here forever...  figure out timeout?  
   // In theory KAD searchs occur in parallel and should timeout on their own... 
   const fetch_response& fr = csc->fetch( target(), 0, 0 ).wait();
   if( fr.total_size ) chunk_size = fr.total_size;

   // update avg query rate... the closer a node is to the target the more 
   // often that node should be queried and the more accurate its estimate of
   // propularity should be.
   fc::bigint d( (target() ^ id).data(), sizeof(id) );

   deadend_count += fr.deadend_count;

   // convert interval into hz
   double query_rate_sec = double(fr.query_interval) / 1000000.0;
   double query_rate_hz  = 1.0 / query_rate_sec;
   int level = std::min( int(16- (160-d.log2()) ), int(1) ); 
   // a far away node should receive only say 2% of total queries, so we must
   // multiply by 50 to get the implied/normalized global rate
   double adj_rate =  level * query_rate_hz;
   // but because it is so far away, there is more error so its significance
   // is less than closer nodes.
   double weight = 1.0 / (1<<(level-1));
   //elog( "weight: %f  adj_rate: %f  level: %d", weight, adj_rate, level );

   // calculate the new running weighted average query rate
   avg_qr = (avg_qr * qr_weight + adj_rate) / (qr_weight + weight);
   qr_weight = qr_weight + weight;

   if( fr.result == chunk_session_result::available ) {
     m_host_results[id^target()] = id; 
     if( m_host_results.size() >= m_n ) {
        set_status( kad_search::done );
     }
   } 
}
uint32_t chunk_search::get_chunk_size()const    { return chunk_size;    }

double   chunk_search::avg_query_rate()const    { return avg_qr;        }
double   chunk_search::query_rate_weight()const { return qr_weight;     }
uint32_t chunk_search::get_deadend_count()const { return deadend_count; }



} // namespace tornet
