#include <tornet/db/chunk.hpp>
#include <fc/thread.hpp>
#include <fc/sha1.hpp>
#include <fc/filesystem.hpp>
#include <fc/time.hpp>
#include <fc/vector.hpp>
#include <fc/exception.hpp>
#include <fc/buffer.hpp>
#include <fc/bigint.hpp>

#include <db_cxx.h>

#include <assert.h>
#include <fc/hex.hpp>

namespace tn { namespace db {
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
    return fc::time_point::now().time_since_epoch().count();
  }


  class chunk_private {
    public:
      chunk_private( const fc::sha1& id, const fc::path& envdir );
      ~chunk_private(){}
      
      fc::thread          m_thread;

      fc::sha1            m_node_id;
      fc::path            m_envdir;
      DbEnv               m_env;
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
  void chunk::fetch_worst_performance( fc::vector<chunk::meta_record>& m, int n  ) {
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
        Dbt pkey(m.back().key.data(), sizeof(m.back().key) );
        Dbt value(&m.back().value, sizeof(m.back().value) ); 

        value.set_data( (char*)&m.back().value );
        value.set_ulen( sizeof(m.back().value) );
        value.set_flags( DB_DBT_USERMEM );

        key.set_data( (char*)&v );
        key.set_ulen( sizeof(v) );
        key.set_flags( DB_DBT_USERMEM );

        pkey.set_data( m.back().key.data() );
        pkey.set_ulen( sizeof(m.back().key) );
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
      } while ( int(m.size()) < n );
      cur->close();
  }
  /**
   *  Find the first N elements in the secondary index (most negative)
   */
  void chunk::fetch_best_opportunity( fc::vector<chunk::meta_record>& m, int n  ) {
      m.resize(0);
      m.reserve(n);

      Dbc*       cur;
      my->m_meta_rev_idx->cursor( NULL, &cur, 0 );
      do {
        m.resize(m.size()+1);
        int64_t v;
        Dbt key((char*)&v,sizeof(v));
        Dbt pkey(m.back().key.data(), sizeof(m.back().key) );
        Dbt value((char*)&m.back().value, sizeof(m.back().value) ); 

        value.set_data( (char*)&m.back().value );
        value.set_size( sizeof(m.back().value) );
        value.set_ulen( sizeof(m.back().value) );
        value.set_flags( DB_DBT_USERMEM );

        key.set_data( (char*)&v );
        key.set_size( sizeof(v) );
        key.set_ulen( sizeof(v) );
        key.set_flags( DB_DBT_USERMEM );

        pkey.set_data( m.back().key.data() );
        pkey.set_ulen( sizeof(m.back().key) );
        pkey.set_flags( DB_DBT_USERMEM );

        if( DB_NOTFOUND == cur->pget( &key, &pkey, &value, DB_NEXT  ) ) {
          m.pop_back();
          cur->close(); 
          return;
        }
        m.back().key = m.back().key ^ my->m_node_id;
      } while ( int(m.size()) < n );
      cur->close(); 
  }


