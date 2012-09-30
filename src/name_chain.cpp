#include <tornet/name_chain.hpp>

namespace tn {

    
    fc::sha1 name_block::calculate_base_hash()const {
      fc::sha1::encoder enc;
      enc.write( prev_block_id.data(), sizeof(prev_block_id) );
      enc.write( &utc_us, sizeof(utc_us) );
      enc.write( &block_num, sizeof(block_num) );
      fc::raw::pack( enc, transactions );
      return enc.result();
    }
}
