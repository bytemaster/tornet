#include "sql_session.hpp"
#include "persist.hpp"
#include <fc/filesystem.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/log.hpp>
#include <fc/exception.hpp>

#include <Wt/Dbo/backend/Sqlite3>

namespace tn {
  class sql_session::impl {
    public:
        impl( const fc::path& dir )
        :_sql3((dir / "app.sqlite" ).string().c_str() ) {
          _session.setConnection(_sql3);
          _sql3.setProperty( "show-queries", "true" );
          _session.mapClass<tn::PublishedResource>("PublishedResource");

         {
            Wt::Dbo::Transaction trx(_session);
            try {
              _session.createTables();
              slog( "created tables" );
            } catch ( ... ) {
              elog( "create tables: %s", fc::current_exception().diagnostic_information().c_str() );
            }
            trx.commit();
         }
         _session.flush();
        }
        Wt::Dbo::Session          _session;
        Wt::Dbo::backend::Sqlite3 _sql3;
  };

  sql_session::sql_session( const fc::path& dir )
  :my(dir){}

  sql_session::~sql_session(){}

  Wt::Dbo::Session& sql_session::db() {
    return my->_session;
  }
}