  chunk::chunk( const fc::sha1& node_id, const fc::path& dir )
  :my(0) {
    my = new chunk_private( node_id, dir );
  }
  chunk::~chunk() { 
    slog( "...");
    try {
        close();
        my->m_thread.quit();
        delete my; 
    } catch (...) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    }
  }

  chunk_private::chunk_private( const fc::sha1& node_id, const fc::path& envdir ) 
  :m_thread("db::chunk"), m_node_id(node_id), m_envdir(envdir), m_env(0), m_chunk_db(0), m_meta_db(0)
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
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){ init(); } ).wait();
        return;
    }
    try {
      if( !fc::exists( my->m_envdir ) ) 
        fc::create_directories(my->m_envdir);

      slog( "initializing chunk database... %s", my->m_envdir.string().c_str() );
      my->m_env.open( my->m_envdir.string().c_str(), DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening database environment: %s\n \t\t%s", my->m_envdir.string().c_str(), e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }

    try { 
      my->m_chunk_db = new Db(&my->m_env, 0);
      my->m_chunk_db->open( NULL, "chunk_data", "chunk_data", DB_BTREE, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening chunk_data database: %1%", e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
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
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }
  }

  void chunk::close() {
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){close();} ).wait();
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
      FC_THROW(e);
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
  bool chunk::store_chunk( const fc::sha1& id, const fc::const_buffer& b ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [this,id,b](){ return store_chunk(id,b); } ).wait();
    }
    fc::sha1 check = fc::sha1::hash( b.data, b.size );

    if( check != id ) {
      FC_THROW( "sha1(data) does not match given id" );
    }
    fc::sha1 dist = id ^ my->m_node_id;
    slog( "store chunk %s dist %s", fc::string(id).c_str(), fc::string(dist).c_str() );
    slog( "store data '%s'", fc::to_hex( b.data, 64 ).c_str() );

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);

    //db_recno_t recno    = 0;
    bool       updated  = false;
    bool       inserted = false;

    try {
        Dbt key;//(dist.hash,sizeof(dist.hash));
        key.set_flags( DB_DBT_USERMEM );
        key.set_data(dist.data());
        key.set_ulen(sizeof(dist) );
        key.set_size(sizeof(dist) );


        Dbt val;//( (char*)boost::asio::buffer_cast<const char*>(b), boost::asio::buffer_size(b) );
        val.set_data( (void*)b.data );
        val.set_size( b.size );
        val.set_flags( DB_DBT_USERMEM );
        val.set_ulen( b.size );

        if( DB_KEYEXIST ==  my->m_chunk_db->put( txn, &key, &val, DB_NOOVERWRITE ) ) {
          wlog( "key already exists, ignoring store command" );
          txn->abort();
          return false;
        }

        meta met;
        Dbt mval( &met, sizeof(met) );
        mval.set_flags( DB_DBT_USERMEM );
        mval.set_ulen( sizeof(met) );

        //bool inserted = false;
        if( DB_NOTFOUND == my->m_meta_db->get( txn, &key, &mval, 0 ) ) {
          slog( "initialize meta here, key %s" );
          // initialize met here... 
          met.first_update = met.now(); 
          met.distance_rank = 161 - fc::bigint( (const char*)dist.data(), sizeof(dist) ).log2();
          inserted = true;
        }
        if( met.size == 0 ) {
          met.size        = b.size;
          met.last_update = met.now();
          slog( "put db" );
          my->m_meta_db->put( txn, &key, &mval, 0 );
          updated    = true;
        }
        slog( "commit" );
        txn->commit( DB_TXN_WRITE_NOSYNC );
    } catch ( const DbException& e ) {
      txn->abort();
      FC_THROW_MSG( "%s", e.what() );
    }

    if( updated || inserted ) {
      slog(">");
        Dbc*       cur;
        Dbt ignore_val; 
        ignore_val.set_dlen(0); 
        ignore_val.set_flags( DB_DBT_PARTIAL );

        Dbt key;//(dist.hash,sizeof(dist.hash));
      //  Dbt key( dist.data(),sizeof(dist);
        key.set_data( dist.data());
        key.set_size( sizeof(dist) );
        key.set_flags( DB_DBT_USERMEM );
        key.set_ulen( sizeof(dist) );

        slog("..");
        auto rtn = my->m_meta_db->cursor( NULL, &cur, 0 );
        if( rtn != 0 ) { FC_THROW_MSG( "Invalid DB cursor" ); }

        rtn = cur->get( &key, &ignore_val, DB_SET );
        if( rtn != 0 ) { FC_THROW_MSG( "Invalid DB Set %s  key %s", rtn, dist ); }
        slog("..");

        db_recno_t idx;
        Dbt        idx_val(&idx, sizeof(idx) );
        idx_val.set_flags( DB_DBT_USERMEM );
        idx_val.set_ulen(sizeof(idx) );
        slog("get recno");

        cur->get(  &ignore_val, &idx_val, DB_GET_RECNO );
        slog("..");
        cur->close();
        slog("..");
        /*
        if( inserted )
          record_inserted(idx);
        else 
          record_changed(idx);
          */
    }
    return true;
  }

  bool chunk::store_meta( const fc::sha1& id, const meta& m ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return store_meta(id,m); } ).wait();
    }
    fc::sha1 dist = id ^ my->m_node_id;
    Dbt key(dist.data(),sizeof(dist));
    key.set_flags( DB_DBT_USERMEM );
    key.set_ulen(sizeof(dist));

    Dbt val( (char*)&m, sizeof(m) );
    val.set_flags( DB_DBT_USERMEM );
    val.set_ulen( sizeof(m) );

    DbTxn * txn=NULL;
    my->m_env.txn_begin(NULL, &txn, 0);
    my->m_meta_db->put( txn, &key, &val, 0 );
    txn->commit( DB_TXN_WRITE_NOSYNC );

    return true;
  }


  bool chunk::fetch_chunk( const fc::sha1& id, const fc::mutable_buffer& b, uint64_t offset ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [=](){ return fetch_chunk(id,b,offset); } ).wait();
    }
    //Dbc*       cur;
    fc::sha1 dist = id ^ my->m_node_id;
    slog( "fetch chunk %s dist %s bufsize %d offset %d", fc::string(id).c_str(), fc::string(dist).c_str(), b.size, offset );

    Dbt key(dist.data(),sizeof(dist));
    /*
    key.set_data( (char*)dist.hash );
    key.set_ulen( sizeof(dist.hash) );
    key.set_flags( DB_DBT_USERMEM );
    */

    Dbt val; 
    val.set_data( b.data );
   // val.set_size( b.size );
    val.set_ulen( b.size );
    val.set_dlen( b.size );
    val.set_flags( DB_DBT_PARTIAL | DB_DBT_USERMEM );
    val.set_doff(offset);

    int rtn;
    if( (rtn = my->m_chunk_db->get( 0, &key, &val, 0 )) == DB_NOTFOUND ) {
      elog( "chunk not found" );
      return false;
    }
    if( rtn == EINVAL ) { elog( "Invalid get" ); }

    wlog( "fetch return data '%s'", fc::to_hex( (char*)val.get_data(), 64 ).c_str() );
    wlog( "fetch return data '%s'", fc::to_hex( b.data, 64 ).c_str() );
    return true;
  }

  bool chunk::fetch_meta( const fc::sha1& id, chunk::meta& m, bool auto_inc ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return fetch_meta( id, m, auto_inc ); } ).wait();
    }
    Dbc*       cur;
    fc::sha1 dist = id ^ my->m_node_id;
    slog( "fetch meta chunk %s dist %s", fc::string(id).c_str(), fc::string(dist).c_str() );

    Dbt key(dist.data(),sizeof(dist));
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
    if( rtn == EINVAL ) { FC_THROW( "Invalid DB Get" ); }

    if( DB_NOTFOUND==rtn && auto_inc ) {
      slog( "not found && auto inc" );
      m.distance_rank = 161 - fc::bigint( (const char*)dist.data(), sizeof(dist) ).log2();
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

  bool chunk::fetch_index(  uint32_t recnum, fc::sha1& id, chunk::meta& m ) {
    if( &fc::thread::current() != &my->m_thread ) {
        return my->m_thread.async( [&,this](){ return fetch_index( recnum, id, m ); } ).wait();
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
      memcpy(id.data(), key.get_data(), key.get_size() );
      id = id ^ my->m_node_id;
      memcpy( &m, val.get_data(), val.get_size() );
      return true;
    }
    wlog( "Unable to find recno %1%", recnum );
    return false;
  }

  uint32_t chunk::count() {
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
    if( &fc::thread::current() != &my->m_thread ) {
        my->m_thread.async( [this](){ return sync(); } ).wait();
        return;
    }
    my->m_meta_db->sync(0);
    my->m_chunk_db->sync(0);
  }

} } // tornet::db
