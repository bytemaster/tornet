#ifndef _TORNET_SERVICES_CHUNK_SESSION_HPP
#define _TORNET_SERVICES_CHUNK_SESSION_HPP
#include <scrypt/sha1.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/rpc/connection.hpp>

struct chunk_session_result {
enum result_enum {
  ok,
  invalid_rank,
  invalid_size,
  credit_limit_reached,
  unknown_chunk,
  already_stored,
  rejected,
  unknown
};
};

struct fetch_response {
  fetch_response( chunk_session_result::result_enum e = chunk_session_result::unknown, int64_t new_bal = 0 )
  :result(e),offset(0),balance(new_bal){}

  int8_t                    result; 
  uint32_t                  offset;
  std::vector<char>         data;
  std::vector<scrypt::sha1> references;   
  int64_t                   balance;
};

struct store_response {
  store_response( chunk_session_result::result_enum e, int64_t new_bal = 0 )
  :result(e),balance(new_bal){}
  int8_t     result; 
  int64_t    balance;
};

/**
 *  Defines the RPC interface to the chunk service. 
 */
class chunk_session {
  public:
    typedef boost::shared_ptr<chunk_session> ptr;
    typedef scrypt::sha1 chunk_id;
    typedef scrypt::sha1 node_id;

    chunk_session( const tornet::db::chunk::ptr& chunk_db, const tornet::rpc::connection::ptr& con );

    int64_t  get_credit_limit()const;
    int64_t  get_balance()const;

    /**
     *  The price per byte to publish chunk cid of size.  To justify publishing the database must
     *  toss its least profitiable chunks up to size.  Assume published data will last for 1 month.
     *
     *  Calculate the number of queries at range cid required to generate the same monthly revenue and
     *  this becomes the 'base-line' popularity.  Charge publisher for that number of queries.  
     */
    int64_t  get_publish_price( const chunk_id& cid, uint32_t size )const;
    int8_t   publish_rank()const;

    /// return total 'balance' sent, aka how much this node sent to the remote node
    int64_t  total_sent();

    /// return total 'balance' received, aka how much the remote node provided to this node.
    int64_t  total_recv();

    /**
     *  @param bytes - if -1 then the entire chunk will be returned starting from offset
     *  
     *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
     */
    fetch_response fetch( const chunk_id& id, int32_t bytes = -1, uint32_t offset = 0 );

    /**
     *  Price is  (100 + data.size) * publish_price() if 'accepted' or
     *  Price is  (100 + data.size) if rejected.
     */
    store_response store( const std::vector<char>& data );

    /**
     *  Price is (120) * publish_price()
     */
    store_response store_reference( const chunk_id& );

    /**
     *  Return the bitcoin address used to add to
     *  your balance.
     */
    std::string get_bitcoin_address();

    /**
     *  Returns the amount of credit provided per
     *  BTC received.  
     */
    uint64_t    get_credit_per_btc()const;
    private:

    tornet::db::chunk::ptr       m_cdb; 
    tornet::rpc::connection::ptr m_con;

};

#endif // _TORNET_SERVICES_CHUNK_SESSION_HPP
