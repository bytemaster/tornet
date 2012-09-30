#ifndef _TORNET_NAME_SERVICE_MESSAGES_HPP_
#define _TORNET_NAME_SERVICE_MESSAGES_HPP_
#include <fc/optional.hpp>
#include <fc/vector.hpp>
#include <fc/sha1.hpp>
#include <fc/pke.hpp>
#include <fc/static_reflect.hpp>
#include <tornet/name_chain.hpp>

namespace tn {

  enum name_service_message_ids {
    broadcast_msg_id         = 0,
    fetch_block_request_id   = 1,
    fetch_trxs_request_id    = 2,
    resolve_name_request_id  = 3
  };

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
       include_transactions = 0x01,
       include_block        = 0x02
    };
    fc::sha1         block_id;
    int64_t          block_num; // -1 for head, -2 for to use block_id
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
     tn::link          site_ref;
  };

} // namespace tn

FC_STATIC_REFLECT( tn::name_transaction, (type)(data) )
FC_STATIC_REFLECT( tn::fetch_block_request, (block_id)(block_num)(flags) )
FC_STATIC_REFLECT( tn::fetch_block_reply, (status)(block)(trxs) )
FC_STATIC_REFLECT( tn::broadcast_msg, (depth)(block)(trxs) )
FC_STATIC_REFLECT( tn::fetch_trxs_request, (trx_ids) )
FC_STATIC_REFLECT( tn::fetch_trxs_reply, (trxs) )
FC_STATIC_REFLECT( tn::resolve_name_request, (name) )
FC_STATIC_REFLECT( tn::resolve_name_reply, (status)(pub_key)(rank)(site_ref) )


#endif // _TORNET_NAME_SERVICE_MESSAGES_HPP_
