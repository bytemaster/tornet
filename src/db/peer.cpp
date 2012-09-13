//#include <tornet/error.hpp>
#include <tornet/db/peer.hpp>
#include <string.h>
#include <fc/log.hpp>
#include <fc/thread.hpp>
#include <fc/filesystem.hpp>
#include <fc/ip.hpp>
#include <db_cxx.h>


namespace tn { namespace db {

  peer::record::record() {
    memset( this, 0, sizeof(record) );
  }

  bool peer::record::valid()const {
    peer::record tmp;
    return memcmp( tmp.public_key, public_key, sizeof(public_key) ) != 0;
  }
  fc::sha1 peer::record::id()const {
    return fc::sha1::hash( public_key, sizeof(public_key) );
  }


  class peer_private {
    public:
      peer_private( const fc::sha1& id, const fc::path& envdir );
      ~peer_private(){}
      fc::thread          m_thread;

      fc::sha1            m_node_id;
      fc::path            m_envdir;

      DbEnv               m_env;

      Db*                 m_peer_db;
      Db*                 m_ep_index_db;
  };
  peer::peer( const fc::sha1& node_id, const fc::path& dir )
  :my(0) {
    my = new peer_private( node_id, dir );
  }

  peer::~peer() { 
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    }
  }

  peer_private::peer_private( const fc::sha1& node_id, const fc::path& envdir ) 
  :m_thread("db::peer"), m_node_id(node_id), m_envdir(envdir), m_env(0), m_peer_db(0), m_ep_index_db(0)
  {
  }

  int get_peer_ep( Db* sdb, const Dbt* key, const Dbt* data, Dbt* skey ) {
    skey->set_data(data->get_data() ); 
    skey->set_size( sizeof( fc::ip::endpoint ) );
    slog( "%s", fc::string(*((fc::ip::endpoint*)data->get_data())).c_str() );
    return 0;
  }

  void peer::init() {
    if( !my->m_thread.is_current() ) {
        my->m_thread.async( [this](){init();} ).wait();
        return;
    }
    try {
      if( !fc::exists( my->m_envdir ) ) 
        fc::create_directories(my->m_envdir);

      slog( "initializing peer database... %s", my->m_envdir.string().c_str() );
      my->m_env.open( my->m_envdir.string().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %s\n \t\t%s", my->m_envdir.string().c_str(), e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }

    try { 
      my->m_peer_db = new Db(&my->m_env, 0);
      my->m_peer_db->set_flags( DB_RECNUM );
      my->m_peer_db->open( NULL, "peer_data", "peer_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening peer_data database: %s", e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }

    try { 
      my->m_ep_index_db = new Db(&my->m_env, 0);
      my->m_ep_index_db->set_flags( DB_DUPSORT );
      my->m_ep_index_db->open( NULL, "peer_ep_index", "peer_ep_index", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0 );
      my->m_peer_db->associate( NULL, my->m_ep_index_db, get_peer_ep, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening peer_ep_index database: %s", e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }
  }

  void peer::close() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){ close(); } ).wait();
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
      FC_THROW(e);
    }
  }
  void peer::sync() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){ sync(); } ).wait();
        return;
    }
    my->m_ep_index_db->sync(0);
    my->m_peer_db->sync(0);
  }

  uint32_t peer::count() {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [this](){ return count(); } ).wait();
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

  bool peer::fetch( const fc::sha1& id, peer::record& r ) {
    if( !my->m_thread.is_current() ) {
        return my->m_thread.async( [&,this](){ return fetch(id,r); } ).wait();
    }
    try {
      fc::sha1 dist = id ^ my->m_node_id;
      
      Dbt key;
      key.set_data( dist.data() );
      key.set_ulen( sizeof(dist) );
      key.set_flags( DB_DBT_USERMEM );
      
      Dbt val;
      val.set_size( sizeof(r) );
      val.set_ulen( sizeof(r) );
      val.set_flags( DB_DBT_USERMEM );
      
      return DB_NOTFOUND != my->m_peer_db->get( 0, &key, &val, 0 );
    } catch ( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      return false;
    }
  }

  bool peer::exists( const fc::sha1& id ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return exists(id); } ).wait();
    }
    //Dbc*       cur;
    fc::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.data(),sizeof(dist));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val;
    val.set_flags( DB_DBT_USERMEM );
    val.set_flags( DB_DBT_PARTIAL );
    val.set_dlen(0); 

    return DB_NOTFOUND != my->m_peer_db->get( 0, &key, &val, 0 );
  }

  bool peer::store( const fc::sha1& id, const record& m ) {
    if( !my->m_thread.is_current() ) {
        return my->m_thread.async( [=](){ return store(id,m); } ).wait();
        //return my->m_thread.async( [&,this](){ return store(id,m); } ).wait();
    }
    slog("storing %s", fc::string(id).c_str() );

    fc::sha1 check = fc::sha1::hash( m.public_key, sizeof(m.public_key) );
    if( check != id ) {
      FC_THROW( "sha1(data) does not match given id" );
    }
    fc::sha1 dist = id ^ my->m_node_id;

    bool ex = exists(id);

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    try {
        Dbt key(dist.data(),sizeof(dist));
        key.set_flags( DB_DBT_USERMEM );
        Dbt val( (char*)&m, sizeof(m));
        val.set_flags( DB_DBT_USERMEM );
        my->m_peer_db->put( txn, &key, &val, 0 );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW( "%s", e.what() );
    }

    Dbc*       cur;
    Dbt ignore_val; 
    ignore_val.set_dlen(0); 
    ignore_val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM );

    Dbt key(dist.data(),sizeof(dist));
    key.set_ulen(sizeof(dist));
    key.set_flags( DB_DBT_USERMEM );

    my->m_peer_db->cursor( NULL, &cur, 0 );
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
    slog( "inserted/updated record %d  ep %s", idx, fc::string(m.last_ep).c_str() );
    cur->close();
    if( !ex )
      record_inserted(idx);
    else 
      record_changed(idx);
    return true;
  }

  bool peer::fetch_by_endpoint( const fc::ip::endpoint& ep, fc::sha1& id, peer::record& m ) {
      Dbt skey((char*)&ep, sizeof(ep));
      skey.set_flags( DB_DBT_USERMEM );

      Dbt pkey( id.data(), sizeof(id) );
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

  bool peer::fetch_index( uint32_t recnum, fc::sha1& id, record& m) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return fetch_index(recnum,id,m); } ).wait();
    }
    db_recno_t rn(recnum);
    Dbt key( &rn, sizeof(rn) );
    Dbt val( &rn, sizeof(rn) );

    int rtn = my->m_peer_db->get( 0, &key, &val, DB_SET_RECNO );
    if( rtn == EINVAL ) {
      elog( "Invalid DB Query" );
      return false;
    }
    if( rtn != DB_NOTFOUND ) {
      memcpy(id.data(), key.get_data(), key.get_size() );
      id = id ^ my->m_node_id;
      memcpy( &m, val.get_data(), val.get_size() );
      return true;
    }
    wlog( "Unable to find recno %1%", recnum );
    return false;
  }



} } // tn::db
