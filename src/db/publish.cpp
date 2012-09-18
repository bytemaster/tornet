#include <tornet/db/publish.hpp>
#include <fc/thread.hpp>
#include <fc/filesystem.hpp>
#include <fc/log.hpp>
#include <db_cxx.h>
#include <string.h>


namespace tn { namespace db {
  int compare_i64(Db *db, const Dbt *key1, const Dbt *key2);

  publish::record::record(uint64_t nu, uint64_t ai, uint32_t hc, uint32_t dhc) {
    next_update         = nu;
    access_interval     = ai;
    host_count          = hc;
    desired_host_count  = dhc;
  }

  class publish_private {
    public:
      publish_private(  const fc::path& envdir );
      ~publish_private(){}

      fc::thread              m_thread;
      fc::path                m_envdir;
      DbEnv                   m_env;

      Db*                     m_publish_db;
      Db*                     m_next_index_db;
  };
  publish::publish( const fc::path& dir )
  :my(0) {
    my = new publish_private( dir );
  }

  publish::~publish() { 
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    }
  }

  publish_private::publish_private(  const fc::path& envdir ) 
  :m_thread("db::publish"), m_envdir(envdir), m_env(0), m_publish_db(0), m_next_index_db(0)
  {
  }

  int get_publish_next( Db* sdb, const Dbt* key, const Dbt* data, Dbt* skey ) {
    skey->set_data( data->get_data() );
    skey->set_size( sizeof(uint64_t) );
    return 0;
  }

