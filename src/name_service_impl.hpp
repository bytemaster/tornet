#include <tornet/name_service.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/name_service_messages.hpp>
#include <tornet/raw_rpc.hpp>
#include <tornet/node.hpp>
#include <tornet/db/name.hpp>
#include <fc/fwd_impl.hpp>

namespace tn {
  class name_service_connection : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<name_service_connection> ptr;

      name_service_connection( name_service& ns, udt_channel&& c );

      publish_name_reply publish_name( const publish_name_request& req );
      resolve_name_reply resolve_name( const resolve_name_request& req );

    protected:
      virtual ~name_service_connection(){}
      name_service& _ns;
      udt_channel   _chan;
      raw_rpc       _rpc;
  };

  class name_service::impl {
    public:
      impl( name_service& s ):_self(s){}

      float               _effort;      
      name_service&       _self;
      fc::path            _dir;
      tn::node::ptr       _node;
      tn::db::name::ptr   _name_db;

      // nodes subscribed to us
      fc::vector<name_service_connection::ptr> _subscribers;

      void on_new_connection( const channel& c );
  };

} // namespace tn
