#ifndef _TORNET_DB_NAME_HPP_
#define _TORNET_DB_NAME_HPP_
#include <tornet/name_chain.hpp>
#include <fc/shared_ptr.hpp>
#include <fc/filesystem.hpp>
#include <fc/static_reflect.hpp>

namespace tn { namespace db {

  /**
   *  A set of databases that manage the name reservation
   *  block chain.  
   *
   */
  class name : public fc::retainable {
    public:
      typedef fc::shared_ptr<name> ptr;

      struct record {
        fc::string         name;
        fc::public_key_t   pub_key;
        fc::sha1           value_id;
        fc::sha1           value_key;
        fc::sha1           last_update_block;
        uint32_t           update_count; // how many updates, more updates is higher rank
      };

      // track names I am using locally, these are the ones that
      // I will need to be generating to keep.
      struct private_name {
        enum name_state {
          unknown,      // the name is unknown to the network
          generating,   // actively searching for a nonce for this name
          allocating,   // a nonce has been found, attempting to publish the allocation
          allocated,    // the name has been anonymously allocated and included by 6 blocks
          publishing,   // after the name has been allocated, we must publish the name
          published,    // the publish update has been validated by 6 blocks
          expired,      // the name has expired
          in_use,       // the given name is already in use.
          transfering,
          transfered,
        };

        fc::public_key_t       pub_key;
        fc::private_key_t      priv_key;
        fc::array<char,128>    name;
        fc::array<uint64_t,2>  nonce;
        fc::sha1               rand;
        fc::sha1               value_id;
        fc::sha1               value_key;
        fc::sha1               res_trx;  // reservation trx
        fc::sha1               pub_trx;  // publish trx
        fc::sha1               last_trx; // last update / transfer trx
        int                    state;

        int                    priority;
      };

      name( const fc::path& dir );
      ~name();

      void init();
      void close();
      void sync();


      uint32_t num_private_names();
      bool     fetch_private_name( uint32_t recnum, private_name& );

      bool     fetch_record_for_name( const fc::string& name, record& r );
      bool     fetch_record_for_public_key( const fc::sha1& pk_sha1, record& r );
      bool     fetch_reservation_for_hash( const fc::sha1& res_id, name_reserve_trx& );
               
      bool     store( const private_name& pn );

      bool     store( const name_reserve_trx& );
      bool     store( const name_publish_trx& );
      bool     store( const name_update_trx& );
      bool     store( const name_transfer_trx& );
               
      bool     fetch( const fc::sha1& id, private_name& );
      bool     fetch( const fc::sha1& id, name_trx_header& );
      bool     fetch( const fc::sha1& id, name_reserve_trx& );
      bool     fetch( const fc::sha1& id, name_publish_trx& );
      bool     fetch( const fc::sha1& id, name_update_trx& );
      bool     fetch( const fc::sha1& id, name_transfer_trx& );
               
      bool     store( const name_block& b );
      bool     fetch( const fc::sha1& id, name_block& b );
               
      bool     fetch_trx_by_index( uint32_t recnum, fc::sha1& id, name_trx_header&  );
      bool     fetch_block_by_index( uint32_t recnum, fc::sha1& id, name_block&  );
      uint64_t max_block_num();
      uint32_t num_blocks();
      uint32_t num_trx();

      // return the hash of all blocks with a given number
      fc::vector<fc::sha1> fetch_blocks_with_number( uint64_t block_num );
      bool                 fetch_block_by_num( uint32_t recnum, fc::sha1& id, name_block&  );

      bool remove_trx( const fc::sha1& trx_id );
      bool remove_block( const fc::sha1& block_id );
      bool remove_private_name( const fc::sha1& name_id );

    private:
      class impl;
      fc::fwd<impl,216> my;
  };
  
} }

FC_STATIC_REFLECT( tn::db::name::private_name,
  (pub_key)(priv_key)(name)(nonce)(rand)(value_id)
  (value_key)(res_trx)(pub_trx)(last_trx)(state)(priority) )

#endif // _TORNET_DB_NAME_HPP_
