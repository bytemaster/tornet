#ifndef _TORNET_DB_NAME_HPP_
#define _TORNET_DB_NAME_HPP_
#include <tornet/name_chain.hpp>
#include <tornet/link.hpp>
#include <fc/shared_ptr.hpp>
#include <fc/filesystem.hpp>
#include <fc/static_reflect.hpp>

namespace tn { namespace db {

  /**
   *  The name database enforces that all records have a proper
   *  hash, expiration date, and signature and represents the
   *  known state.
   */
  class name : public fc::retainable {
    public:
      typedef fc::shared_ptr<name> ptr;

      struct record {
         record()
         :expires(0),revision(0),nonce(0){}
         tn::link            site_ref;
         uint32_t            expires;
         uint32_t            revision;  // how many times has this record been updated
         fc::public_key_t    pub_key;
         uint64_t            nonce;
         fc::signature_t     sig; // includes nonce, required to prove key approved site_ref
      };

      struct record_key {
        record_key( const fc::public_key_t& pub, const fc::private_key_t& priv )
        :pub_key(pub),priv_key(priv){}
        record_key(){}

        fc::public_key_t     pub_key;
        fc::private_key_t    priv_key;
      };

      name( const fc::path& dir );
      ~name();

      void init();
      void close();
      void sync();

      bool     fetch( const fc::string& n, record& r );
      bool     fetch( const fc::string& n, record_key& r );

      bool     store( const fc::string& n, const record& r );
      bool     store( const fc::string& n, const record_key& r );

      bool     fetch( uint32_t recno, fc::string& n, record_key& r );
      bool     fetch( uint32_t recno, fc::string& n, record& r );

      bool     remove_record( const fc::sha1& id );
      bool     remove_key( const fc::sha1& id );

      uint32_t record_count();
      uint32_t record_key_count();
    private:
      class impl;
      fc::fwd<impl,216> my;
  };
  
} }

FC_STATIC_REFLECT( tn::db::name::record_key, (pub_key)(priv_key) )

#endif // _TORNET_DB_NAME_HPP_
