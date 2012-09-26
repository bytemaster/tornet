#ifndef _TORNET_NAME_CHAIN_HPP_
#define _TORNET_NAME_CHAIN_HPP_
#include <fc/static_reflect.hpp>
#include <fc/pke.hpp>
#include <fc/sha1.hpp>
#include <fc/vector.hpp>
#include <fc/string.hpp>
#include <fc/array.hpp>

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
    fc::array<uint64_t,2> nonce;  // 16
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
    fc::sha1                chunk_id;  
    fc::sha1                chunk_key;
  };
  struct name_update_trx {
    enum type_num { id = 3 };
    name_trx_header    head;
    fc::sha1           name_id;       // hash(name)
    fc::public_key_t   pub_key;       // 256
    uint32_t           update_count;  // increments every time the name updates
    fc::sha1           chunk_id;
    fc::sha1           chunk_key;
  };

  struct name_transfer_trx {
    enum type_num { id = 4 };
    name_trx_header    head;
    fc::sha1           name_id; // hash(name)
    fc::public_key_t   to_pub_key;
  };

  union name_transaction {
    name_reserve_trx   reserve;
    name_publish_trx   publish;
    name_update_trx    update;
    name_transfer_trx  transfer;
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
    /// hash of these values form the base
    fc::sha1              prev_block_id;
    uint64_t              block_num; 
    uint64_t              block_date;
    fc::vector<fc::sha1>  transactions; 

    /// this is the transaction that solved the block.
    fc::sha1              gen_transaction;
  };

};

FC_STATIC_REFLECT( tn::name_trx_header,   (base)(nonce)(type)(signature) )
FC_STATIC_REFLECT( tn::name_reserve_trx,  (head)(pub_key)(res_id) )
FC_STATIC_REFLECT( tn::name_publish_trx,  (head)(name)(rand)(chunk_id)(chunk_key) )
FC_STATIC_REFLECT( tn::name_update_trx,   (head)(name_id)(chunk_id)(chunk_key) )
FC_STATIC_REFLECT( tn::name_transfer_trx, (head)(name_id)(to_pub_key) )
FC_STATIC_REFLECT( tn::name_block,        (prev_block_id)(block_num)(transactions)(gen_transaction) )


#endif // _TORNET_NAME_CHAIN_HPP_
