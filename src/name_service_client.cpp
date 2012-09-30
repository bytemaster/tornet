#include <tornet/name_service_client.hpp>
#include <tornet/service_ports.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/raw_rpc.hpp>
#include <fc/fwd_impl.hpp>
#include <tornet/node.hpp>

namespace tn {

  class name_service_client::impl {
    public:
      impl(tn::node& n, const fc::sha1& c):_node(n),_id(c){}
      tn::node&       _node;
      fc::sha1        _id;
      udt_channel     _udt_chan;
      
      raw_rpc         _rpc;
  };
  
  name_service_client::name_service_client( tn::node& n, const fc::sha1& c )
  :my(n,c) {
      my->_rpc.connect( udt_channel( n.open_channel( c, name_service_udt_port ) ) );
  }

  name_service_client::~name_service_client() {

  }


  fc::future<fetch_block_reply> name_service_client::fetch_head() {
    fetch_block_request req;
    req.flags     = fetch_block_request::include_block;
    req.block_num = -1;
    return my->_rpc.invoke<fetch_block_reply>( fetch_block_request_id, req );
  }
  fc::future<fetch_block_reply> name_service_client::fetch_block( const fc::sha1& id ) {
    fetch_block_request req;
    req.flags     = fetch_block_request::include_block;
    req.block_id  = id;
    req.block_num = -2;  
    return my->_rpc.invoke<fetch_block_reply>( fetch_block_request_id, req );
  }
  fc::future<fetch_block_reply> name_service_client::fetch_block_transactions( const fc::sha1& id ) {
    fetch_block_request req;
    req.flags     = fetch_block_request::include_block | fetch_block_request::include_transactions;
    req.block_id  = id;
    req.block_num = -2;  
    return my->_rpc.invoke<fetch_block_reply>( fetch_block_request_id, req );
  }
  fc::future<fetch_trxs_reply>  name_service_client::fetch_trxs( const fetch_trxs_request& r ) {
    return my->_rpc.invoke<fetch_trxs_reply>( fetch_trxs_request_id, r );
  }

  void  name_service_client::broadcast( const broadcast_msg& m ) {
    my->_rpc.notice( broadcast_msg_id, m );
  }


} // namespace tn
