#ifndef _TORNET_ARCHIVE_HPP_
#define _TORNET_ARCHIVE_HPP_
#include <fc/optional.hpp>
#include <fc/sha1.hpp>
#include <fc/vector.hpp>
#include <fc/static_reflect.hpp>
#include <tornet/link.hpp>

namespace fc {
  class path;
}

namespace tn {

  /**
   *  Querying the DHT is the bottleneck due to latency inherrent in iterative 
   *  lookup. To the extent possible we want to do the following:
   *
   *  1) group small static files into one chunk
   *      - if any of these files change, the entire chunk 'changes' so they shouldn't
   *        be modified often or should be organized into groups that change together.
   *
   *  2) create a directory index of tornet links for file names (don't use global key value pair)
   *
   *  3) Use global key-value DB for 'dynamic content', this is the slowest content
   *  
   *  As much as possible we want users using 'jquery' to use the same exact files so that their
   *  chunks are better cached.  
   *
   *  The 'brute force' method is to create a tornet file for every file regardless of size and put
   *  them in an index.
   *
   *
   *  An archive is simply a 'tree' of name / link pairs.
   */
  class archive {
    public:
      struct entry {
        fc::string  name;
        link        ref;
      };

      /**
       *  Given a path, search for the given entry in the
       *  archive.
       */
      const entry* find( const fc::path& p );
      void add( const fc::path& p, const tn::link& l );


      fc::vector<entry>     entries;
  };


}

FC_STATIC_REFLECT( tn::archive::entry, (name)(ref) )
FC_STATIC_REFLECT( tn::archive, (entries) )

#endif // _TORNET_ARCHIVE_HPP_
