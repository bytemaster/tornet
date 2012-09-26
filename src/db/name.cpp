#include <tornet/db/name.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/thread.hpp>
#include <fc/raw.hpp>

#include <db_cxx.h>

namespace tn { namespace db {

  class name::impl {
    public:
      impl()
      :_thread("db::name"),_env(0){}
       
      fc::thread          _thread;
      fc::path            _envdir;
      DbEnv               _env;

      Db*                 _trx_db;        // index trx by their hash
      Db*                 _trx_name_idx;  // index trx by hash of name (or name+nonce)

      Db*                 _block_db;      // index blocks by their hash
      Db*                 _block_num_idx; // index blocks by their number

      Db*                 _record_db;     // most up-to-date state given known trx
      Db*                 _private_db;    // private keys for my names
  };

  
  name::name( const fc::path& dir ){
    my->_envdir = dir;
  }

  name::~name(){
  }

  int get_name_hash( Db* secondary, const Dbt* pkey, const Dbt* pdata, Dbt* skey ) {
    auto head = (tn::name_trx_header*)(pdata->get_data());
    switch( head->type ) {
      case name_reserve_trx::id: {
        auto rtrx = (tn::name_reserve_trx*)(pdata->get_data());
        skey->set_data( &rtrx->res_id ); 
        skey->set_size( sizeof( rtrx->res_id) );
      } break;
      case name_publish_trx::id: {
        auto rtrx = (tn::name_publish_trx*)(pdata->get_data());
        skey->set_data ( new fc::sha1( fc::sha1::hash( rtrx->name.data, strlen(rtrx->name.data)  ) ) );
        skey->set_size ( sizeof(fc::sha1) );
        skey->set_flags( DB_DBT_APPMALLOC );
      } break;
      case name_update_trx::id: {
        auto rtrx = (tn::name_update_trx*)(pdata->get_data());
        skey->set_data( &rtrx->name_id ); 
        skey->set_size( sizeof( rtrx->name_id) );
      } break;
      case name_transfer_trx::id: {
        auto rtrx = (tn::name_transfer_trx*)(pdata->get_data());
        skey->set_data( &rtrx->name_id ); 
        skey->set_size( sizeof( rtrx->name_id) );
      } break;
    }
    return 0;
  }
  int compare_sha1(Db *db, const Dbt *key1, const Dbt *key2) {
    fc::sha1* _k1 = (fc::sha1*)(key1->get_data());
    fc::sha1* _k2 = (fc::sha1*)(key2->get_data());
    if( *_k1 > *_k2 ) return 1;
    if( *_k1 == *_k2 ) return 0;
    return -1;
  }

  void name::init(){
    if( !my->_thread.is_current() ) {
        my->_thread.async( [this](){ init(); } ).wait();
        return;
    }
    if( !fc::exists( my->_envdir ) ) 
      fc::create_directories(my->_envdir);
     
    try {
      my->_env.open( my->_envdir.string().c_str(), 
                  DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %s\n \t\t%s", my->_envdir.string().c_str(), e.what() );
      FC_THROW( e );
    }

    try { 
      slog( "initialize database %s/trx_db", my->_envdir.string().c_str() );
      my->_trx_db = new Db(&my->_env, 0);
      my->_trx_db->open( NULL, "trx_data", "trx_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );

      my->_trx_name_idx = new Db(&my->_env, 0 );
      my->_trx_name_idx->set_flags( DB_DUP | DB_DUPSORT );
      my->_trx_name_idx->set_bt_compare( &compare_sha1 );
      my->_trx_name_idx->open( NULL, "trx_name_idx", "trx_name_idx", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0);//0600 );
      my->_trx_db->associate( 0, my->_trx_name_idx, get_name_hash, 0 );

    } catch( const DbException& e ) {
      elog( "Error opening trx_data database: %s", e.what() );
      FC_THROW( e );
    } 
    try { 
      slog( "initialize database %s/priv_db", my->_envdir.string().c_str() );
      my->_private_db = new Db(&my->_env, 0);
      my->_private_db->open( NULL, "priv_data", "priv_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening trx_data database: %s", e.what() );
      FC_THROW( e );
    } 

  }

  void name::close(){
    if( !my->_thread.is_current() ) {
        my->_thread.async( [this](){ close(); } ).wait();
        return;
    }
    try {
      if( nullptr != my->_trx_db ) {
        my->_trx_db->close(0);
        delete my->_trx_db;
        my->_trx_db = nullptr;
      }
      if( nullptr != my->_trx_name_idx ) {
        my->_trx_name_idx->close(0);
        delete my->_trx_name_idx;
        my->_trx_name_idx = nullptr;
      }
    } catch ( const DbException& e ) {
      FC_THROW_MSG("%s", e.what());
    }
  }

  void name::sync(){
    if( !my->_thread.is_current() ) {
        my->_thread.async( [this](){ sync(); } ).wait();
        return;
    }
    my->_trx_db->sync(0);
    my->_trx_name_idx->sync(0);
  }



  uint32_t name::num_private_names(){
  }

  bool     name::fetch_private_name( uint32_t recnum, private_name& ){
  }


  bool     name::fetch_record_for_name( const fc::string& name, record& r ){
  }

  bool     name::fetch_record_for_public_key( const fc::sha1& pk_sha1, record& r ){
  }

  bool     name::fetch_reservation_for_hash( const fc::sha1& res_id, name_reserve_trx& ){
  }

           
  bool     name::store( const private_name& pn ) {
    fc::vector<char> dat = fc::raw::pack(pn);
    fc::sha1 id = fc::sha1::hash( pn.name.data, strlen(pn.name.data) );

    Dbt key(id.data(),sizeof(id));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val( dat.data(), dat.size() );
    val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_private_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }

  bool     name::remove_private_name( const fc::sha1& name_id ) {
    return false;
  }
  bool     name::fetch( const fc::sha1& id, private_name& pn ) {
    Dbt key( id.data(), sizeof(id) );
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_private_db->get( txn, &key, &val, 0 );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    pn = fc::raw::unpack<private_name>( (char*)val.get_data(), val.get_size() );

    return true;
  }

  bool     name::store( const name_reserve_trx& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(trx); } ).wait();
    }
    fc::sha1::encoder enc;
    fc::raw::pack( enc, trx );
    fc::sha1 id = enc.result();
    Dbt key( id.data(), sizeof(id) );

