#ifndef _TORNET_APP_HPP_
#define _TORNET_APP_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/string.hpp>
#include <fc/vector.hpp>
#include <fc/fwd.hpp>
#include <fc/reflect.hpp>

namespace fc{ 
  namespace ip {
    class endpoint;
  }
}

namespace tn {
  class node;
  class name_service;
  class chunk_service;

  class tornet_app : public virtual fc::retainable {
    public:
      typedef fc::shared_ptr<tornet_app> ptr;

      struct config {
          uint16_t                 tornet_port;
          fc::string               data_dir;
          fc::vector<fc::string>   bootstrap_hosts;
      };

      tornet_app();
      ~tornet_app();

      static const ptr& instance();

      void shutdown();
      void configure( const config& c );

      const fc::shared_ptr<node>&          get_node()const;
      const fc::shared_ptr<name_service>&  get_name_service()const;
      const fc::shared_ptr<chunk_service>& get_chunk_service()const;

    private:
      class impl;
      fc::fwd<impl,24> my;
  };

} // namespace tn

FC_REFLECTABLE( tn::tornet_app::config )


#endif //  _TORNET_APP_HPP_
