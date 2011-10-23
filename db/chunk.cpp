#include <tornet/error.hpp>
#include <tornet/db/chunk.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <db_cxx.h>


namespace tornet { namespace db {
  chunk::meta::meta()
  :first_update(0),last_update(0),query_count(0),size(0){}

  uint64_t chunk::meta::access_interval()const {
      return (last_update - first_update)/query_count;
  }

  class chunk_private {
    public:
      chunk_private( boost::cmt::thread& t, const scrypt::sha1& id, const boost::filesystem::path& envdir );
      ~chunk_private(){}
      
      DbEnv               m_env;
      boost::cmt::thread& m_thread;

      scrypt::sha1        m_node_id;
      boost::filesystem::path m_envdir;
      Db*                 m_chunk_db;
      Db*                 m_meta_db;
  };


  chunk::chunk( const scrypt::sha1& node_id, const boost::filesystem::path& dir )
  :my(0) {
    boost::cmt::thread* t = boost::cmt::thread::create(); 
    my = new chunk_private( *t, node_id, dir );
  }
  chunk::~chunk() { 
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch ( const boost::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
    }
  }

  chunk_private::chunk_private( boost::cmt::thread& t, const scrypt::sha1& node_id, const boost::filesystem::path& envdir ) 
  :m_thread(t), m_node_id(node_id), m_envdir(envdir), m_env(0), m_chunk_db(0), m_meta_db(0)
  {
  }

