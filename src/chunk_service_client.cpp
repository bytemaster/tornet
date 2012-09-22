#include <tornet/chunk_service_client.hpp>
#include <tornet/service_ports.hpp>
#include <tornet/udt_channel.hpp>
#include <fc/fwd_impl.hpp>

namespace tn {
  class chunk_service_client::impl {
    public:
      impl(){}
      tn::node::ptr _node;
      udt_channel   _udt_chan;
  };
  
  chunk_service_client::chunk_service_client( const tn::node::ptr& n, const fc::sha1& remote_id ) {
      my->_node      = n;
      my->_udt_chan  = udt_channel( n->open_channel( remote_id, chunk_service_udt_port ) );
  }

  chunk_service_client::~chunk_service_client() {

  }

  /**
   *  @param bytes - if -1 then the entire chunk will be returned starting from offset
   *  
   *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
   */
  fetch_response chunk_service_client::fetch( const fc::sha1& id, int32_t bytes, uint32_t offset ) {
    fetch_response r;
    return  r;
  }
}
