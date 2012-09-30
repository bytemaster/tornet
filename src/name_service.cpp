#include <tornet/name_service.hpp>
#include <tornet/name_service_messages.hpp>
#include <tornet/node.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/db/name.hpp>
#include <tornet/raw_rpc.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <fc/time.hpp>

#include <time.h>
#include <stdlib.h>

#include "name_service_impl.hpp"

namespace tn {


  name_service::name_service( const fc::path& sdir, const fc::shared_ptr<tn::node>& n ) {
    my->_dir  = sdir;
    my->_node = n;

    my->_node->start_service( 53, "named", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
    srand( time(NULL) );

    my->_name_db.reset( new db::name( sdir ) );
    my->_name_db->init();
  }

  name_service::~name_service() {
    shutdown();
  }

  
  fc::vector<fc::string> name_service::get_reserved_names()const{
    fc::vector<fc::string> names;
    return names;
  }

  void name_service::shutdown() {
    my->_node->close_service( 53 );
  }


  void name_service::reserve_name( const fc::string& name, const tn::link& site_ref ) {
    tn::db::name::record rec;
    if( my->_name_db->fetch_record_for_name( name, rec ) ) {
      FC_THROW_MSG( "Name %s is already registered", name.c_str() );
    }

    if( name.size() > 127 ) {
      FC_THROW_MSG( "Name '%s' is longer than 127 characters", name.c_str() );
    }

    db::name::private_name pn;
    auto nhash = fc::sha1::hash( name.c_str(), name.size() );
    if( !my->_name_db->fetch( nhash, pn ) ) {
        fc::generate_keys( pn.pub_key, pn.priv_key ); 
        memset( pn.name.data, 0, sizeof(pn.name) );
        memcpy( pn.name.data, name.c_str(), name.size() + 1 );
        uint64_t now = rand() + fc::time_point::now().time_since_epoch().count();
        pn.rand = fc::sha1::hash( (char*)&now, sizeof(now) );
    }

    pn.site_ref  = site_ref;

    memset( (char*)pn.nonce.data, 0, sizeof(pn.nonce.data) );

    pn.priority  = 1;
    pn.state     = db::name::private_name::generating;
    
    my->_name_db->store( pn );
  }
                         
  void                   name_service::release_name( const fc::string& name ){
    my->_name_db->remove_private_name( fc::sha1::hash( name.c_str(), name.size() ) );    
  }            
                         
  tn::link   name_service::get_link_for_name( const fc::string& name ) {
    // check the public data base first
    db::name::record rec;
    if( my->_name_db->fetch_record_for_name( name, rec ) ) {
      return rec.site_ref;
    }
    // then check my private database
    db::name::private_name prec;
    if( my->_name_db->fetch( fc::sha1::hash( name.c_str(), name.size() ), prec ) ) {
      return prec.site_ref;
    }
    FC_THROW_MSG( "No site reference registered for name '%s'", name.c_str() );
    return tn::link();
  }
                         
  //name_service::record   name_service::get_record_for_name( const fc::string& name ){
  //  return record(); 
 // }
                         
  fc::signature_t name_service::sign_with_name( const fc::sha1& digest, const fc::string& name ){
    db::name::private_name prec;
    if( my->_name_db->fetch( fc::sha1::hash( name.c_str(), name.size() ), prec ) ) {
       fc::signature_t sig;
       prec.priv_key.sign( digest, sig );
       return sig;
    }
    FC_THROW_MSG( "No private key for name '%s'", name.c_str() );
    return fc::signature_t(); 
  }
                         
  bool            name_service::validate_signature_by_name( const fc::sha1& digest, 
                                                            const fc::signature_t& sig, 
                                                            const fc::string& name ){
    return false;  
  }
                         
  void                   name_service::update_link_for_name( const fc::string& name, const tn::link& val ){
    // then check my private database
    db::name::private_name prec;
    if( my->_name_db->fetch( fc::sha1::hash( name.c_str(), name.size() ), prec ) ) {
       prec.site_ref = val;  
       my->_name_db->store( prec );
       return;
    }
    FC_THROW_MSG( "No private key for name '%s'", name.c_str() );
  }
  void                   name_service::transfer_name( const fc::string& name, const fc::public_key_t& to_key ){
    
  }
                         
  void                   name_service::setProcessingEffort( float e ){
    my->_effort = e; 
  }
  float                  name_service::getProcessingEffort(){
    return my->_effort;
  }
  

  name_service_connection::name_service_connection( tn::udt_channel&& c )
  :_chan(fc::move(c)) {
     _rpc.add_method( broadcast_msg_id,    this, &name_service_connection::broadcast);
     _rpc.add_method( fetch_block_request_id, this, &name_service_connection::fetch_blocks );
     _rpc.add_method( fetch_trxs_request_id,   this, &name_service_connection::fetch_transactions );
     _rpc.add_method( resolve_name_request_id, this, &name_service_connection::resolve_name );
     _rpc.connect(_chan);
  }
  void name_service_connection::broadcast( const tn::broadcast_msg& msg ) {
  }

  /**
   *  Fetch a block by either hash or number (or max)
   *  Return the block and all transactions in the block.
   */
  fetch_block_reply name_service_connection::fetch_blocks( const fetch_block_request& req ) {
    fetch_block_reply reply;
    return reply;
  }

  fetch_trxs_reply name_service_connection::fetch_transactions( const fetch_trxs_request& req ) {
    fetch_trxs_reply reply;
    return reply; 
  }

  /**
   *  Until users have synchronoized with the block chain, they can ask other nodes to resolve the
   *  name for them.
   */
  resolve_name_reply name_service_connection::resolve_name( const resolve_name_request& req ) {
    resolve_name_reply reply;

    return reply;
  }
  

  void name_service::impl::on_new_connection( const tn::channel& c ) {
      name_service_connection::ptr con( new name_service_connection( udt_channel(c,256) ) );
      _subscribers.push_back(con);
  }
  void name_service::impl::download_block( const fc::sha1&, tn::name_block& b ) {
    
  }
  

}
