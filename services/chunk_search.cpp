#include "chunk_search.hpp"

#include <tornet/rpc/client.hpp>
#include <tornet/services/reflect_chunk_session.hpp>
#include <scrypt/bigint.hpp>

namespace tornet {

chunk_search::chunk_search( const node::ptr& local_node, const scrypt::sha1& target, 
                            uint32_t N, uint32_t P, bool fn )
:kad_search( local_node, target, N, P ), find_nm(fn), avg_qr(0), qr_weight(0), deadend_count(0) {

}


/**
 *  Stops the search after N hosts have been found with the desired chunk.
 *
 *  TODO: create a common database of chunk_session clients so that we do not
 *  need to create new channels/connections every time
 *
 *  TODO: Find out why things do get cleaned up
 */
void chunk_search::filter( const node::id_type& id ) {
  
    /*
   tornet::rpc::client<chunk_session>::ptr  chunk_client_ptr;
   boost::any accp  = get_node()->get_cached_object( id, "rpc::client<chunk_session>" );
   if( boost::any_cast<tornet::rpc::client<chunk_session>::ptr>(&accp) ) {
      chunk_client_ptr = boost::any_cast<tornet::rpc::client<chunk_session>::ptr>(accp); 
   } else {
      elog( "creating new channel / connection" );
       tornet::channel                chan = get_node()->open_channel( id, 100 );
       tornet::rpc::connection::ptr   con  = boost::make_shared<tornet::rpc::connection>(chan);
       chunk_client_ptr = boost::make_shared<tornet::rpc::client<chunk_session> >(con);
       get_node()->cache_object( id, "rpc::client<chunk_session>", chunk_client_ptr );
   }
   */
   tornet::rpc::client<chunk_session>&  chunk_client = 
    *tornet::rpc::client<chunk_session>::get_udp_connection( get_node(), id );//*chunk_client_ptr;
  

   elog( "fetch target %1% on node %2%", target(), id );
   /// TODO: UDP connections may drop the request, this would cause this strand to block here forever...  figure out timeout?  
   // In theory KAD searchs occur in parallel and should timeout on their own... 
   fetch_response fr = chunk_client->fetch( target(), 0, 0 );

   // update avg query rate... the closer a node is to the target the more 
   // often that node should be queried and the more accurate its estimate of
   // propularity should be.
   scrypt::bigint d( (char*)(target() ^ id).hash, sizeof( id.hash ) );

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
   elog( "weight: %1%  adj_rate: %2%  level: %3%", weight, adj_rate, level );

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

double   chunk_search::avg_query_rate()const    { return avg_qr;        }
double   chunk_search::query_rate_weight()const { return qr_weight;     }
uint32_t chunk_search::get_deadend_count()const { return deadend_count; }



} // namespace tornet
