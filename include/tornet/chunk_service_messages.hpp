#ifndef _CHUNK_SERVICE_MESSAGES_HPP_
#define _CHUNK_SERVICE_MESSAGES_HPP_
#include <fc/static_reflect.hpp>
#include <fc/reflect_fwd.hpp>
#include <fc/sha1.hpp>
#include <fc/vector.hpp>

namespace tn {

    struct message_header {
      int16_t req_num; // positive for request, neg for response
      uint8_t method;
    };
    enum chunk_service_methods {
      fetch_method_id = 1, 
      store_method_id = 2 
    };

    struct chunk_session_result {
        enum result_enum {
          ok                   = 0,
          available            = 1,
          invalid_rank         = 2,
          invalid_size         = 3,
          invalid_range        = 4,
          credit_limit_reached = 5,
          unknown_chunk        = 6,
          already_stored       = 7,
          rejected             = 8,
          unknown              = 9
        };
    };

    struct fetch_request {
      fetch_request(){}
      fetch_request( const fc::sha1& s, int32_t len, int32_t off )
      :target(s),length(len),offset(off){}

      fc::sha1                  target; ///< the target chunk we are searching for
      int32_t                   length; ///< the number of bytes to fetch, if -1 return the entire chunk
      int32_t                   offset; ///< the offset from the start of the chunk
    };

    struct fetch_response {
      fetch_response( chunk_session_result::result_enum e = chunk_session_result::unknown, int64_t new_bal = 0 )
      :result(e),offset(0),balance(new_bal),query_interval(0),deadend_count(0){}

      int8_t                    result;         ///!< see chunk_session_result::result_enum
      uint32_t                  offset;         ///!< offset from start of the data
      fc::vector<char>          data;           ///!< actual data of the chunk
      int64_t                   balance;        ///!< current balance/credit on this node
      int64_t                   query_interval; ///!< how often this chunk is queried on this node
      uint32_t                  deadend_count;  ///!< number of sequential unsuccessful searches for this chunk by this node
    };

    struct store_response {
      store_response( int8_t r = 0 ):result(r){}
      int8_t result;
    };

}

FC_STATIC_REFLECT( tn::fetch_request, (target)(length)(offset) )
FC_STATIC_REFLECT( tn::fetch_response, (result)(offset)(data)(balance)(query_interval)(deadend_count) )
FC_STATIC_REFLECT( tn::store_response, (result) )
FC_REFLECTABLE( tn::fetch_request )
FC_REFLECTABLE( tn::fetch_response )
FC_REFLECTABLE( tn::store_response )
#endif // _CHUNK_SERVICE_MESSAGES_HPP_
