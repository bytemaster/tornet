#include "persist.hpp"
#include <fc/string.hpp>
#include <fc/sha1.hpp>


namespace tn {
  PublishedResource::PublishedResource( const std::string& lpath, const std::string& lid, uint64_t sd ) 
  :local_path(lpath),link_id(lid),link_seed(sd)
  {
  }
}