    key.set_flags( DB_DBT_USERMEM );
    Dbt val( (char*)&trx, sizeof(trx));
    val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_trx_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }

  bool     name::store( const name_publish_trx& trx ){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(trx); } ).wait();
    }
    fc::sha1::encoder enc;
    fc::raw::pack( enc, trx );
    fc::sha1 id = enc.result();
    Dbt key( id.data(), sizeof(id) );

    key.set_flags( DB_DBT_USERMEM );
    Dbt val( (char*)&trx, sizeof(trx));
    val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_trx_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }

  bool     name::store( const name_update_trx& trx ){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(trx); } ).wait();
    }
    fc::sha1::encoder enc;
    fc::raw::pack( enc, trx );
    fc::sha1 id = enc.result();
    Dbt key( id.data(), sizeof(id) );

    key.set_flags( DB_DBT_USERMEM );
    Dbt val( (char*)&trx, sizeof(trx));
    val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_trx_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }

  bool     name::store( const name_transfer_trx& trx ){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(trx); } ).wait();
    }
    fc::sha1::encoder enc;
    fc::raw::pack( enc, trx );
    fc::sha1 id = enc.result();
    Dbt key( id.data(), sizeof(id) );

    key.set_flags( DB_DBT_USERMEM );
    Dbt val( (char*)&trx, sizeof(trx));
    val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        my->_trx_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }
  bool     name::fetch( const fc::sha1& id, name_trx_header& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(id,trx); } ).wait();
    }
    try {
      Dbt key;
      key.set_data( id.data() );
      key.set_ulen( sizeof(id) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(trx) );
      val.set_ulen( sizeof(trx) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->_trx_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool     name::fetch( const fc::sha1& id, name_reserve_trx& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(id,trx); } ).wait();
    }
    try {
      Dbt key;
      key.set_data( id.data() );
      key.set_ulen( sizeof(id) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(trx) );
      val.set_ulen( sizeof(trx) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->_trx_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool     name::fetch( const fc::sha1& id, name_publish_trx& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(id,trx); } ).wait();
    }
    try {
      Dbt key;
      key.set_data( id.data() );
      key.set_ulen( sizeof(id) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(trx) );
      val.set_ulen( sizeof(trx) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->_trx_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool     name::fetch( const fc::sha1& id, name_update_trx& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(id,trx); } ).wait();
    }
    try {
      Dbt key;
      key.set_data( id.data() );
      key.set_ulen( sizeof(id) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(trx) );
      val.set_ulen( sizeof(trx) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->_trx_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool     name::fetch( const fc::sha1& id, name_transfer_trx& trx){
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(id,trx); } ).wait();
    }
    try {
      Dbt key;
      key.set_data( id.data() );
      key.set_ulen( sizeof(id) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(trx) );
      val.set_ulen( sizeof(trx) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->_trx_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

           
  bool     name::store( const name_block& b ){
  }

  bool     name::fetch( const fc::sha1& id, name_block& b ){
  }

           
  bool     name::fetch_trx_by_index( uint32_t recnum, fc::sha1& id, name_trx_header&  ){
  }

  bool     name::fetch_block_by_index( uint32_t recnum, fc::sha1& id, name_block&  ){
  }

  uint64_t name::max_block_num(){
  }

  uint32_t name::num_blocks(){
  }

  uint32_t name::num_trx(){
  }


  // return the hash of all blocks with a given number
  fc::vector<fc::sha1> name::fetch_blocks_with_number( uint64_t block_num ){
  }

  bool                 name::fetch_block_by_num( uint32_t recnum, fc::sha1& id, name_block&  ){
  }


  bool name::remove_trx( const fc::sha1& trx_id ){
  }

  bool name::remove_block( const fc::sha1& block_id ){
  }


} } 
