#include <tornet/name_service.hpp>
#include <tornet/node.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/db/name.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <fc/time.hpp>

#include <time.h>
#include <stdlib.h>

namespace tn {
  class name_service_connection : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<name_service_connection> ptr;

      name_service_connection( udt_channel&& c );

    protected:
      virtual ~name_service_connection(){}
      udt_channel _chan;
  };

  class name_service::impl {
    public:
      float               _effort;      
      fc::path            _dir;
      tn::node::ptr       _node;
      tn::db::name::ptr   _name_db;

      // nodes subscribed to us
      fc::vector<name_service_connection::ptr> _subscribers;

      // nodes we are subscribed to
      fc::vector<name_service_connection::ptr> _sources;

      void subscribe_to_network();
      void on_new_connection( const channel& c );
  };


  name_service::name_service( const fc::path& sdir, const fc::shared_ptr<tn::node>& n ) {
    my->_dir  = sdir;
    my->_node = n;

    my->_node->start_service( 53, "named", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
    srand( time(NULL) );

    my->_name_db.reset( new db::name( sdir ) );
    my->_name_db->init();
  }

  name_service::~name_service() {
    my->_node->close_service( 53 );
  }

  
  fc::vector<fc::string> name_service::get_reserved_names()const{
    fc::vector<fc::string> names;
    return names;
  }

  void name_service::reserve_name( const fc::string& name, const fc::sha1& value_id, const fc::sha1& key ){
    
    tn::db::name::record rec;
    if( my->_name_db->fetch_record_for_name( name, rec ) ) {
      FC_THROW_MSG( "Name %s is already registered", name.c_str() );
    }

    if( name.size() > 127 ) {
      FC_THROW_MSG( "Name '%s' is longer than 127 characters", name.c_str() );
    }

    db::name::private_name pn;
    fc::generate_keys( pn.pub_key, pn.priv_key ); 
    memset( pn.name.data, 0, sizeof(pn.name) );
    memcpy( pn.name.data, name.c_str(), name.size() + 1 );
    uint64_t now = rand() + fc::time_point::now().time_since_epoch().count();
    pn.rand = fc::sha1::hash( (char*)&now, sizeof(now) );

    pn.value_id  = value_id;
    pn.value_key = key;
    memset( (char*)pn.nonce.data, 0, sizeof(pn.nonce.data) );
    pn.state     = db::name::private_name::generating;

    my->_name_db->store( pn );
  }
                         
  void                   name_service::release_name( const fc::string& name ){
    my->_name_db->remove_private_name( fc::sha1::hash( name.c_str(), name.size() ) );    
  }            
                         
  void   name_service::get_value_for_name( const fc::string& name, fc::sha1& val_id, fc::sha1& key_id ){

  }
                         
  //name_service::record   name_service::get_record_for_name( const fc::string& name ){
  //  return record(); 
 // }
                         
  fc::sha1               name_service::sign_with_name( const fc::sha1& digest, const fc::string& name ){
    return fc::sha1(); 
  }
                         
  bool                   name_service::validate_signature_by_name( const fc::sha1& digest, const fc::signature_t& sig, 
                                                                                                const fc::string& name ){
    return false;  
  }
                         
  void                   name_service::update_value_for_name( const fc::string& name, const fc::sha1& val ){
    
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
   *  Attempt to find N independent nodes and subscribe to their stream. All incoming
   *  messages should come from these three nodes and all three nodes should be forwarding
   *  the same messages... if not then someone 'filtered' a message... the node should be
   *  flagged and a new node found.  The nodes producing the most traffic are most 
   *  trustworthy. 
   *
   *  We know a message has throughly 'propagated' when a full circle is made and all of
   *  the nodes we subscribe to have echoed the message on to us.
   *
   *  Each node sends every message M times and receives it N times where M is the
   *  number of subscribers and N is the number of nodes this node is subscribed to.
   */
  void name_service::impl::subscribe_to_network() {

  }

  name_service_connection::name_service_connection( tn::udt_channel&& c )
  :_chan(fc::move(c)) {
  }
  

  void name_service::impl::on_new_connection( const tn::channel& c ) {
      name_service_connection::ptr con( new name_service_connection( udt_channel(c,256) ) );
      _subscribers.push_back(con);
  }
  

}
