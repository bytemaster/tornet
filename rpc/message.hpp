#ifndef _TORNET_RPC_MESSAGE_HPP_
#define _TORNET_RPC_MESSAGE_HPP_
#include <boost/rpc/varint.hpp>

namespace tornet { namespace rpc {
  using boost::rpc::signed_int;
  using boost::rpc::unsigned_int;

  struct message {
    enum types {
      notice = 1,
      call   = 2,
      result = 3,
      error  = 4
    };
    uint16_t          id;
    uint8_t           type;
    unsigned_int      method_id;
    std::vector<char> data;
  };

} } // tornet::rpc

BOOST_REFLECT( tornet::rpc::message, 
    (id)
    (type)
    (method_id)
    (data)
)

#endif
