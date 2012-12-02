#ifndef _DBO_PERSIST_HPP_
#define _DBO_PERSIST_HPP_
#include "dbo.hpp"


namespace tn {
    template<typename Action>
    void PublishedResource::persist( Action& a ) {
      dbo::id( a, site_ref, "site_ref" );
      dbo::field( a, local_path,       "local_path"       );
      dbo::field( a, availability,     "availability"     );
      dbo::field( a, last_update,      "last_update"      );
      dbo::hasMany( a, domains, dbo::ManyToOne, "resource" );
    }
    template<typename Action>
    void Domain::persist( Action& a ) {
      dbo::id( a, name, "name" );
      dbo::field( a, status, "status" );
      dbo::field( a, expires, "expires" );
      dbo::belongsTo( a, resource, "resource" );
    }
}
#endif // _DBO_PERSIST_HPP_