  void chunk::init() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&chunk::init, this) );
        return;
    }
    try {
      if( !boost::filesystem::exists( my->m_envdir ) ) 
        boost::filesystem::create_directories(my->m_envdir);

      slog( "initializing chunk database... %1%", my->m_envdir );
      my->m_env.open( my->m_envdir.native().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %1%\n \t\t%2%", my->m_envdir, e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_chunk_db = new Db(&my->m_env, 0);
      my->m_chunk_db->open( NULL, "chunk_data", "chunk_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening chunk_data database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }

    try { 
      my->m_meta_db = new Db(&my->m_env, 0);
      my->m_meta_db->set_flags( DB_RECNUM );
      my->m_meta_db->open( NULL, "chunk_meta", "chunk_meta", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening chunk_meta database: %1%", e.what() );
      BOOST_THROW_EXCEPTION( e );
    } catch( const std::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      BOOST_THROW_EXCEPTION( e );
    }
  }

  void chunk::close() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&chunk::close, this) );
        return;
    }
    try {
        if( my->m_chunk_db ){
          slog( "closing chunk db" );
          my->m_chunk_db->close(0);    
          delete my->m_chunk_db;
          my->m_chunk_db = 0;
        }
        if( my->m_meta_db ) {
          slog( "closing chunk meta db" );
          my->m_meta_db->close(0);    
          delete my->m_meta_db;
          my->m_meta_db = 0;
        }
        slog( "closing environment" );
        my->m_env.close(0);
    } catch ( const DbException& e ) {
      BOOST_THROW_EXCEPTION(e);
    }
  }

  void chunk::store_chunk( const scrypt::sha1& id, const boost::asio::const_buffer& b ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&chunk::store_chunk, this, id, b) );
        return;
    }
    scrypt::sha1 check;
    scrypt::sha1_hash( check, boost::asio::buffer_cast<const char*>(b), boost::asio::buffer_size(b) );
    if( check != id ) {
      TORNET_THROW( "sha1(data) does not match given id" );
    }
    scrypt::sha1 dist = id ^ my->m_node_id;

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    db_recno_t recno    = 0;
    bool       updated  = false;
    bool       inserted = false;

    try {
        Dbt key(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );
        Dbt val( (char*)boost::asio::buffer_cast<const char*>(b), boost::asio::buffer_size(b) );
        val.set_flags( DB_DBT_USERMEM );
        if( DB_KEYEXIST ==  my->m_chunk_db->put( txn, &key, &val, DB_NOOVERWRITE ) ) {
          wlog( "key already exists, ignoring store command" );
          txn->abort();
          return;
        }

        meta met;
        Dbt mval( &met, sizeof(met) );
        mval.set_flags( DB_DBT_USERMEM );

        bool inserted = false;
        using namespace boost::chrono;
        if( DB_NOTFOUND == my->m_meta_db->get( txn, &key, &mval, 0 ) ) {
          // initialize met here... 
          met.first_update = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
          inserted = true;
        }
        if( met.size == 0 ) {
          met.size        = boost::asio::buffer_size(b);
          met.last_update = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
          my->m_meta_db->put( txn, &key, &mval, 0 );
          updated    = true;
        }
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      TORNET_THROW( "%1%", %e.what() );
    }

    if( updated || inserted ) {
        Dbc*       cur;
        Dbt ignore_val; 
        ignore_val.set_dlen(0); 
        ignore_val.set_flags( DB_DBT_PARTIAL );

        Dbt key(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );

        my->m_meta_db->cursor( NULL, &cur, 0 );
        cur->get( &key, &ignore_val, 0 );

        db_recno_t idx;
        Dbt        idx_val(&idx, sizeof(idx) );
        idx_val.set_flags( DB_DBT_USERMEM );
        idx_val.set_ulen(sizeof(idx) );

        cur->get(  0, &idx_val, DB_SET_RECNO );
        cur->close();
        if( inserted )
          record_inserted(idx);
        else 
          record_changed(idx);
    }
  }


  bool chunk::fetch_chunk( const scrypt::sha1& id, const boost::asio::mutable_buffer& b, uint64_t offset ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_chunk, this, id, b, offset ) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val( boost::asio::buffer_cast<char*>(b), boost::asio::buffer_size(b) );
    val.set_flags( DB_DBT_USERMEM );
    val.set_doff(offset);

    return DB_NOTFOUND != my->m_chunk_db->get( 0, &key, &val, 0 );
  }

  bool chunk::fetch_meta( const scrypt::sha1& id, chunk::meta& m ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_meta, this, id, boost::ref(m) ) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );

    Dbt val( &m, sizeof(m) );
    val.set_flags( DB_DBT_USERMEM | DB_DBT_PARTIAL );

    return DB_NOTFOUND != my->m_meta_db->get( 0, &key, &val, 0 );
  }

  bool chunk::fetch_index(  uint32_t recnum, scrypt::sha1& id, chunk::meta& m ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_index, this, recnum, boost::ref(id), boost::ref(m) ) );
    }
    db_recno_t rn(recnum);
    Dbt key( &rn, sizeof(rn) );
    key.set_flags( DB_DBT_MALLOC );

    Dbt val( (char*)&m, sizeof(m) );
    val.set_flags( DB_DBT_USERMEM | DB_DBT_PARTIAL );

    int rtn = my->m_meta_db->get( 0, &key, &val, 0 );
    if( rtn != DB_NOTFOUND ) {
      memcpy((char*)id.hash, key.get_data(), key.get_size() );
      return true;
    }
    return false;
  }

  uint32_t chunk::count() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<uint32_t>( boost::bind(&chunk::count, this ) );
    }
    db_recno_t num;
    Dbc*       cur;
    Dbt key;
    Dbt val(&num, sizeof(num) );
    val.set_ulen(sizeof(num));
    val.set_flags( DB_DBT_USERMEM );

    Dbt ignore_val; 
    ignore_val.set_flags( DB_DBT_MALLOC | DB_DBT_PARTIAL);
    ignore_val.set_dlen(0); 

    my->m_meta_db->cursor( NULL, &cur, 0 );
    int rtn = cur->get( &key, &ignore_val, DB_LAST );
    if( rtn == DB_NOTFOUND ) {
      cur->close();
      return 0;
    }
    rtn = cur->get( &key, &val, DB_GET_RECNO );
    cur->close();
    return num;
  }

  void chunk::sync() {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        my->m_thread.sync( boost::bind(&chunk::sync, this ) );
        return;
    }
    my->m_meta_db->sync(0);
    my->m_chunk_db->sync(0);
  }

} } // tornet::db
