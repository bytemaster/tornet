#include <tornet/error.hpp>
#include <tornet/db/chunk.hpp>
#include <boost/cmt/thread.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <db_cxx.h>
#include <scrypt/bigint.hpp>


namespace tornet { namespace db {
  chunk::meta::meta()
  :first_update(0),last_update(0),query_count(0),size(0),distance_rank(1){}

  /**
   *  Microseconds between accesses, always >= 1
   */
  uint64_t chunk::meta::access_interval()const {
      if( query_count == 0 ) return 0xffffffffffffffff;
      return 1+ (last_update - first_update)/query_count;
  }

  /**
   *  size * (161-log2(dist)) / year
   */
  int64_t chunk::meta::annual_revenue_rate()const {
    return price() * annual_query_count();
  }
  int64_t chunk::meta::price()const {
      return (size ? int64_t(size) : -1024*66) * distance_rank + 1024;
  }
  int64_t  chunk::meta::annual_query_count()const {
    return (365ll*24*60*60*1000000) / access_interval();
  }


  uint64_t chunk::meta::now()const {
    using namespace boost::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count(); 
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
      Db*                 m_meta_rev_idx;
  };

  /**
   *   Calculates the secondary key for 'potential' annaul revenue from the given
   *   meta record.  If the size is 0, that means it is 'opportunity cost' and therefore
   *   has a negitive annual revenue.
   */
  int get_annual_rev( Db* secondary, const Dbt* pkey, const Dbt* pdata, Dbt* skey ) {
    chunk::meta* met = (chunk::meta*)(pdata->get_data());
    skey->set_data ( new int64_t(met->annual_revenue_rate() ) );
    skey->set_size ( sizeof(int64_t) );
    skey->set_flags( DB_DBT_APPMALLOC );
    return 0;
  }

  /**
   *  Find the first N positive meta records
   */
  void chunk::fetch_worst_performance( std::vector<chunk::meta_record>& m, int n  ) {
      m.resize(0);
      m.reserve(n);

      int64_t v=0;
      Dbt key((char*)&v,sizeof(v));

      Dbc*       cur;
      my->m_meta_rev_idx->cursor( NULL, &cur, 0 );


      do {
        m.resize(m.size()+1);
        int64_t v=1;
        Dbt key((char*)&v,sizeof(v));
        Dbt pkey((char*)&m.back().key.hash, sizeof(m.back().key.hash) );
        Dbt value((char*)&m.back().value, sizeof(m.back().value) ); 

        value.set_data( (char*)&m.back().value );
        value.set_ulen( sizeof(m.back().value) );
        value.set_flags( DB_DBT_USERMEM );

        key.set_data( (char*)&v );
        key.set_ulen( sizeof(v) );
        key.set_flags( DB_DBT_USERMEM );

        pkey.set_data( (char*)&m.back().key.hash );
        pkey.set_ulen( sizeof(m.back().key.hash) );
        pkey.set_flags( DB_DBT_USERMEM );

        int rtn = 0;
        if( m.size() == 1 ) {
           rtn = cur->pget( &key, &pkey, &value, DB_SET_RANGE  );
        } else {
           rtn = cur->pget( &key, &pkey, &value, DB_NEXT );
        }

        if( DB_NOTFOUND == rtn ) {
          m.pop_back();
          cur->close();
          return;
        }
        m.back().key = m.back().key ^ my->m_node_id;
      } while ( m.size() < n );
      cur->close();
  }
  /**
   *  Find the first N elements in the secondary index (most negative)
   */
  void chunk::fetch_best_opportunity( std::vector<chunk::meta_record>& m, int n  ) {
      m.resize(0);
      m.reserve(n);

      Dbc*       cur;
      my->m_meta_rev_idx->cursor( NULL, &cur, 0 );
      do {
        m.resize(m.size()+1);
        int64_t v;
        Dbt key((char*)&v,sizeof(v));
        Dbt pkey((char*)&m.back().key.hash, sizeof(m.back().key.hash) );
        Dbt value((char*)&m.back().value, sizeof(m.back().value) ); 

        value.set_data( (char*)&m.back().value );
        value.set_size( sizeof(m.back().value) );
        value.set_ulen( sizeof(m.back().value) );
        value.set_flags( DB_DBT_USERMEM );

        key.set_data( (char*)&v );
        key.set_size( sizeof(v) );
        key.set_ulen( sizeof(v) );
        key.set_flags( DB_DBT_USERMEM );

        pkey.set_data( (char*)&m.back().key.hash );
        pkey.set_ulen( sizeof(m.back().key.hash) );
        pkey.set_flags( DB_DBT_USERMEM );

        if( DB_NOTFOUND == cur->pget( &key, &pkey, &value, DB_NEXT  ) ) {
          m.pop_back();
          cur->close(); 
          return;
        }
        m.back().key = m.back().key ^ my->m_node_id;
      } while ( m.size() < n );
      cur->close(); 
  }


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
  int compare_i64(Db *db, const Dbt *key1, const Dbt *key2) {
    int64_t* _k1 = (int64_t*)(key1->get_data());
    int64_t* _k2 = (int64_t*)(key2->get_data());
    if( *_k1 > *_k2 ) return 1;
    if( *_k1 == *_k2 ) return 0;
    return -1;
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

      my->m_meta_rev_idx = new Db(&my->m_env, 0 );
      my->m_meta_rev_idx->set_flags( DB_DUP | DB_DUPSORT );
      my->m_meta_rev_idx->set_bt_compare( &compare_i64 );
      my->m_meta_rev_idx->open( NULL, "chunk_meta_rev_idx", "chunk_meta_rev_idx", DB_BTREE , DB_CREATE | DB_AUTO_COMMIT, 0);//0600 );
      my->m_meta_db->associate( 0, my->m_meta_rev_idx, get_annual_rev, 0 );

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



  /**
   *  Attempts to store data in buffer b.  
   *
   *  @pre sha1(b) == id
   *  @post buffer b stored at index id
   *  @post meta table for index id exists.
   *
   */
  bool chunk::store_chunk( const scrypt::sha1& id, const boost::asio::const_buffer& b ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::store_chunk, this, id, b) );
    }
    scrypt::sha1 check;
    scrypt::sha1_hash( check, boost::asio::buffer_cast<const char*>(b), boost::asio::buffer_size(b) );
    if( check != id ) {
      TORNET_THROW( "sha1(data) does not match given id" );
    }
    scrypt::sha1 dist = id ^ my->m_node_id;
    slog( "store chunk %1% dist %2%", id, dist );

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    db_recno_t recno    = 0;
    bool       updated  = false;
    bool       inserted = false;

