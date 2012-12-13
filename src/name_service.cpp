#include <tornet/name_service.hpp>
#include <tornet/name_service_messages.hpp>
#include <tornet/node.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/db/name.hpp>
#include <tornet/raw_rpc.hpp>
#include <tornet/tornet_app.hpp>
#include <tornet/name_service_client.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <fc/time.hpp>

#include <tornet/db/name.hpp>
#include <time.h>
#include <stdlib.h>

#include "name_service_impl.hpp"

namespace tn {


  name_service::name_service( const fc::path& sdir, const fc::shared_ptr<tn::node>& n )
  :my(*this) {
    my->_dir  = sdir;
    my->_node = n;

    my->_node->start_service( 53, "named", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
    srand( time(NULL) );

    my->_name_db.reset( new db::name( sdir ) );
    my->_name_db->init();

    //reserve_name( "testname", link() );
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


  void name_service::reserve_name( const fc::string& name, const cafs::link& site_ref ) {
    tn::db::name::record rec;
    tn::db::name::record_key rec_key;
    bool update_domain = false;
    if( my->_name_db->fetch( name, rec ) ) { // if already found
      if( !my->_name_db->fetch( name, rec_key ) )  // and we don't have the key
        FC_THROW_MSG( "Name %s is already registered to someone else", name.c_str() );
      update_domain = true;
    }

    if( name.size() > 63 ) {
      FC_THROW_MSG( "Name '%s' is longer than 63 characters", name.c_str() );
    }
    if( !update_domain ) { 
        db::name::record_key pn;
        fc::generate_keys( pn.pub_key, pn.priv_key ); 

        rec.pub_key = pn.pub_key;
        rec.expires = fc::time_point::now().time_since_epoch().count() / 1000000;
        rec.site_ref  = site_ref;

        my->_name_db->store(name,pn);
        my->_name_db->store(name,rec);
    } else {
      rec.site_ref = site_ref;
      my->_name_db->store(name,rec);
    }
    wlog( "reserved name %s for site %s", name.c_str() , fc::string(site_ref).c_str() );
  }
                         
  void                   name_service::release_name( const fc::string& name ){
  }            
                         
  cafs::link   name_service::get_link_for_name( const fc::string& name ) {
    // check the public data base first
    db::name::record rec;
    if( my->_name_db->fetch( name, rec ) ) {
      return rec.site_ref;
    }
    FC_THROW_MSG( "No site reference registered for name '%s'", name.c_str() );
    return cafs::link();
  }
                         
  //name_service::record   name_service::get_record_for_name( const fc::string& name ){
  //  return record(); 
 // }
                         
  fc::signature_t name_service::sign_with_name( const fc::sha1& digest, const fc::string& name ){
    db::name::record_key prec;
    if( my->_name_db->fetch( name, prec ) ) {
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
                         
  void                   name_service::update_link_for_name( const fc::string& name, const cafs::link& val ){
  #if 0
    // then check my private database
    db::name::record_key prec;
    if( my->_name_db->fetch( name, prec ) ) {
       prec.site_ref = val;  
       my->_name_db->store( prec );
       return;
    }
    #endif
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

  /**
   *  Based upon the level, send the request to 2 nodes at that kbucket level.
   *
   *  Level 0 is 'root', and I am responsible for my half, in addition to forwarding to the other half
   */
  void name_service::publish_name( const publish_name_request& r ) {
    int sent = 0;
    int lup = 0;
    while( sent < 3 ) {
      fc::vector<fc::sha1>  buck = get_node()->get_kbucket( r.level + lup, 3 );
      for( uint32_t i = 0; i < buck.size() && sent < 3; ++i ) {
          auto nsc = tn::get_node()->get_client<name_service_client>(  buck[i] );
          nsc->publish_name( r );
          sent++;
      }
      lup++;
    }
    if( r.level == 0 ) {
      auto r2 = r;
      r2.level++;
      publish_name(r2);
    }
  }
  

  name_service_connection::name_service_connection( name_service& ns, tn::udt_channel&& c )
  :_ns(ns),_chan(fc::move(c)) {
     _rpc.add_method( publish_name_id, this, &name_service_connection::publish_name);
     _rpc.add_method( resolve_name_id, this, &name_service_connection::resolve_name);
     _rpc.connect(_chan);
  }

  /**
   *  Until users have synchronoized with the block chain, they can ask other nodes to resolve the
   *  name for them.
   */
  resolve_name_reply name_service_connection::resolve_name( const resolve_name_request& req ) {
    resolve_name_reply reply;
    try {
        db::name::record rec;
        if( _ns.get_name_db()->fetch( req.name, rec ) ) {
          publish_name_request r;
          r.level = -1;
          r.name     = req.name;
          r.key      = rec.pub_key;
          r.site_ref = rec.site_ref;
          r.expires  = rec.expires;
          r.nonce    = rec.nonce;
          r.sig      = rec.sig;
          reply.record = r;
        }
    } catch (...) {}
    return reply;
  }
  
  publish_name_reply name_service_connection::publish_name( const publish_name_request& req ) {
    publish_name_reply reply;
    reply.status = publish_name_reply::rejected;
    try {
        db::name::record rec;
        if( _ns.get_name_db()->fetch( req.name, rec ) ) {
          reply.status = publish_name_reply::already_reserved;
        } else {
          rec.pub_key  = req.key;
          rec.site_ref = rec.site_ref;
          rec.expires  = rec.expires;
          rec.nonce    = rec.nonce;
          rec.sig      = rec.sig;
          if( _ns.get_name_db()->store( req.name, rec ) ) {
            reply.status = tn::publish_name_reply::ok;
            auto fwd_req = req;
            fwd_req.level++;
            _ns.publish_name( fwd_req );
          }
       }
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    }
    return reply;
  }

  db::name::ptr name_service::get_name_db()const { return my->_name_db; }

  void name_service::impl::on_new_connection( const tn::channel& c ) {
      name_service_connection::ptr con( new name_service_connection( _self, udt_channel(c,256) ) );
      _subscribers.push_back(con);
  }
  

}
