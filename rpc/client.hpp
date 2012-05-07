#ifndef _TORNET_CLIENT_HPP_
#define _TORNET_CLIENT_HPP_
#include <boost/reflect/any_ptr.hpp>
#include <tornet/rpc/client_interface.hpp>
#include <tornet/rpc/client_base.hpp>

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
  };

} }

#endif