  void publish::init() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){ init(); } ).wait();
        return;
    }
    try {
      if( !fc::exists( my->m_envdir ) ) 
        fc::create_directories(my->m_envdir);

      slog( "initializing publish database... %s", my->m_envdir.string().c_str() );
      my->m_env.open( my->m_envdir.string().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %s\n \t\t%s", my->m_envdir.string().c_str(), e.what() );
      throw;
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception(fc::current_exception());
    }

    try { 
      my->m_publish_db = new Db(&my->m_env, 0);
      my->m_publish_db->set_flags( DB_RECNUM );
      my->m_publish_db->open( NULL, "publish_data", "publish_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening publish_data database: %s", e.what() );
      throw;
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception(fc::current_exception());
    }

    try { 
      my->m_next_index_db = new Db(&my->m_env, 0);
      my->m_next_index_db->set_flags( DB_DUPSORT );
      my->m_next_index_db->set_bt_compare( &compare_i64 );
      my->m_next_index_db->open( NULL, "publish_next_index", "publish_next_index", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0 );
      my->m_publish_db->associate( NULL, my->m_next_index_db, get_publish_next, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening publish_next_index database: %s", e.what() );
      FC_THROW_MSG( "%s", e.what() );
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception(fc::current_exception());
    }
  }

  void publish::close() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){close();} ).wait();
        return;
    }
    try {
        if( my->m_publish_db ){
          slog( "closing publish db" );
          my->m_publish_db->close(0);    
          delete my->m_publish_db;
          my->m_publish_db = 0;
        }
        if( my->m_next_index_db ) {
          slog( "closing publish next_index db" );
          my->m_next_index_db->close(0);    
          delete my->m_next_index_db;
          my->m_next_index_db = 0;
        }
        slog( "closing environment" );
        my->m_env.close(0);
    } catch ( const DbException& e ) {
      FC_THROW_MSG("%s", e.what());
    }
  }
  void publish::sync() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){sync();} ).wait();
        return;
    }
    my->m_next_index_db->sync(0);
    my->m_publish_db->sync(0);
  }

  uint32_t publish::count() {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [this](){return count();} ).wait();
    }
    db_recno_t num;
    Dbc*       cur;
    Dbt key;
    Dbt val(&num, sizeof(num) );
    val.set_ulen(sizeof(num));
    val.set_flags( DB_DBT_USERMEM );

    Dbt ignore_val; 
    ignore_val.set_flags( DB_DBT_USERMEM | DB_DBT_PARTIAL);
    ignore_val.set_dlen(0); 

    my->m_publish_db->cursor( NULL, &cur, 0 );
    int rtn = cur->get( &key, &ignore_val, DB_LAST );
    if( rtn == DB_NOTFOUND ) {
      slog( "Not found" );
      cur->close();
      return 0;
    }
    rtn = cur->get( &key, &val, DB_GET_RECNO );
    cur->close();
    return num;
  }

  bool publish::fetch( const fc::sha1& id, publish::record& r ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return fetch(id,r); } ).wait();
    }
    try {
    fc::sha1 dist = id;

    Dbt key;
    key.set_data( (char*)dist.data());
    key.set_ulen( sizeof(dist.data()) );
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_size( sizeof(r) );
    val.set_ulen( sizeof(r) );
    val.set_flags( DB_DBT_USERMEM );

    return DB_NOTFOUND != my->m_publish_db->get( 0, &key, &val, 0 );
    } catch ( const DbException& e ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool publish::exists( const fc::sha1& id ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return exists(id); } ).wait();
    }
    fc::sha1 dist = id;

    Dbt key(dist.data(),sizeof(dist.data()));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_flags( DB_DBT_USERMEM );
    val.set_flags( DB_DBT_PARTIAL );
    val.set_dlen(0); 

    return DB_NOTFOUND != my->m_publish_db->get( 0, &key, &val, 0 );
  }

  bool publish::store( const fc::sha1& id, const record& m ) {
    slog("storing %1%", id );
    fc::sha1 dist = id;

    bool ex = exists(id);

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    try {
        Dbt key(dist.data(),sizeof(dist.data()));
        key.set_flags( DB_DBT_USERMEM );
        Dbt val( (char*)&m, sizeof(m));
        val.set_flags( DB_DBT_USERMEM );
        my->m_publish_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }

    Dbc*       cur;
    Dbt ignore_val; 
    ignore_val.set_dlen(0); 
    ignore_val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM );

    Dbt key(dist.data(),sizeof(dist.data()));
    key.set_ulen(sizeof(dist.data()));
    key.set_flags( DB_DBT_USERMEM );

    slog( "");
    my->m_publish_db->cursor( NULL, &cur, 0 );
    if( DB_NOTFOUND == cur->get( &key, &ignore_val, DB_FIRST ) ) {
      slog( "not found" );
      cur->close();
      FC_THROW( "couldn't find key we just put in db" ); 
    }
    db_recno_t idx;
    Dbt        idx_val(&idx, sizeof(idx) );
    idx_val.set_flags( DB_DBT_USERMEM );
    idx_val.set_ulen(sizeof(idx) );

    Dbt ignore_key;
    cur->get(  &ignore_key, &idx_val, DB_GET_RECNO );
    slog( "inserted/updated record %lld", idx );
    cur->close();
    /*
    if( !ex )
      record_inserted(idx);
    else 
      record_changed(idx);
      */
    return true;
  }

  bool publish::fetch_next( fc::sha1& id, publish::record& m ) {
      uint64_t next = 0;
      Dbt skey((char*)&next, sizeof(next));
      skey.set_flags( DB_DBT_USERMEM );
      skey.set_ulen(sizeof(next) );

      Dbt pkey( id.data(), sizeof(id) );
      pkey.set_ulen( sizeof(id) );
      pkey.set_flags( DB_DBT_USERMEM );

      Dbt pdata( (char*)&m, sizeof(m) );
      pdata.set_ulen( sizeof(m) );
      pdata.set_flags( DB_DBT_USERMEM );

      Dbc*       cur;
      my->m_next_index_db->cursor( NULL, &cur, 0 );
    
      int rtn = cur->pget( &skey, &pkey, &pdata, DB_FIRST );

      cur->close();
      if( rtn != DB_NOTFOUND )  {
        return true;
      }
      return false;
  }

  bool publish::fetch_index( uint32_t recnum, fc::sha1& id, record& m) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return fetch_index(recnum, id, m ); } ).wait();
    }
    db_recno_t rn(recnum);
    Dbt key( &rn, sizeof(rn) );
    Dbt val( &rn, sizeof(rn) );

    int rtn = my->m_publish_db->get( 0, &key, &val, DB_SET_RECNO );
    if( rtn == EINVAL ) {
      elog( "Invalid DB Query" );
      return false;
    }
    if( rtn != DB_NOTFOUND ) {
      memcpy(id.data(), key.get_data(), key.get_size() );
      memcpy( &m, val.get_data(), val.get_size() );
      return true;
    }
    wlog( "Unable to find recno %1%", recnum );
    return false;
  }



} }
