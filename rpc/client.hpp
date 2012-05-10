#ifndef _TORNET_CLIENT_HPP_
#define _TORNET_CLIENT_HPP_
#include <boost/reflect/any_ptr.hpp>
#include <tornet/rpc/client_interface.hpp>
#include <tornet/rpc/client_base.hpp>
#include <tornet/rpc/udp_connection.hpp>
#include <tornet/rpc/udt_connection.hpp>

namespace tornet { namespace rpc {

  /**
   *   Given a p2p connection, map methods on InterfaceType into RPC calls
   *   with the expected results returned as futures as defined by p2p::client_interface
   *
   *   @todo - make client_interface take a pointer to client_base to enable the
   *          construction of one client<Interface> and then quickly change the 
   *          connection object without having to iterate over the methods again.
   */
  template<typename InterfaceType>
  class client : public boost::reflect::any_ptr<InterfaceType, tornet::rpc::client_interface>, 
                 public client_base {
    public:
      typedef boost::shared_ptr<client>   ptr;

      client(){}
      client( const client& c ):client_base(c) {
        tornet::rpc::client_interface::set( *this );
      }
      bool operator!()const { return !m_con; }

      client& operator=( const client& c ) {
        if( &c != this )  {
            m_con = c.m_con;
            tornet::rpc::client_interface::set( *this );
        }
        return *this;
      }

      client( const tornet::rpc::connection::ptr& c) 
      :client_base(c) {
         tornet::rpc::client_interface::set( *this );
      }      

      /**
       *  Returns the singlton connection to a particular node id on the given port
       *
       */
      static client::ptr get_udp_connection( const tornet::node::ptr& n, 
                                         const tornet::node::id_type& id, uint16_t port = InterfaceType::port ) {
        ptr  client_ptr;
        std::string name = boost::reflect::get_typename<InterfaceType>(); 
        name += "_udp";
        boost::any accp  = n->get_cached_object( id, name );
        if( boost::any_cast<tornet::rpc::client<InterfaceType>::ptr>(&accp) ) {
           client_ptr = boost::any_cast<tornet::rpc::client<InterfaceType>::ptr>(accp); 
        } else {
           elog( "creating new channel / connection" );
            tornet::channel                chan = n->open_channel( id, port );
            tornet::rpc::connection::ptr   con  = boost::static_pointer_cast<tornet::rpc::connection>(boost::make_shared<tornet::rpc::udp_connection>(chan));
            client_ptr = boost::make_shared<tornet::rpc::client<InterfaceType> >(con);
            n->cache_object( id, name, client_ptr );
        }
        return client_ptr;
      }

      /**
       *  Returns the singlton udt connection to a particular node id on the given port
       *
       */
      static client::ptr get_udt_connection( const tornet::node::ptr& n, 
                                         const tornet::node::id_type& id, uint16_t port = InterfaceType::port + 1) {
        ptr  client_ptr;
        std::string name = boost::reflect::get_typename<InterfaceType>(); 
        name += "_udt";
        boost::any accp  = n->get_cached_object( id, name );
        if( boost::any_cast<tornet::rpc::client<InterfaceType>::ptr>(&accp) ) {
           client_ptr = boost::any_cast<tornet::rpc::client<InterfaceType>::ptr>(accp); 
        } else {
           elog( "creating new channel / connection" );
            tornet::channel                chan = n->open_channel( id, port );
            tornet::rpc::connection::ptr   con  = boost::static_pointer_cast<tornet::rpc::connection>(boost::make_shared<tornet::rpc::udt_connection>(chan));
            client_ptr = boost::make_shared<tornet::rpc::client<InterfaceType> >(con);
            n->cache_object( id, name, client_ptr );
        }
        return client_ptr;
      }
  };

} }

#endif
