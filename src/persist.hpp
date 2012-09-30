#ifndef _DBO_PERSIST_HPP_
#define _DBO_PERSIST_HPP_
#include "dbo.hpp"


namespace tn {
    template<typename Action>
    void TornetLink::persist( Action& a ) {
      dbo::id(    a, tid,              "id"               );
      dbo::field( a, name,             "name"             );
      dbo::field( a, check,            "check"            );
      dbo::field( a, seed,             "seed"             );
                                                          
      dbo::field( a, tornet_size,      "tornet_size"      );
      dbo::field( a, file_size,        "file_size"        );
      dbo::field( a, download_count,   "download_count"   );
      dbo::field( a, publishing,       "publishing"       );
      dbo::field( a, replicate_count,  "replicate_count"  );
      dbo::field( a, created,          "id_sig"           );
      dbo::field( a, last_download,    "pub_key"          );
    }


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
}
#endif // _DBO_PERSIST_HPP_
