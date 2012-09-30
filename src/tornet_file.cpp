#include <tornet/tornet_file.hpp>
#include <fc/reflect_impl.hpp>
#include <fc/reflect_vector.hpp>

FC_REFLECT( tn::tornet_file::chunk_data,
  (size)(seed)(id)(slices) )

FC_REFLECT( tn::tornet_file,
  (version)(compression)(mime)(checksum)(name)(size)(chunks)(inline_data) )
