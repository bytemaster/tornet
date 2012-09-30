#include <tornet/archive.hpp>
#include <fc/filesystem.hpp>

namespace tn {
   const archive::entry* archive::find( const fc::path& p ) {
    return 0;
   }
   void archive::add( const fc::path& p, const tn::link& l ) {
    entries.push_back( entry() );
    entries.back().name = p.string();
    entries.back().ref = l;
   }
}
