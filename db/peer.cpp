#include <tornet/error.hpp>
#include <tornet/db/peer.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <db_cxx.h>


namespace tornet { namespace db {

  peer::record::record() {
    memset( this, 0, sizeof(record) );
  }


  class peer_private {
    public:
      peer_private( boost::cmt::thread& t, const scrypt::sha1& id, const boost::filesystem::path& envdir );
      ~peer_private(){}

      DbEnv               m_env;
      boost::cmt::thread& m_thread;

      scrypt::sha1            m_node_id;
      boost::filesystem::path m_envdir;
      Db*                     m_peer_db;
      Db*                     m_ep_index_db;
  };
  peer::peer( const scrypt::sha1& node_id, const boost::filesystem::path& dir )
  :my(0) {
    boost::cmt::thread* t = boost::cmt::thread::create("peer_db"); 
    my = new peer_private( *t, node_id, dir );
  }

  peer::~peer() { 
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch ( const boost::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
    }
  }

  peer_private::peer_private( boost::cmt::thread& t, const scrypt::sha1& node_id, const boost::filesystem::path& envdir ) 
  :m_thread(t), m_node_id(node_id), m_envdir(envdir), m_env(0), m_peer_db(0), m_ep_index_db(0)
  {
  }

  int get_peer_ep( Db* sdb, const Dbt* key, const Dbt* data, Dbt* skey ) {
    skey->set_data( data->get_data() );
    skey->set_size( 6 );
    return 0;
  }

  void peer::init() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&peer::init, this) );
        return;
    }
    try {
      if( !boost::filesystem::exists( my->m_envdir ) ) 
        boost::filesystem::create_directories(my->m_envdir);

      slog( "initializing peer database... %1%", my->m_envdir );
      my->m_env.open( my->m_envdir.native().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %1%\n \t\t%2%", my->m_envdir, e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_peer_db = new Db(&my->m_env, 0);
      my->m_peer_db->set_flags( DB_RECNUM );
      my->m_peer_db->open( NULL, "peer_data", "peer_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening peer_data database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_ep_index_db = new Db(&my->m_env, 0);
      my->m_ep_index_db->set_flags( DB_DUPSORT );
      my->m_ep_index_db->open( NULL, "peer_ep_index", "peer_ep_index", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0 );
      my->m_peer_db->associate( NULL, my->m_ep_index_db, get_peer_ep, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening peer_ep_index database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }
  }

  void peer::close() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&peer::close, this) );
        return;
    }
    try {
        if( my->m_peer_db ){
          slog( "closing peer db" );
          my->m_peer_db->close(0);    
          delete my->m_peer_db;
          my->m_peer_db = 0;
        }
        if( my->m_ep_index_db ) {
          slog( "closing peer ep_index db" );
          my->m_ep_index_db->close(0);    
          delete my->m_ep_index_db;
          my->m_ep_index_db = 0;
        }
        slog( "closing environment" );
        my->m_env.close(0);
    } catch ( const DbException& e ) {
      BOOST_THROW_EXCEPTION(e);
    }
  }
  void peer::sync() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&peer::sync, this ) );
        return;
    }
    my->m_ep_index_db->sync(0);
    my->m_peer_db->sync(0);
  }

  uint32_t peer::count() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<uint32_t>( boost::bind(&peer::count, this ) );
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

    my->m_peer_db->cursor( NULL, &cur, 0 );
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

  bool peer::fetch( const scrypt::sha1& id, peer::record& r ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&peer::fetch, this, id, boost::ref(r)) );
    }
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val( (char*)&r, sizeof(r) );
    val.set_flags( DB_DBT_USERMEM );

    return DB_NOTFOUND != my->m_peer_db->get( 0, &key, &val, 0 );
  }

  bool peer::exists( const scrypt::sha1& id ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&peer::exists, this, id) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_flags( DB_DBT_USERMEM );
    val.set_flags( DB_DBT_PARTIAL );
    val.set_dlen(0); 

    return DB_NOTFOUND != my->m_peer_db->get( 0, &key, &val, 0 );
  }

  bool peer::store( const scrypt::sha1& id, const record& m ) {
    slog("storing %1%", id );
    scrypt::sha1 check;
    scrypt::sha1_hash( check, m.public_key, sizeof(m.public_key) );
    if( check != id ) {
      TORNET_THROW( "sha1(data) does not match given id" );
    }
    scrypt::sha1 dist = id ^ my->m_node_id;

    bool ex = exists(id);

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    try {
        Dbt key(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );
        Dbt val( (char*)&m, sizeof(m));
        val.set_flags( DB_DBT_USERMEM );
        my->m_peer_db->put( txn, &key, &val, 0 );
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
    my->m_peer_db->cursor( NULL, &cur, 0 );
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

  bool peer::fetch_by_endpoint( const peer::endpoint& ep, scrypt::sha1& id, peer::record& m ) {
      Dbt skey((char*)&ep, 6);
      skey.set_flags( DB_DBT_USERMEM );

      Dbt pkey( (char*)id.hash, sizeof(id) );
      pkey.set_ulen( sizeof(id) );
      pkey.set_flags( DB_DBT_USERMEM );

      Dbt pdata( (char*)&m, sizeof(m) );
      pdata.set_ulen( sizeof(m) );
      pdata.set_flags( DB_DBT_USERMEM );
    
      int rtn = my->m_ep_index_db->pget( NULL, &skey, &pkey, &pdata, 0 );
      if( rtn != DB_NOTFOUND )  {
        id = id ^ my->m_node_id;
        return true;
      }
      return false;
  }




} }
