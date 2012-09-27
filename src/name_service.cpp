#include <tornet/name_service.hpp>
#include <tornet/node.hpp>
#include <tornet/udt_channel.hpp>
#include <tornet/db/name.hpp>
#include <tornet/raw_rpc.hpp>
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
      raw_rpc     _rpc;
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


  /**
   *  Perform a KAD search for a RANDOM ID and ask each node for the
   *  head block.  The head-block selected will be the one with the
   *  longest chain. 
   *
   *  Then after downloading the head block, I will randomly select
   *  nodes from my contact list and request all of the transactions
   *  for the head node.
   *
   *  Then I will randomly pick a node to request the previous block and
   *  its transactions from and I wll repeat this process until
   *  I come to the gensis block or a block that I already know.
   *
   *  After downloading all transactions and blocks, then I will replay
   *  the transactions and validate the results updating the name_record
   *  database in the process.
   *
   *  After everything has been validated and my 'root node' is the same
   *  as the rest of the network then I can start generating my own 
   *  transactions.
   *
   *  Downloading is a long-running process with many intermediate states 
   *  and is therefore maintained in its own state object.
   *
   *  Once connected to the network, this node will be receiving live 
   *  transaction updates.  These transactions get logged and broadcast.
   */
  void name_service::download_block_chain() {

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
  

  name_service_connection::name_service_connection( tn::udt_channel&& c )
  :_chan(fc::move(c)) {
     _rpc.add_method( broadcast_id,    this, &name_service_connection::broadcast_msg );
     _rpc.add_method( fetch_blocks_id, this, &name_service_connection::fetch_blocks );
     _rpc.add_method( fetch_trxs_id,   this, &name_service_connection::fetch_transactions );
     _rpc.add_method( resolve_name_id, this, &name_service_connection::resolve_name );
     _rpc.connect(_chan);
  }
  void name_service_connection::broadcast( const broadcast_msg& msg ) {
  }

  /**
   *  Fetch a block by either hash or number (or max)
   *  Return the block and all transactions in the block.
   */
  fetch_block_reply name_service_connection::fetch_blocks( const fetch_block_request& req ) {
    fetch_block_reply reply;
    return reply;
  }

  fetch_transactions_reply name_service_connection::fetch_transactions( const fetch_transactions_request& req ) {
    fetch_transactions_reply reply;
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
  

}
