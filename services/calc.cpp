#include <boost/reflect/any_ptr.hpp>
#include "calc.hpp"

//BOOST_REFLECT_ANY( tornet::service::calc_connection,
//  (add)
//)

namespace tornet { namespace service {

  boost::any calc_service::init_connection( const rpc::connection::ptr& con ) {
      boost::shared_ptr<calc_connection> cc(new calc_connection(this) );
      boost::reflect::any_ptr<calc_connection> acc(cc);

      uint16_t mid = 0;
      boost::reflect::visit(acc, service::visitor<calc_connection>(*con, acc, mid) );

      return acc;
  }

} } // tornet::rpc
