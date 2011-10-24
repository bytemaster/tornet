#include <tornet/rpc/service.hpp>
#include <tornet/rpc/connection.hpp>

namespace tornet { namespace rpc {
  class service_private {
    public:
      service_private( rpc::service& s, const tornet::node::ptr& n, const std::string& name, uint16_t p,
                       boost::cmt::thread* t ) 
      :self(s),m_node(n),m_port(p),m_thread(t) {
        m_node->start_service( p, name, boost::bind( &service_private::on_connection, this, _1 ) );
      }

      void on_connection( const tornet::channel& c ) {
        rpc::connection::ptr rpcc( new rpc::connection( c, m_thread ) ); 
        rpcc->closed.connect( boost::bind( &service_private::on_close, this, rpc::connection::wptr(rpcc) ) );
        
        // add service methods to connection!

        m_connections.push_back(std::make_pair(rpcc, self.init_connection(rpcc) ));
      }

      void on_close( const rpc::connection::wptr& c ) {
        rpc::connection::ptr p(c);
        con_list::iterator itr = m_connections.begin();
        while( itr != m_connections.end() ) {
          if( itr->first == p ) { m_connections.erase(itr); return; }
          ++itr;
        }
      }
      typedef std::list< std::pair<rpc::connection::ptr,boost::any> > con_list;
      con_list            m_connections;
      uint16_t            m_port; 
      tornet::node::ptr   m_node;
      boost::cmt::thread* m_thread;
      service&            self;
  };

  service::service( const tornet::node::ptr& n, const std::string& name, uint16_t port,
                    boost::cmt::thread* t ) {
    my = new service_private(*this, n, name, port, t );
  }

  service::~service() {
    delete my;
  }

  /**
   *  Derived services overload this method to initialize the connection and return 
   *  connection-specific data that will be freed when the connection closes.
   */
  boost::any service::init_connection( const rpc::connection::ptr& con ) {
    return boost::any();
  }

} }// tornet::rpc
