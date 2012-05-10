#include "chunk_session.hpp"
#include <tornet/rpc/connection.hpp>
#include <algorithm>

chunk_session::chunk_session( const tornet::db::chunk::ptr& chunk_db, const tornet::rpc::connection::ptr& con )
:m_cdb(chunk_db),m_con(con) {
}

int64_t  chunk_session::get_credit_limit()const {
  return 0x00ffffffffffff;
}
int64_t  chunk_session::get_balance()const {
  return 0;
}
int64_t  chunk_session::get_publish_price(const chunk_id& cid, uint32_t size )const {
  return 0;
}
int8_t   chunk_session::publish_rank()const {
  return 160;
}

fetch_response chunk_session::fetch( const chunk_id& cid, int32_t bytes, uint32_t offset  ) {
  slog( "fetch %1%   bytes: %2%   ofset: %3%", cid, bytes, offset );
  tornet::db::chunk::meta met;
  m_cdb->fetch_meta( cid, met, true );

  if( !met.size ) {
    // TODO: Check for References
    // TODO: Calculate Cost and Bill User Account
    int64_t new_bal = 0;
    fetch_response fr( chunk_session_result::unknown_chunk, new_bal );
    double sec = (met.access_interval()/double(1000000));
    wlog( "ai: %1% sec  %2% hz query count: %3%", sec, 1.0/sec, met.query_count );
    fr.query_interval = met.access_interval();
    return fr;
  }

  fetch_response r;
  r.offset = offset;
  if( met.size && bytes > 0 && offset < met.size ) {
    r.data.resize( (std::min)(uint32_t(met.size - offset),uint32_t(bytes) ) );
    m_cdb->fetch_chunk( cid, boost::asio::buffer( r.data ), offset );
  }

  // TODO: Calculate Cost and Bill User Account
  int64_t new_bal = 0;

  r.balance = new_bal;
  r.result  = chunk_session_result::ok;
  r.query_interval = met.access_interval();

  return r;
}
store_response chunk_session::store( const std::vector<char>& data ) {
  slog( "store %1% bytes", data.size() );
  // TODO: Is rank( con->remote_id() ) > rank( local_node_id )

  int64_t new_bal = 0;
  
  if( data.size() == 0 || data.size() > 1024*1024 ) {
    elog( "invalid size: %1% > %2%", data.size(), 1024*1024 );
    // TODO: Calculate Cost and Bill User Account
    return store_response( chunk_session_result::invalid_size, new_bal );
  }
  
  chunk_id cid;
  scrypt::sha1_hash( cid, &data.front(), data.size() );

  tornet::db::chunk::meta met;
  m_cdb->fetch_meta( cid, met, true );
  if( met.size == 0 ) {
    if( m_cdb->store_chunk( cid, boost::asio::buffer(data) ) )  { 
      slog( "ok, stored" );
      // TODO: Calculate Cost and Bill User Account, only the DB can decide whether or not to store the chunk
      return store_response( chunk_session_result::ok, new_bal );
    } else {
      slog( "rejected, not stored" );
      // TODO: Calculate Cost and Bill User Account, don't bill as much if w do not store.
      return store_response( chunk_session_result::rejected, new_bal );
    }
  } 


  // TODO: Calculate Cost and Bill User Account
  return store_response( chunk_session_result::already_stored, new_bal );
}
store_response chunk_session::store_reference( const chunk_id& ) {
  store_response r(chunk_session_result::unknown);
  return r;
}

/**
 *  Return the bitcoin address used to add to
 *  your balance.
 */
std::string chunk_session::get_bitcoin_address() {
  return "";
}

/**
 *  Returns the amount of credit provided per
 *  BTC received.  
 */
uint64_t chunk_session::get_credit_per_btc()const {
  return 0;
}
