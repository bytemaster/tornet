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

  fc::future<publish_name_reply> name_service_client::publish_name( const publish_name_request& r ) {
    return my->_rpc.invoke<publish_name_reply>( publish_name_id, r );
  }
  fc::future<resolve_name_reply> name_service_client::resolve_name( const resolve_name_request& r ) {
    return my->_rpc.invoke<resolve_name_reply>( resolve_name_id, r );
  }


} // namespace tn
