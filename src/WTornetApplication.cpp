#include <tornet/WTornetApplication.hpp>
#include <tornet/tornet_app.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/node.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception.hpp>
#include <Wt/WTableView>
#include <Wt/WSortFilterProxyModel>
#include <Wt/WText>
#include <Wt/WPushButton>
#include <Wt/WLineEdit>
#include <Wt/WMessageBox>
#include "WUserItemModel.hpp"
#include "sql_session.hpp"
#include <Wt/Dbo/QueryModel>

#include <fc/thread.hpp>
#include "persist.hpp"

using namespace Wt;

class WTornetApplication::impl {
  public:
    impl()
    :session(tn::tornet_app::instance()->get_node()->datadir() ){}
    
    Wt::WLineEdit*        path_edit;
    Wt::WLineEdit*        site_path_edit;
    Wt::WLineEdit*        site_domain_edit;
    tn::sql_session       session;
};

  

WTornetApplication::WTornetApplication( const Wt::WEnvironment& env )
:WApplication(env){
    setTitle("Tornet"); 
    setCssTheme( "polished" );
    wlog("...");
  try {
    auto cont = new Wt::WContainerWidget(  );
  //  new Wt::WText( WString::tr("Hello WOrld"), cont );

    Wt::WTableView* t = new Wt::WTableView(cont);
    t->setAlternatingRowColors(true);
    t->setSortingEnabled(true);


    WUserItemModel* uim = new WUserItemModel( tn::tornet_app::instance()->get_node()->get_peers(), t );
    
    auto proxy = new Wt::WSortFilterProxyModel(this);
    proxy->setSourceModel(uim);
    t->setModel( proxy );

    root()->addWidget( new Wt::WText( "Publish File " ) );
    my->path_edit = new Wt::WLineEdit(root());
    my->path_edit->setFocus();

    auto publish = new Wt::WPushButton( "Publish", root() );
    root()->addWidget( new Wt::WBreak() );
    publish->clicked().connect(this, &WTornetApplication::onPublish ); 
    my->path_edit->enterPressed().connect( this, &WTornetApplication::onPublish );


    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Peers" ) );
    root()->addWidget( t );

    auto link_model = new Wt::Dbo::QueryModel< Wt::Dbo::ptr<tn::TornetLink> >(this);
    link_model->setQuery( my->session.db().find<tn::TornetLink>() );
    link_model->addAllFieldsAsColumns();

    WTableView* v = new WTableView();
    v->setModel( link_model );
    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Files" ) );
    root()->addWidget(v);

    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Sites" ) );
    root()->addWidget( new Wt::WBreak() );

    root()->addWidget( new Wt::WText( "Domain " ) );
    my->site_domain_edit = new Wt::WLineEdit(root());

    root()->addWidget( new Wt::WText( "Document Root  " ) );
    my->site_path_edit = new Wt::WLineEdit(root());


    auto publish_site = new Wt::WPushButton( "Publish Site", root() );
    publish_site->clicked().connect(this, &WTornetApplication::onPublishSite ); 
    root()->addWidget( new Wt::WBreak() );


    auto ts_model = new Wt::Dbo::QueryModel< Wt::Dbo::ptr<tn::Torsite> >(this);
    ts_model->setQuery( my->session.db().find<tn::Torsite>() );
    ts_model->addAllFieldsAsColumns();

    WTableView* tsv = new WTableView();
    tsv->setModel( ts_model );
    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Files" ) );
    root()->addWidget(tsv);

  } catch ( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
  }
}

WTornetApplication::~WTornetApplication() {
}

void WTornetApplication::onPublish() {
  try {

      fc::string path = my->path_edit->text().toUTF8().c_str();
//      tn::tornet_app::instance()->get_node()->get_thread().async( [=]() {
          fc::sha1 tn_id;
          fc::sha1 check;
          uint64_t  seed;
          slog( ".............  Publish Tornet '%s'", path.c_str() );
          tn::tornet_app::instance()->get_chunk_service()->publish( path, 3 );
          //tn::tornet_app::instance()->get_chunk_service()->publish_tornet( tn_id, check, seed, 3 );
 //     } );
  } catch ( ... ) {
      Wt::WMessageBox::show( "Error", fc::current_exception().diagnostic_information().c_str(), Wt::Ok  );
  }
}

void WTornetApplication::onPublishSite() {
  fc::string path = my->site_path_edit->text().toUTF8().c_str();
  fc::string domain = my->site_domain_edit->text().toUTF8().c_str();


  //tn::tornet_app::instance()->get_chunk_service()->publish( path, domain );
}



Wt::WTableView* WTornetApplication::createChunkTable( ) {
  Wt::WTableView* t = new Wt::WTableView();
  t->setAlternatingRowColors(true);

  WUserItemModel* uim = new WUserItemModel( tn::tornet_app::instance()->get_node()->get_peers() );
  t->setModel( uim );

  t->setColumnWidth(0, 100);
  t->setColumnWidth(1, 150);
  t->setColumnWidth(2, 100);
  t->setColumnWidth(3, 60);
  t->setColumnWidth(4, 100);
  t->setColumnWidth(5, 100);

  return t;
}
