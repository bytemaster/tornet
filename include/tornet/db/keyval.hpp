namespace tn { namespace db { 


  /**
   *  The keyval database allows users to store keys that anyone
   *  can read, but only the owner can write.  The owner is identified by
   *  a public key, the value is a JSON string up to 1KB and the key is
   *  any valid URL.
   *
   *  TORNET uses the following scheme:
   *
   *  tornet://${name}/${key}
   *
   *  Which gets translated into:
   *
   *  tornet://sha1(pub_key_for_name(${name}))/sha1($key)
   *
   *  The database tracks the frequency that keys are accessed and decides which
   *  keys to store based upon 'supply and demand'.  L
   *
   */
  class keyval : virtual public fc::retainable {
    public:
      keyval();
      ~keyval();

      struct meta {
          meta();
          uint64_t first_update;
          uint64_t last_update;
          uint64_t query_count;
          uint32_t size;
          uint8_t  distance_rank;

          uint64_t now()const;
          uint64_t access_interval()const;
          int64_t  price()const;
          int64_t  annual_query_count()const;
          int64_t  annual_revenue_rate()const;
      };

      struct meta_record {
        fc::sha1 key;
        meta    value;
      };

      struct record {
          fc::sha1              user_id;   // sha1(publickey)
          fc::sha1              key_id;    // sha1(key)
          fc::array<char,128>   key;       // must be a valid 'path'
          fc::array<char,1024>  value;     // must be json
          uint32_t              expires;   // when the value 'expires'

          fc::signature_t       signature; // used to validate entry
      };

      void fetch_worst_performance( fc::vector<meta_record>& m, int n = 1 );
      void fetch_best_opportunity( fc::vector<meta_record>& m, int n = 1 );

      bool fetch_meta( const fc::sha1& id, meta& m, bool auto_inc );
      bool store_meta( const fc::sha1& id, const meta& m );

      bool del_data( const fc::sha1& id );
      bool del_meta( const fc::sha1& id );
      bool del( const fc::sha1& id );

      bool     fetch( const fc::sha1& idx, record& rec );
      fc::sha1 store( const fc::public_key_t& _user, const fc::string& _key, const fc::string& _val, const fc::signature_t& s );

    private:
      class impl;
      fc::fwd<impl,8> my;
  };


} } } // tn::db
