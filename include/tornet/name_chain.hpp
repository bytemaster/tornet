#ifndef _TORNET_NAME_CHAIN_HPP_
#define _TORNET_NAME_CHAIN_HPP_
#include <fc/reflect.hpp>
#include <fc/pke.hpp>
#include <fc/sha1.hpp>
#include <fc/vector.hpp>
#include <fc/string.hpp>
#include <fc/array.hpp>
#include <fc/raw.hpp>

#include <cafs.hpp>

namespace tn {


  /**
   *  There are several transaction types:
   *
   *  1) reserve   public_key + hash( name + rand )              + signature
   *  2) publish   name + rand + value_sha1 + key_sha1           + signature 
   *  3) update    hash( name + rand ) + value_sha1 + key_sha1   + signature 
   *  4) transfer  hash( name + rand ) + new_public_key          + signature
   */
  struct name_trx_header {
    uint8_t               type;
    fc::sha1              prev_block_id;
    fc::sha1              base;      // hash( prev_block_id + block_num + other trx )
    uint64_t              nonce;
    fc::signature_t       signature; // 256
  };

  struct name_reserve_trx {
    enum type_num { id = 1 };
    name_trx_header    head;      // 300
    fc::sha1           res_id;    // 40
    fc::public_key_t   pub_key;   // 256
  };

  struct name_publish_trx {
    enum type_num { id = 2 };
    name_trx_header        head; // 300
    fc::sha1               reserve_trx_id; 
    fc::array<char,128>    name; // null term name (max width 128 chars)
    fc::array<uint64_t,2>  rand; 
    cafs::link             site_ref;
  };

  struct name_update_trx {
    enum type_num { id = 3 };
    name_trx_header    head;
    fc::sha1           name_id;       // hash(name)
    fc::public_key_t   pub_key;       // 256
    uint32_t           update_count;  // increments every time the name updates
    cafs::link           site_ref;
  };

  struct name_transfer_trx {
    enum type_num { id = 4 };
    name_trx_header    head;
    fc::sha1           name_id; // hash(name)
    fc::public_key_t   to_pub_key;
  };

  /**
   *  Before a user can publish a transaction, the
   *  must find a hash for their transaction that is
   *  below some minimal threshold to show proof of
   *  work.  
   *
   *  The act of finding hashes for your transaction also
   *  helps you find hashes to solve the block because the
   *  hash of your transaction contains the hash of the
   *  block header.  
   *
   *  The difficulty of the block is the sum of all transaction
   *  difficulties.  To win the block you must include the most
   *  transactions from other users.  Failure to do so means that
   *  someone else could 'bump' your block and you would have to
   *  restart solving for your transaction.
   */
  struct name_block {
    name_block()
    :utc_us(0),block_num(0),block_date(0){}

    /// hash of these values form the base
    fc::sha1              prev_block_id;
    uint64_t              utc_us;  // approx time the block was generated  
    uint64_t              block_num; 
    uint64_t              block_date;
    fc::vector<fc::sha1>  transactions; 

    fc::sha1 base_hash()const {
      fc::sha1::encoder enc;
      enc.write( prev_block_id.data(), sizeof(prev_block_id) );
      enc.write( (char*)&utc_us, sizeof(utc_us) );
      enc.write( (char*)&block_num, sizeof(block_num) );
      fc::raw::pack( enc, transactions );
      return enc.result();
    }
    uint64_t difficulty() {
      return transactions.size();
    }

    /// this is the transaction that solved the block.
    fc::sha1              gen_transaction;
  };


  template<typename Trx>
  bool validate_trx_hash( const Trx& tran, const fc::sha1& thresh ) {
     fc::sha1::encoder enc;
     fc::raw::pack( enc, tran );
     return  enc.result() < thresh;
  }

  /**
   *  For a block hash to be valid, the gen_transaction must use 
   *  the hash of the base header fields for is 'base' and the
   *  hash of the transaction must be below thresh
   */
  template<typename Trx>
  bool validate_block_hash( const name_block& b, const Trx& gen, const fc::sha1& block_thresh, const fc::sha1& trx_thresh ) {
     if( gen.head.base != b.base_hash() ) 
        return false;

     // make sure that gen is really the one used.
     fc::sha1::encoder enc;
     fc::raw::pack( enc, gen );
     
     if( b.gen_transaction != enc.result() )
        return false;


     if( enc.result() >= block_thresh ) return false;
     
     // every trx must be below the desired trx_thresh
     for( auto itr = b.transactions.begin(); itr != b.transactions.end(); ++itr )
        if( *itr > trx_thresh ) return false;

     return true;
  }

  
  /**
   *  Searches for a nonce that will make the transaction hash below thresh.
   *  @param start1 where to start searching for the nonce
   *  @param done   a volatile boolean that can be used to exit the search early
   */
  template<typename Trx>
  uint64_t find_nonce( Trx& tran, uint64_t start, uint64_t end, const fc::sha1& thresh, volatile bool& done  ) {
    tran.head.nonce = start;
    while( !done && tran.head.nonce < end ) {
     if( validate_trx_hash( tran, thresh ) )
        return start;
      tran.head.nonce++;
    }
    return start;
  }

}

FC_REFLECT( tn::name_trx_header,   (base)(nonce)(type)(signature) )
FC_REFLECT( tn::name_reserve_trx,  (head)(pub_key)(res_id) )
FC_REFLECT( tn::name_publish_trx,  (head)(name)(rand)(site_ref) )
FC_REFLECT( tn::name_update_trx,   (head)(name_id)(update_count)(site_ref) )
FC_REFLECT( tn::name_transfer_trx, (head)(name_id)(to_pub_key) )
FC_REFLECT( tn::name_block,        (prev_block_id)(utc_us)(block_num)(transactions)(gen_transaction) )


#endif // _TORNET_NAME_CHAIN_HPP_