    try {
        Dbt key;//(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );
        key.set_data(dist.hash);
        key.set_ulen(sizeof(dist.hash) );


        Dbt val;//( (char*)boost::asio::buffer_cast<const char*>(b), boost::asio::buffer_size(b) );
        val.set_data( (char*)boost::asio::buffer_cast<const char*>(b) );
        val.set_flags( DB_DBT_USERMEM );
        val.set_ulen( boost::asio::buffer_size(b) );

        if( DB_KEYEXIST ==  my->m_chunk_db->put( txn, &key, &val, DB_NOOVERWRITE ) ) {
          wlog( "key already exists, ignoring store command" );
          txn->abort();
          return false;
        }

        meta met;
        Dbt mval( &met, sizeof(met) );
        mval.set_flags( DB_DBT_USERMEM );
        mval.set_ulen( sizeof(met) );

        bool inserted = false;
        if( DB_NOTFOUND == my->m_meta_db->get( txn, &key, &mval, 0 ) ) {
          // initialize met here... 
          met.first_update = met.now(); 
          met.distance_rank = 161 - scrypt::bigint( (const char*)dist.hash, sizeof(dist.hash) ).log2();
          inserted = true;
        }
        if( met.size == 0 ) {
          met.size        = boost::asio::buffer_size(b);
          met.last_update = met.now();
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

        Dbt key;//(dist.hash,sizeof(dist.hash));
        key.set_data( (char*)dist.hash );
        key.set_size( sizeof(dist.hash) );
        key.set_flags( DB_DBT_USERMEM );
        key.set_ulen( sizeof(dist.hash) );

        my->m_meta_db->cursor( NULL, &cur, 0 );
        cur->get( &key, &ignore_val, DB_SET );

        db_recno_t idx;
        Dbt        idx_val(&idx, sizeof(idx) );
        idx_val.set_flags( DB_DBT_USERMEM );
        idx_val.set_ulen(sizeof(idx) );

        cur->get(  &ignore_val, &idx_val, DB_GET_RECNO );
        cur->close();
        if( inserted )
          record_inserted(idx);
        else 
          record_changed(idx);
    }
    return true;
  }

