#include <tornet/chunk_service_client.hpp>
#include <tornet/service_ports.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/raw_rpc.hpp>
#include <fc/fwd_impl.hpp>

namespace tn {
  



  class chunk_service_client::impl {
    public:
      impl(tn::node& n, const fc::sha1& c):_node(n),_id(c){}
      tn::node&       _node;
      fc::sha1        _id;
      udt_channel     _udt_chan;
      
      raw_rpc         _rpc;
  };
  
  chunk_service_client::chunk_service_client( tn::node& n, const fc::sha1& c )
  :my(n,c) {
      my->_rpc.connect( udt_channel( n.open_channel( c, chunk_service_udt_port ) ) );
  }

  chunk_service_client::~chunk_service_client() {

  }

  /**
   *  @param bytes - if -1 then the entire chunk will be returned starting from offset
   *  
   *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
   */
  fc::future<fetch_response> chunk_service_client::fetch( const fc::sha1& id, int32_t bytes, uint32_t offset ) {
    return my->_rpc.invoke<fetch_response>( fetch_method_id, fetch_request( id, bytes, offset ) );
  }

  /**
   *  @param bytes - if -1 then the entire chunk will be returned starting from offset
   *  
   *  Price is  (100 + bytes returned) * (160-log2((id^local_node_id)*10)) 
   */
  fc::future<store_response> chunk_service_client::store( fc::vector<char>& data ) {
    return my->_rpc.invoke<store_response>( store_method_id, data );
  }
}
