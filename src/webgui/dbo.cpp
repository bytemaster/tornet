#include "persist.hpp"
#include <fc/string.hpp>
#include <fc/sha1.hpp>


namespace tn {
  PublishedResource::PublishedResource( const std::string& lpath, const std::string& lid ) 
  :local_path(lpath),site_ref(lid)
  {
  }
}
