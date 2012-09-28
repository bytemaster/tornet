#ifndef _TORNET_NAME_SERVICE_MESSAGES_HPP_
#define _TORNET_NAME_SERVICE_MESSAGES_HPP_
#include <fc/optional.hpp>
#include <fc/vector.hpp>
#include <fc/sha1.hpp>
#include <fc/pke.hpp>
#include <tornet/name_chain.hpp>

namespace tn {

  /**
   *  To broadcast a message I start with a depth of 0 and
   *  then send the request (with depth 0) to the first K nodes of bucket 0 (the other half of the network)
   *  then send the request (with depth 1) to the first K nodes of bucket 1 (my half of the network)
   *
   *  Upon receiving a request of depth N, send to the first K nodes of bucket N+1
   *
   *  If bucket N+1 does not contain K nodes, continue to bucket N+2.. N+3.. etc until the message 
   *  has been sent.
   *
   *  With this technique, the most reliable nodes are at the center of the broadcast.
   *
   *  
   *
   *
   *
   */

  struct name_transaction {
    uint8_t          type;
    fc::vector<char> data;
  };

  struct fetch_block_request {
    enum flag_values {
       include_transactions = 0x01
    };
    fc::sha1         block_id;
    int64_t          block_num; // -1 for head
    uint8_t          flags;
  };

  struct fetch_block_reply {
    enum status_values {
      ok            = 0x00,
      unknown_block = 0x01
    };
    uint8_t                      status;
    name_block                   block;
    fc::vector<name_transaction> trxs;
  };

  struct broadcast_msg {
      uint8_t                      depth;
      fc::optional<name_block>     block;
      fc::vector<name_transaction> trxs;
  };

  struct fetch_trxs_request {
    fc::vector<fc::sha1> trx_ids;
  };

  struct fetch_trxs_reply {
    fc::vector<name_transaction> trxs;
  };

  struct resolve_name_request {
    fc::sha1 name;
  };

  struct resolve_name_reply {
     enum status_values {
       found     = 0x00,
       not_found = 0x01,
       not_synced = 0x02 // in the event that this node is also 'unsynced'
     };
     uint8_t           status;
     fc::public_key_t  pub_key;   
     uint32_t          rank;      // how many times has the name been updated
     fc::sha1          name;      // passed with request
     fc::sha1          value_id;  // info required to download page / email / etc.
     fc::sha1          key;
     uint64_t          seed;
  };

} // namespace tn


#endif // _TORNET_NAME_SERVICE_MESSAGES_HPP_
