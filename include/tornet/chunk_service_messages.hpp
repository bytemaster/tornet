#ifndef _CHUNK_SERVICE_MESSAGES_HPP_
#define _CHUNK_SERVICE_MESSAGES_HPP_
namespace tn {

    struct chunk_session_result {
        enum result_enum {
          ok                   = 0,
          available            = 1,
          invalid_rank         = 2,
          invalid_size         = 3,
          credit_limit_reached = 4,
          unknown_chunk        = 5,
          already_stored       = 6,
          rejected             = 7,
          unknown              = 8
        };
    };

    struct fetch_response {
      fetch_response( chunk_session_result::result_enum e = chunk_session_result::unknown, int64_t new_bal = 0 )
      :result(e),offset(0),balance(new_bal),query_interval(0),deadend_count(0){}

      int8_t                    result;         ///!< see chunk_session_result::result_enum
      uint32_t                  offset;         ///!< offset from start of the data
      fc::vector<char>          data;           ///!< actual data of the chunk
      fc::vector<fc::sha1>      references;     ///!< nodes that are known to host the content... 
                                                ///< @todo consider removing now that a method to pay to publish is available
      int64_t                   balance;        ///!< current balance/credit on this node
      int64_t                   query_interval; ///!< how often this chunk is queried on this node
      uint32_t                  deadend_count;  ///!< number of sequential unsuccessful searches for this chunk by this node
    };

}
#endif // _CHUNK_SERVICE_MESSAGES_HPP_
