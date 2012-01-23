#include "accounting.hpp"
#include <ltl/persist.hpp>
#include <ltl/server.hpp>
#include <ltl/rpc/session.hpp>
#include <ltl/rpc/reflect.hpp>

namespace tornet { namespace service {
  accounting::accounting( const boost::filesystem::path& dbdir, 
              const tornet::node::ptr& node, const std::string& name, uint16_t port,
              boost::cmt::thread* t )
  :service(node,name,port,t) {
    boost::filesystem::create_directories(dbdir/"db");
    m_serv = ltl::server::ptr( new ltl::server(dbdir) );
  }

  boost::any accounting::init_connection( const rpc::connection::ptr& con ) {
      boost::shared_ptr<ltl::rpc::session> cc(new ltl::rpc::session(m_serv) );
      boost::reflect::any_ptr<ltl::rpc::session> acc(cc);

      uint16_t mid = 0;
      boost::reflect::visit(acc, service::visitor<ltl::rpc::session>(*con, acc, mid) );

      return acc;
  }
} }
