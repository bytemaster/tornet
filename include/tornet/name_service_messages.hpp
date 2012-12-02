#ifndef _TORNET_NAME_SERVICE_MESSAGES_HPP_
#define _TORNET_NAME_SERVICE_MESSAGES_HPP_
#include <fc/optional.hpp>
#include <fc/vector.hpp>
#include <fc/sha1.hpp>
#include <fc/pke.hpp>
#include <fc/reflect.hpp>
#include <tornet/name_chain.hpp>

namespace tn {

  enum name_service_message_ids {
    publish_name_id = 0,
    resolve_name_id = 1,
  };

  struct publish_name_request {
     uint8_t           level;
     fc::string        name;
     fc::public_key_t  key;
     tn::link          site_ref;
     uint32_t          expires;
     uint64_t          nonce;
     fc::signature_t   sig;
  };


  struct publish_name_reply {
      enum publish_name_reply_status {
         ok = 0,
         already_reserved = 1,
         invalid_request = 2,
         unknown_name = 3,
         rejected = 4
      };
    uint8_t status;
  };

  struct resolve_name_request {
    fc::string name;
  };

  struct resolve_name_reply {
    fc::optional<publish_name_request>  record;
  };

} // namespace tn

FC_REFLECT( tn::publish_name_request, (name)(key)(site_ref)(expires)(nonce)(sig) )
FC_REFLECT( tn::publish_name_reply, (status) )
FC_REFLECT( tn::resolve_name_request, (name) )
FC_REFLECT( tn::resolve_name_reply, (record) )

#endif // _TORNET_NAME_SERVICE_MESSAGES_HPP_
