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

      Db*                 _rec_db;     // index records by their name
      Db*                 _private_db; // private keys for my names
  };

  
  name::name( const fc::path& dir ){
    my->_envdir = dir;
  }

  name::~name(){
  }
#if 0
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
#endif
  void name::init(){
    if( !my->_thread.is_current() ) {
        my->_thread.async( [this](){ init(); } ).wait();
        return;
    }
    if( !fc::exists( my->_envdir ) ) 
      fc::create_directories(my->_envdir);
     
    try {
      slog( "initializing name database... %s", my->_envdir.string().c_str() );
      my->_env.open( my->_envdir.string().c_str(), 
                  DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %s\n \t\t%s", my->_envdir.string().c_str(), e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }

    try { 
      slog( "initialize database %s/trx_db", my->_envdir.string().c_str() );
      my->_rec_db = new Db(&my->_env, 0);
      my->_rec_db->open( NULL, "name_data", "name_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );

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
      if( nullptr != my->_rec_db ) {
      slog("close namedb" );
        my->_rec_db->close(0);
        delete my->_rec_db;
        my->_rec_db = nullptr;
      }
    } catch ( const DbException& e ) {
      FC_THROW_MSG("%s", e.what());
    }
    try {
      if( nullptr != my->_private_db ) {
      slog("close privdb" );
        my->_private_db->close(0);
        delete my->_private_db;
        my->_private_db = nullptr;
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
    slog( "sync" );
    my->_rec_db->sync(0);
    my->_private_db->sync(0);
  }

  bool  name::fetch( const fc::string& n, record& r ) {
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(n,r); } ).wait();
    }
    slog( "%s rec", n.c_str() );
    Dbt key;
    key.set_flags( DB_DBT_USERMEM );
    key.set_ulen( n.size() );
    key.set_size( n.size() );
    key.set_data( (char*)n.c_str() );

    Dbt val;
    val.set_flags( DB_DBT_USERMEM );
    val.set_ulen( sizeof(r) );
    val.set_size( sizeof(r) );
    val.set_data( &r );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        slog( "get!" );
        int r = DB_NOTFOUND != my->_rec_db->get( txn, &key, &val, 0 );
        txn->abort();
        return r;
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return false;
  }

  bool  name::fetch( const fc::string& n, record_key& r ) {
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return fetch(n,r); } ).wait();
    }
    slog( "%s key", n.c_str() );
    Dbt key( (char*)n.c_str(), n.size() );
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);
    
    try {
        if( DB_NOTFOUND != my->_private_db->get( txn, &key, &val, 0 ) ) {
            r = fc::raw::unpack<record_key>( (char*)val.get_data(), val.get_size() );
            txn->abort();
            return true;
        }
        txn->abort();
        return false;
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }
           
  bool     name::store( const fc::string& n, const record_key& r ) {
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(n,r); } ).wait();
    }
    slog( "%s key", n.c_str() );
    Dbt key( (char*)n.c_str(), n.size() );
    //key.set_flags( DB_DBT_USERMEM );

    auto dat = fc::raw::pack(r);
    Dbt val( dat.data(), dat.size() );
    //val.set_flags( DB_DBT_USERMEM );

    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        slog( "put" );
        my->_private_db->put( txn, &key, &val, 0 );
        slog( "commit" );
        txn->commit( DB_TXN_WRITE_NOSYNC );
        slog( "done" );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }
  
  bool     name::store( const fc::string& n, const record& r ) {
    if( !my->_thread.is_current() ) {
        return my->_thread.async( [&](){ return store(n,r); } ).wait();
    }
    slog( "store %s", n.c_str() );
    Dbt key;//( (char*)n.c_str(), n.size() );
        key.set_data( (char*)n.c_str() );
        key.set_size( n.size() );
        key.set_flags( DB_DBT_USERMEM );
        key.set_ulen( n.size() );

    Dbt val;
        val.set_data( (void*)&r );
        val.set_size( sizeof(r) );
        val.set_flags( DB_DBT_USERMEM );
        val.set_ulen( sizeof(r) );


    DbTxn * txn=NULL;
    my->_env.txn_begin(NULL, &txn, 0);

    try {
        slog( "rec put" );
        my->_rec_db->put( txn, &key, &val, 0 );
        slog( "rec commit" );
        txn->commit( DB_TXN_WRITE_NOSYNC );
        slog( "rec done" );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }
    return true;
  }

} } 