  bool chunk::store_meta( const scrypt::sha1& id, const meta& m ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::store_meta, this, id, boost::ref(m) ) );
    }
    scrypt::sha1 dist = id ^ my->m_node_id;
    Dbt key(dist.hash,sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );
    key.set_ulen(sizeof(dist.hash));

    Dbt val( (char*)&m, sizeof(m) );
    val.set_flags( DB_DBT_USERMEM );
    val.set_ulen( sizeof(m) );

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);
    my->m_meta_db->put( txn, &key, &val, 0 );
    txn->commit( DB_TXN_WRITE_NOSYNC );

    return true;
  }


  bool chunk::fetch_chunk( const scrypt::sha1& id, const boost::asio::mutable_buffer& b, uint64_t offset ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_chunk, this, id, b, offset ) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    /*
    key.set_data( (char*)dist.hash );
    key.set_ulen( sizeof(dist.hash) );
    key.set_flags( DB_DBT_USERMEM );
    */

    Dbt val; 
    val.set_data( boost::asio::buffer_cast<char*>(b) );
    val.set_size(  boost::asio::buffer_size(b) );
    val.set_ulen( boost::asio::buffer_size(b) );
    val.set_dlen(  boost::asio::buffer_size(b) );
    val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM );
    val.set_doff(offset);

    int rtn;
    if( rtn = my->m_chunk_db->get( 0, &key, &val, 0 ) == DB_NOTFOUND ) {
      return false;
    }
    if( rtn == EINVAL ) { elog( "Invalid get" ); }

    return true;
  }

  bool chunk::fetch_meta( const scrypt::sha1& id, chunk::meta& m, bool auto_inc ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_meta, this, id, boost::ref(m), auto_inc ) );
    }
    Dbc*       cur;
    scrypt::sha1 dist = id ^ my->m_node_id;

    Dbt key(dist.hash,sizeof(dist.hash));
    /*
    key.set_data(dist.hash);
    key.set_ulen(sizeof(dist.hash));
    key.set_flags( DB_DBT_USERMEM );
    */

    Dbt val;
    val.set_data( &m );
    val.set_size( sizeof(m) );
    val.set_dlen( sizeof(m) );
    val.set_ulen( sizeof(m) );
    val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM);

    int rtn  = my->m_meta_db->get( 0, &key, &val, 0 );
    if( rtn == EINVAL ) { TORNET_THROW( "Invalid DB Get" ); }

    if( DB_NOTFOUND==rtn && auto_inc ) {
      slog( "not found && auto inc" );
      m.distance_rank = 161 - scrypt::bigint( (const char*)dist.hash, sizeof(dist.hash) ).log2();
      m.first_update = m.now();
      m.last_update  = m.now();
      m.query_count  = 0;
      store_meta( id, m );
      return true;
    }
    if( rtn == DB_NOTFOUND )  {
      wlog( "not found && !auto inc" );
      return false;
    }
    if( auto_inc ) {
      slog( "found && auto inc" );
      m.query_count++;
      m.last_update = m.now();
      store_meta( id, m );
    }
    return true;
  }

  bool chunk::fetch_index(  uint32_t recnum, scrypt::sha1& id, chunk::meta& m ) {
    if( &boost::cmt::thread::current() != &my->m_thread ) {
        return my->m_thread.sync<bool>( boost::bind(&chunk::fetch_index, this, recnum, boost::ref(id), boost::ref(m) ) );
    }
    db_recno_t rn(recnum);
    Dbt key( &rn, sizeof(rn) );
    //key.set_flags( DB_DBT_MALLOC );

    //Dbt val( (char*)&m, sizeof(m) );
    Dbt val( &rn, sizeof(rn) );
    //val.set_flags( DB_DBT_USERMEM | DB_DBT_PARTIAL );

    int rtn = my->m_meta_db->get( 0, &key, &val, DB_SET_RECNO );
    if( rtn == EINVAL ) {
      elog( "Invalid DB Query" );
      return false;
    }
    if( rtn != DB_NOTFOUND ) {
      memcpy((char*)id.hash, key.get_data(), key.get_size() );
      id = id ^ my->m_node_id;
      memcpy( &m, val.get_data(), val.get_size() );
      return true;
    }
    wlog( "Unable to find recno %1%", recnum );
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
