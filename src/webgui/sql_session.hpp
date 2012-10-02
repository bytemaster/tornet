#ifndef _TN_SQL_SESSION_HPP_
#define _TN_SQL_SESSION_HPP_
#include <fc/fwd.hpp>

namespace Wt { namespace Dbo {
  class Session;
} }

namespace fc {
  class path;
}

namespace tn {

  class sql_session {
    public:
      sql_session( const fc::path& dir );
      ~sql_session();

      Wt::Dbo::Session& db();
    private:
      class impl;
      fc::fwd<impl,344> my;
  };

}


#endif // _TN_SQL_SESSION_HPP_
