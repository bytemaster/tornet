#include "cafs_file_db.hpp"
#include <fc/thread.hpp>
#include <fc/raw.hpp>
#include <db_cxx.h>

class cafs_file_db::impl : public fc::retainable {
  public:
      impl()
      :_thread("cafs_file_db"),_db(nullptr),_env(0)
      {
      }
      fc::thread          _thread;
      Db*                 _db;
      DbEnv               _env;
};

cafs_file_db::cafs_file_db() 
:my( new impl() ) {
}

cafs_file_db::~cafs_file_db() { }

void cafs_file_db::open( const fc::path& p ) {
  if( !my->_thread.is_current() ) {
    my->_thread.async([=](){ this->open(p); }).wait();
    return;
  }

  try {
    if( !fc::exists(p) ) fc::create_directories(p);
    slog( "initializing file database... %s", p.string().c_str() );
    my->_env.open( p.generic_string().c_str(), 
                   DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_REGISTER | DB_RECOVER, 0 );
  } catch( const DbException& e ) {
    elog( "Error opening database environment: %s\n \t\t%s", p.string().c_str(), e.what() );
    FC_THROW_MSG( "Error opening database environment: %s\n %s", p.string().c_str(), e.what() );
  } catch( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    fc::rethrow_exception( fc::current_exception() );
  }

    try { 
      my->_db = new Db(&my->_env, 0);
      my->_db->open( NULL, "file_data", "file_data", DB_HASH, DB_CREATE | DB_AUTO_COMMIT, 0 );
    } catch( const DbException& e ) {
      elog( "Error opening chunk_data database: %s", e.what() );
      FC_THROW( e );
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      fc::rethrow_exception( fc::current_exception() );
    }
}

void cafs_file_db::close() {
  if( !my->_thread.is_current() ) {
    my->_thread.async([=](){ this->close(); }).wait();
    return;
  }
  try {
      my->_db->close(0);
      delete my->_db;
      my->_db = nullptr;
      my->_env.close(0);
  } catch (...) {
    FC_THROW_MSG( "Error closing cafs file db" );
  }
}

void                          cafs_file_db::store( const cafs::file_ref& r ) {
  if( !my->_thread.is_current() ) {
    my->_thread.async([&](){ this->store(r); }).wait();
    return;
  }
  fc::vector<char> dat = fc::raw::pack(r);
  DbTxn * txn=NULL;
  my->_env.txn_begin(NULL, &txn, 0);

  Dbt key;//(dist.hash,sizeof(dist.hash));
  key.set_flags( DB_DBT_USERMEM );
  key.set_data(r.content.data());
  key.set_ulen(sizeof(r.content) );
  key.set_size(sizeof(r.content) );

  Dbt val;
  val.set_data( (void*)dat.data() );
  val.set_size( dat.size() );
  val.set_ulen( dat.size() );
  val.set_flags( DB_DBT_USERMEM );
  try {
      if( DB_KEYEXIST ==  my->_db->put( txn, &key, &val, DB_NOOVERWRITE ) ) {
        // TODO: enable multiple stores?
        wlog( "key already exists, ignoring store command" );
        txn->abort();
        return;
      }
  } catch ( const DbException& e ) {
    txn->abort();
    FC_THROW_MSG( "%s", e.what() );
  }
  txn->commit( DB_TXN_WRITE_NOSYNC );
}

fc::optional<cafs::file_ref>  cafs_file_db::fetch( const fc::sha1& id ) {
  if( !my->_thread.is_current() ) {
    return my->_thread.async([&](){ return this->fetch(id); }).wait();
  }
  fc::optional<cafs::file_ref> r;

  Dbt key(id.data(),sizeof(id));
  Dbt val;

  int rtn;
  if( (rtn = my->_db->get( 0, &key, &val, 0 )) == DB_NOTFOUND ) {
    elog( "chunk not found" );
  } else {
    fc::datastream<char*> ds((char*)val.get_data(), val.get_size() );
    cafs::file_ref fr;
    fc::raw::unpack( ds, fr );
    r = fc::move(fr);
  }
  if( rtn == EINVAL ) { elog( "Invalid get" ); }
  return r;
}
void                          cafs_file_db::remove( const fc::sha1& id ) {
  if( !my->_thread.is_current() ) {
    return my->_thread.async([&](){ this->remove(id); }).wait();
  }
  // TODO: implement cafs_file_db::remove
  wlog( "Not Implemented" );

}
