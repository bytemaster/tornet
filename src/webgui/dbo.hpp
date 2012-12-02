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
  class Domain;
  class PublishedResource;
  typedef dbo::collection<dbo::ptr<PublishedResource> >        PublishedResources;
  typedef dbo::collection<dbo::ptr<Domain> >                   Domains;
}

namespace Wt { namespace Dbo {
  template<>
  struct dbo_traits<tn::Domain> : public dbo_default_traits {
    typedef std::string IdType;
    static IdType invalidId() { return IdType(); }
    static const char* surrogateIdField() { return 0; }
  };
  template<>
  struct dbo_traits<tn::PublishedResource> : public dbo_default_traits {
    typedef std::string IdType;
    static IdType invalidId() { return IdType(); }
    static const char* surrogateIdField() { return 0; }
  };
} }

namespace tn {

    /**
     *
     */
    class PublishedResource : public dbo::Dbo<PublishedResource>, public dbo::ptr<PublishedResource> {
      public:
        PublishedResource( const std::string& lpath, const std::string& lid );
        PublishedResource(){}

        std::string         local_path;
        std::string         site_ref;
        std::string         status;
        double              availability;
        Wt::WDateTime       last_update; // the last time the 'link' was updated.

        Domains             domains; 

        template<typename Action>
        void persist( Action& a );
    };

    class Domain : public dbo::Dbo<Domain>, public dbo::ptr<Domain> {
      public:
          std::string                 name;    // domain name
          std::string                 status;  // reserving, published, updating
          Wt::WDateTime               expires; // the last time the 'link' was updated.
          dbo::ptr<PublishedResource> resource; 

          template<typename Action>
          void persist( Action& a );
    };
}

#endif // _TORNET_DBO_HPP
