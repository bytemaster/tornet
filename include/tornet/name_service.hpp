#pragma once

#include <fc/string.hpp>
#include <fc/sha1.hpp>
#include <fc/optional.hpp>
#include <fc/pke.hpp>
#include <fc/fwd.hpp>
#include <fc/filesystem.hpp>
#include <fc/shared_ptr.hpp>
#include <cafs.hpp>

namespace tn {
  namespace db{ class name; }
  class node;
  class publish_name_request;

  /**
   *  This service is used to register names. Each name
   *  maps to a sha1 value and a public key.  Locally names
   *  also include a private key;
   *
   *  Each public key can have exactly one name registered to it.
   */
  class name_service : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<name_service> ptr;

      name_service( const fc::path& sdir, const fc::shared_ptr<tn::node>& n );
      ~name_service();

      void download_block_chain();
      void shutdown();

      fc::shared_ptr<db::name> get_name_db()const;

      void publish_name( const publish_name_request& r );

      /**
       *  @return a list of all names reserved by this node.
       */
      fc::vector<fc::string> get_reserved_names()const;

      /**
       *  Instructs the names service to attempt to reserve a given name.  Returns the
       *  state of the reservation process.
       */
      void   reserve_name( const fc::string& name, const cafs::link& ln );

      /**
       *  This will make the name available to other users.
       */
      void   release_name( const fc::string& name );            

      /**
       *  Return the value assigned to a given name.
       */
      cafs::link   get_link_for_name( const fc::string& name );

      /**
       *  Get the name record.
       */
      //record       get_record_for_name( const fc::string& name );

      /** 
       *  Used to sign a digest gwith the given name.  Throws an exception
       *  if no private key is known for name.
       */
      fc::signature_t   sign_with_name( const fc::sha1& digest, const fc::string& name );

      /**
       *  Validates that the name digest was signed by name, throws an exception if
       *  name is unknown.
       */
      bool         validate_signature_by_name( const fc::sha1& digest, const fc::signature_t&, const fc::string& name );

      /**
       *  Updates the value associated with the name.
       */
      void         update_link_for_name( const fc::string& name, const cafs::link& l );
      void         transfer_name( const fc::string& name, const fc::public_key_t& to_key );

      /**
       *  Sets the level-of-efort as a 'percent of CPU' to use when searching for a
       *  given hash. This will affect the time it takes to publish/update a name.
       */
      void         setProcessingEffort( float e );
      float        getProcessingEffort();

      class impl;
    private:
      fc::fwd<impl,208> my;
  };

}
/**
 *  Goals of Name Service
 *    - no limit to the 'rate' of name registration except a 'fixed cost per name' in CPU/hrs.
 *    - what people are really 'mining' is names, names become the 'currency' 
 *      - all names are different and have different 'values' on the market.
 *    - keeping a name requires contributing to the validation of all names.
 *    - because all names are 'renewed' every N months, and everyone interested in those names
 *      is around to 'validate' all new blocks attackers cannot 'rewrite' the transaction history without
 *      invalidating all 'renewals' durring the 're-write' period.   
 *    - the more names that are registered, the more secure everyones name becomes.
 *    - because all names must 'report in' every N months, we can require that each new block
 *      require the signature of at least 3 'old names' chosen from renewals included in the
 *      block chain. 
 *    - squatters, desiring to profit from speculatively buying 'names' would up the difficulty and
 *    therefore, the 'reward' for mining is a name and names have value!  
 *    - Each name has a 'speculative' value and so long as the speculative value >= the cost of mining the
 *        difficulty will increase until the total number of CPU cycles equals the number of 'wanted names' +
 *        the number of 'speculative names'.   To steal a 'name' would cost more than the value of all
 *        other names combined. 
 *    - the cheaper someone can mine, the more 'speculative' names they will reserve, therefore difficulty
 *      will keep increasing 'without bound' so long as the 'demand for names' is increasing.
 *      
 *
 *
 *    - all miners know a piece of 'ground truth', their names, and reject any block that
 *      violates their truth, because updates require a 'signature' 
 *    - to steel a name, an attacker would have to 'prevent' a user from 'renewing' *or* have enough
 *      hashing power to re-write all blocks... but these blocks would be rejected by the other miners
 *      because they would, out of necesity, violate their 'truths'.  
 *    - to prevent a user from renewing you would have to prevent them from publishing
 *    - block difficulty includes per-trx difficulty, therefore, to solve a block all users want
 *      to include as many valid transactions as possible as this is the 'cheapest' way to
 *      validate your block.  
 *    - any block/transaction older than 2*N months can
 *      be discarded because the 'ground truth' from blocks that are N months old cannot be changed
 *      by an attacker because no 'good nodes' would accept that alternative history.  Mean while,
 *      good nodes would also reject any block 
 *      to be kept (only the headers)
 *
 *  Every transaction requires an average of 5 CPU minutes to prevent 'spam'. 
 *  Every name requires renewal every 3 months, yielding 20 CPU minutes/year to renew a name.
 *  The process of mining a 'name' also doubles as mining a 'block'.
 *
 *  If someone develops an ASIC they could 'register/squat' on names, but the number of names they
 *  could squat on is limited by their raw CPU power.  Difficulty of finding new blocks would go up.
 *  
 *
 *  hash( other_trxns + prev_block + block_header ) -> base
 *  hash( base +cur_trx+ nonce ) =>  validates a trx if less than X and a block if less than Y where Y <<<< X
 *
 *  So by searching for a hash to validate your transaction, you may also find something that will validate the block and
 *  because all users must continually search for hashes to maintain their 'name' the block chain will also get
 *  processed.  The more demand there is for names, the greater hashing power behind the block chain.
 *
 *  The only thing an 'attacker' can gain by having more hashing power is the ability to 'steal' names by 
 *  creating a new tree with higher hashing power...  
 *
 *  There are two people interested in names: owners and users.
 *  Owners want to prevent others from steeling their name, and therefore will invest resources into making
 *  sure that their name is 'secure'.  The 'cheapest' way of securing your name is to include other individuals
 *  transactions which in turn 'beef up' the secuirty of the entire chain.
 *
 *  To steel a name requires creating a new block chain that includes a different history equal to the total
 *  difficulty in finding working hashes for every transaction.  So each user's transaction counts as a 
 *  'proof of work' that an attacker would have to duplicate.  Because every transaction includes the prior
 *  block id in its hash, an attacker could not use other peoples tranactions in their chain and would therefore
 *  have to generate 'fake transactions'... but the other miners in the network would not accept any 'fake
 *  transactions' that disagreed with names they are protecting.  
 *
 *  herefore, an
 *  atacker would require more hashing power than the entire network AND the majority of existing name holders.
 *
 *
 *  Difficulty of the block equals the sum of the difficulty of all transactions included in the block, therefore,
 *  the more transactions you include, the more likely you are to 'solve' the block.... 
 *
 *  Why should you include other peoples transactions?  
 *  Why should you dedicate as much hashing power as possible? 
 *      - the more hashing power you contribute, the more secure your name is.
 *      - the more names 
 *
 *
 *  Each name has two values associated with it, a public key and a sha1 hash.
*/
