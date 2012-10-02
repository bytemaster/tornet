#ifndef _DBO_PERSIST_HPP_
#define _DBO_PERSIST_HPP_
#include "dbo.hpp"


namespace tn {
    template<typename Action>
    void PublishedResource::persist( Action& a ) {
      dbo::field( a, local_path,       "local_path"  );
      dbo::field( a, link_id,          "link_id"     );
      dbo::field( a, link_seed,        "link_seed"   );
      dbo::field( a, last_update,      "last_update" );
    }

    /*
    template<typename Action>
    void Torsite::persist( Action& a ) {
      dbo::field( a, domain,       "domain"       );
      dbo::field( a, tid,          "tid"          );
      dbo::field( a, check,        "check"        );
      dbo::field( a, seed,         "seed"         );
      dbo::field( a, local_dir,    "local_dir"    );
      dbo::field( a, replicate,    "replicate"    );
      dbo::field( a, availability, "availability" );
      dbo::field( a, last_update,  "last_update"  );
    }
    */
}
#endif // _DBO_PERSIST_HPP_
