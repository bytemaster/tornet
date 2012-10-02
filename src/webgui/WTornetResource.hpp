#ifndef _WTORNET_RESOURCE_HPP_
#define _WTORNET_RESOURCE_HPP_
#include <Wt/WResource>
#include <fc/fwd.hpp>

class WTornetResource : public Wt::WResource {
  public:
    WTornetResource( Wt::WObject* p = 0 );
    ~WTornetResource();

    virtual void handleRequest( const Wt::Http::Request& request, Wt::Http::Response& r );

  private:
    class impl;
    fc::fwd<impl,8> my;
};



#endif // _WTORNET_RESOURCE_HPP_
