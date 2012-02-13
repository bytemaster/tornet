#ifndef _REFLECT_CHUNK_SESSION_HPP_
#define _REFLECT_CHUNK_SESSION_HPP_

#include <boost/reflect/any_ptr.hpp>
#include <tornet/services/chunk_session.hpp>

BOOST_REFLECT( fetch_response,
  (result)(offset)(data)(references)(balance)(query_interval)(deadend_count) )
BOOST_REFLECT( store_response, (result)(balance) )

BOOST_REFLECT_ANY( chunk_session,
  (get_credit_limit)
  (get_balance)
  (get_publish_price)
  (publish_rank)
  (fetch)
  (store)
  (store_reference)
  (get_bitcoin_address)
  (get_credit_per_btc)
)

#endif 
