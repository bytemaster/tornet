#ifndef _TORNET_ACCOUNTING_SERVICE_HPP_
#define _TORNET_ACCOUNTING_SERVICE_HPP_
#include <tornet/rpc/service.hpp>
#include <tornet/rpc/connection.hpp>
#include <boost/filesystem.hpp>

namespace ltl { class server; }

namespace tornet { namespace service {

  class accounting : public tornet::rpc::service {
    public:
      accounting( const boost::filesystem::path& dbdir, const tornet::node::ptr& node, const std::string& name, uint16_t port,
              boost::cmt::thread* t = &boost::cmt::thread::current() );
    
    protected:
      virtual boost::any init_connection( const rpc::connection::ptr& con );
      boost::shared_ptr<ltl::server>        m_serv;
  };

} }

#endif
