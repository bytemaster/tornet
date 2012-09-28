#include <tornet/WTornetApplication.hpp>
#include <fc/fwd_impl.hpp>

#include <Wt/WTableView>

using namespace Wt;

class WTornetApplication::impl {
  public:

};

WTornetApplication::WTornetApplication( const Wt::WEnvironment& env )
:WApplication(env){
    setTitle("Tornet"); 
}

WTornetApplication::~WTornetApplication() {
}

Wt::WTableView* WTornetApplication::createChunkTable( ) {
  Wt::WTableView* t = new Wt::WTableView();
  t->setAlternatingRowColors(true);

  t->setColumnWidth(0, 100);
  t->setColumnWidth(1, 150);
  t->setColumnWidth(2, 100);
  t->setColumnWidth(3, 60);
  t->setColumnWidth(4, 100);
  t->setColumnWidth(5, 100);

  return t;
}
