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

namespace Wt { namespace Dbo {
  template<>
  struct dbo_traits<tn::TornetLink> : public dbo_default_traits {
    typedef std::string IdType;
    static IdType invalidId() { return IdType(); }
    static const char* surrogateIdField() { return 0; }
  };
} }

namespace tn {
    class Torsite : public dbo::Dbo<Torsite>, public dbo::ptr<Torsite> {
      public:
        Torsite();
        std::string    domain;     // globaly unique domain! 
                       
        std::string    tid;        // root chunk for the site (normally an archive)
        std::string    check;
        long long      seed;       
                       
        std::string    local_dir;    // directory to monitor for changes
        int            replicate;    // > 1 to publish chunks
        int            availability; // how many copies currently exist 

        Wt::WDateTime  last_update;

        template<typename Action>
        void persist( Action& a );
    };

    /**
     *  Used to maintain information about an individual tornet link such
     *  as its ID, CHECK and SEED along with other useful meta information 
     *  such as whether or not it is being published, how replicated is it
     *  currently, how often is it 'downloaded' and other stats. 
     */
    class TornetLink : public dbo::Dbo<TornetLink>, public dbo::ptr<TornetLink> {
      public:
        TornetLink( const fc::sha1& id, const fc::sha1& check, uint64_t seed, const fc::string& name, uint64_t size, uint64_t tsize );
        TornetLink(){}
        std::string tid;
        std::string check;
        long long   seed;             // random seed used to ensure data is random enough
        std::string name;
        long long   tornet_size;      // size of the tornet file, can accelerate downloads
        long long   file_size;        // size of the file described by the tornet file
        int         download_count;   // how many times it has been downloaded
        int         publishing;
        double      replicate_count;
        Wt::WDate   created;
        Wt::WDate   last_download;

        template<typename Action>
        void persist( Action& a );
    };
}

#endif // _TORNET_DBO_HPP
