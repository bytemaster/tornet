#include <tornet/error.hpp>
#include <tornet/db/publish.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <db_cxx.h>


namespace tornet { namespace db {
  int compare_i64(Db *db, const Dbt *key1, const Dbt *key2);

  publish::record::record(uint64_t nu, uint64_t ai, uint32_t hc, uint32_t dhc) {
    next_update         = nu;
    access_interval     = ai;
    host_count          = hc;
    desired_host_count  = dhc;
  }

  class publish_private {
    public:
      publish_private( boost::cmt::thread& t, const boost::filesystem::path& envdir );
      ~publish_private(){}

      DbEnv               m_env;
      boost::cmt::thread& m_thread;

      boost::filesystem::path m_envdir;
      Db*                     m_publish_db;
      Db*                     m_next_index_db;
  };
  publish::publish( const boost::filesystem::path& dir )
  :my(0) {
    boost::cmt::thread* t = boost::cmt::thread::create("publish_db"); 
    my = new publish_private( *t, dir );
  }

  publish::~publish() { 
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch ( const boost::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
    }
  }

  publish_private::publish_private( boost::cmt::thread& t,  const boost::filesystem::path& envdir ) 
  :m_thread(t), m_envdir(envdir), m_env(0), m_publish_db(0), m_next_index_db(0)
  {
  }

  int get_publish_next( Db* sdb, const Dbt* key, const Dbt* data, Dbt* skey ) {
    skey->set_data( data->get_data() );
    skey->set_size( sizeof(uint64_t) );
    return 0;
  }

  void publish::init() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&publish::init, this) );
        return;
    }
    try {
      if( !boost::filesystem::exists( my->m_envdir ) ) 
        boost::filesystem::create_directories(my->m_envdir);

      slog( "initializing publish database... %1%", my->m_envdir );
      my->m_env.open( my->m_envdir.native().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %1%\n \t\t%2%", my->m_envdir, e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_publish_db = new Db(&my->m_env, 0);
      my->m_publish_db->set_flags( DB_RECNUM );
      my->m_publish_db->open( NULL, "publish_data", "publish_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening publish_data database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_next_index_db = new Db(&my->m_env, 0);
      my->m_next_index_db->set_flags( DB_DUPSORT );
      my->m_next_index_db->set_bt_compare( &compare_i64 );
      my->m_next_index_db->open( NULL, "publish_next_index", "publish_next_index", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0 );
      my->m_publish_db->associate( NULL, my->m_next_index_db, get_publish_next, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening publish_next_index database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }
  }

  void publish::close() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&publish::close, this) );
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
      BOOST_THROW_EXCEPTION(e);
    }
  }
  void publish::sync() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&publish::sync, this ) );
        return;
    }
    my->m_next_index_db->sync(0);
    my->m_publish_db->sync(0);
  }

  uint32_t publish::count() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<uint32_t>( boost::bind(&publish::count, this ) );
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

  bool publish::fetch( const scrypt::sha1& id, publish::record& r ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&publish::fetch, this, id, boost::ref(r)) );
    }
    try {
    scrypt::sha1 dist = id;

    Dbt key;
    key.set_data( (char*)dist.hash);
    key.set_ulen( sizeof(dist.hash) );
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_size( sizeof(r) );
    val.set_ulen( sizeof(r) );
    val.set_flags( DB_DBT_USERMEM );

    return DB_NOTFOUND != my->m_publish_db->get( 0, &key, &val, 0 );
    } catch ( const DbException& e ) {
      elog( "%1%", boost::diagnostic_information( e ) );
      return false;
    }
  }

  bool publish::exists( const scrypt::sha1& id ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&publish::exists, this, id) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id;

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_flags( DB_DBT_USERMEM );
    val.set_flags( DB_DBT_PARTIAL );
    val.set_dlen(0); 

    return DB_NOTFOUND != my->m_publish_db->get( 0, &key, &val, 0 );
  }

  bool publish::store( const scrypt::sha1& id, const record& m ) {
    slog("storing %1%", id );
    scrypt::sha1 dist = id;

    bool ex = exists(id);

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    try {
        Dbt key(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );
        Dbt val( (char*)&m, sizeof(m));
        val.set_flags( DB_DBT_USERMEM );
        my->m_publish_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      TORNET_THROW( "%1%", %e.what() );
    }

    Dbc*       cur;
    Dbt ignore_val; 
    ignore_val.set_dlen(0); 
    ignore_val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM );

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_ulen(sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    slog( "");
    my->m_publish_db->cursor( NULL, &cur, 0 );
    if( DB_NOTFOUND == cur->get( &key, &ignore_val, DB_FIRST ) ) {
      slog( "not found" );
      cur->close();
      TORNET_THROW( "couldn't find key we just put in db" ); 
    }
    db_recno_t idx;
    Dbt        idx_val(&idx, sizeof(idx) );
    idx_val.set_flags( DB_DBT_USERMEM );
    idx_val.set_ulen(sizeof(idx) );

    Dbt ignore_key;
    cur->get(  &ignore_key, &idx_val, DB_GET_RECNO );
    slog( "inserted/updated record %1%", idx );
    cur->close();
    if( !ex )
      record_inserted(idx);
    else 
      record_changed(idx);
    return true;
  }

  bool publish::fetch_next( scrypt::sha1& id, publish::record& m ) {
      uint64_t next = 0;
      Dbt skey((char*)&next, sizeof(next));
      skey.set_flags( DB_DBT_USERMEM );

      Dbt pkey( (char*)id.hash, sizeof(id) );
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

  bool publish::fetch_index( uint32_t recnum, scrypt::sha1& id, record& m) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&publish::fetch_index, this, recnum, boost::ref(id), boost::ref(m) ) );
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
      memcpy((char*)id.hash, key.get_data(), key.get_size() );
      memcpy( &m, val.get_data(), val.get_size() );
      return true;
    }
    wlog( "Unable to find recno %1%", recnum );
    return false;
  }



} }
