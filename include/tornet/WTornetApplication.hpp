#ifndef _WTORNET_APPLICATION_HPP_
#define _WTORNET_APPLICATION_HPP_
#include <Wt/WApplication>
#include <fc/fwd.hpp>

namespace Wt {
  class WTableView;
}

class WTornetApplication : public Wt::WApplication {
  public:
    WTornetApplication( const Wt::WEnvironment& env );
    ~WTornetApplication();

    Wt::WTableView* createChunkTable();
    void onPublish();
    void onPublishSite();
  private:
    class impl;
    fc::fwd<impl,1000> my;
};


#endif // _WTORNET_APPLICATION_HPP_
