#include <tornet/name_service.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/name_service_messages.hpp>
#include <tornet/raw_rpc.hpp>
#include <tornet/node.hpp>
#include <tornet/db/name.hpp>

namespace tn {
  class name_service_connection : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<name_service_connection> ptr;

      name_service_connection( udt_channel&& c );

      void               broadcast( const broadcast_msg& m );
      fetch_block_reply  fetch_blocks( const fetch_block_request& req );
      resolve_name_reply resolve_name( const resolve_name_request& req );
      fetch_trxs_reply   fetch_transactions( const fetch_trxs_request& req );

    protected:
      virtual ~name_service_connection(){}
      udt_channel _chan;
      raw_rpc     _rpc;
  };

  class name_service::impl {
    public:
      float               _effort;      
      fc::path            _dir;
      tn::node::ptr       _node;
      tn::db::name::ptr   _name_db;

      tn::name_block      _head_block; // the block currently accepted as most recent
      tn::name_block      _gen_block;  // the block we are building to append to head

      fc::sha1 find_head_block();

      /**
       *  Checks locally first, then downloads it from the network
       */
      void     download_block( const fc::sha1& bid, tn::name_block& b );

      // nodes subscribed to us
      fc::vector<name_service_connection::ptr> _subscribers;

      // nodes we are subscribed to
      fc::vector<name_service_connection::ptr> _sources;

      void subscribe_to_network();
      void on_new_connection( const channel& c );
  };

} // namespace tn
