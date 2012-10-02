#ifndef _TORNET_DBO_HPP_
#define _TORNET_DBO_HPP_
#include <Wt/Dbo/Dbo>
#include <Wt/Dbo/WtSqlTraits>
#include <Wt/WTime>
#include <Wt/WDateTime>
#include <Wt/Utils>

namespace dbo = Wt::Dbo;

namespace fc {
  class sha1;
  class string;
}

namespace tn {
  class TornetLink;
  class Torsite;
}

/*
namespace Wt { namespace Dbo {
  template<>
  struct dbo_traits<tn::TornetLink> : public dbo_default_traits {
    typedef std::string IdType;
    static IdType invalidId() { return IdType(); }
    static const char* surrogateIdField() { return 0; }
  };
} }
*/

namespace tn {

    /**
     *  Sites that have been published
    class Torsite : public dbo::Dbo<Torsite>, public dbo::ptr<Torsite> {
      public:
        Torsite();
        std::string    domain;     // globaly unique domain! 
        std::string    local_path; // local resource being published at name.
                       
        std::string    link_id;    // root chunk for the site (normally an archive)
        long long      link_seed;        

        Wt::WDateTime  last_update; // the last time the 'link' was updated.

        template<typename Action>
        void persist( Action& a );
    };                  
     */

    /**
     *
     */
    class PublishedResource : public dbo::Dbo<PublishedResource>, public dbo::ptr<PublishedResource> {
      public:
        PublishedResource( const std::string& lpath, const std::string& lid, uint64_t sd );
        PublishedResource(){}

        std::string         local_path;
        std::string         link_id;            
        long long           link_seed;   // random seed used to ensure data is random enough
        Wt::WDateTime       last_update; // the last time the 'link' was updated.

        template<typename Action>
        void persist( Action& a );
    };
}

#endif // _TORNET_DBO_HPP
