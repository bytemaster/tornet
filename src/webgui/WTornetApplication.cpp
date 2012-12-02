#include "WTornetApplication.hpp"
#include <tornet/tornet_app.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/name_service.hpp>
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
    
    Wt::WContainerWidget* published_resources;
    Wt::WLineEdit*        path_edit;
    Wt::WLineEdit*        site_path_edit;
    Wt::WLineEdit*        site_domain_edit;
    WTableView*           links;
    Wt::Dbo::QueryModel<Wt::Dbo::ptr<tn::PublishedResource> >* link_model;
    tn::sql_session       session;
};

  

WTornetApplication::WTornetApplication( const Wt::WEnvironment& env )
:WApplication(env){
    setTitle("Tornet"); 
    setCssTheme( "polished" );
    wlog("...");
  try {
    auto cont = new Wt::WContainerWidget(  );

    Wt::WTableView* t = new Wt::WTableView(cont);
    t->setAlternatingRowColors(true);
    t->setSortingEnabled(true);

    WUserItemModel* uim = new WUserItemModel( tn::tornet_app::instance()->get_node()->get_peers(), t );
    
    auto proxy = new Wt::WSortFilterProxyModel(this);
    proxy->setSourceModel(uim);
    t->setModel( proxy );

    root()->addWidget( new Wt::WText( "Publish File or Site" ) );
    my->path_edit = new Wt::WLineEdit(root());
    my->path_edit->setWidth( 300 );
    my->path_edit->setFocus();

    auto publish = new Wt::WPushButton( "Publish", root() );
    root()->addWidget( new Wt::WBreak() );
    publish->clicked().connect(this, &WTornetApplication::onPublish ); 
    my->path_edit->enterPressed().connect( this, &WTornetApplication::onPublish );


    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Peers" ) );
    root()->addWidget( t );

    my->link_model = new Wt::Dbo::QueryModel< Wt::Dbo::ptr<tn::PublishedResource> >(this);
    my->link_model->setQuery( my->session.db().find<tn::PublishedResource>() );
    my->link_model->addAllFieldsAsColumns();

    my->links = new WTableView();
    my->links->setModel( my->link_model );
    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Files" ) );
    root()->addWidget(my->links);

    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Sites" ) );
    root()->addWidget( new Wt::WBreak() );

    root()->addWidget( new Wt::WText( "Domain " ) );
    my->site_domain_edit = new Wt::WLineEdit(root());
    my->site_domain_edit->setWidth(300);

    root()->addWidget( new Wt::WText( "Document Root  " ) );
    my->site_path_edit = new Wt::WLineEdit(root());
    my->site_path_edit->setWidth(450);


    auto publish_site = new Wt::WPushButton( "Publish Site", root() );
    publish_site->clicked().connect(this, &WTornetApplication::onPublishSite ); 
    root()->addWidget( new Wt::WBreak() );

    auto ts_model = new Wt::Dbo::QueryModel< Wt::Dbo::ptr<tn::Domain> >(this);
    ts_model->setQuery( my->session.db().find<tn::Domain>() );
    ts_model->addAllFieldsAsColumns();

    WTableView* tsv = new WTableView();
    tsv->setModel( ts_model );
    root()->addWidget( new Wt::WBreak() );
    root()->addWidget( new WText( "Domains" ) );
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
      slog( ".............  Publish Tornet '%s'", path.c_str() );
      auto ln =  tn::tornet_app::instance()->get_chunk_service()->publish( path, 3 );
      
      {
          Wt::Dbo::Transaction trx(my->session.db());
      //    slog( "link fetch/%s/%llu", fc::string(ln.id).c_str(), ln.seed );
      wlog( "link %s.chunk", fc::string(ln).c_str() );
          auto pr = new tn::PublishedResource( path.c_str(), ("http://"+fc::string(ln)+".chunk").c_str()  );
          my->session.db().add(pr);
      }
      my->link_model->modelReset()();

  } catch ( ... ) {
      Wt::WMessageBox::show( "Error", fc::current_exception().diagnostic_information().c_str(), Wt::Ok  );
  }
}

void WTornetApplication::onPublishSite() {
  fc::string path = my->site_path_edit->text().toUTF8().c_str();
  fc::string domain = my->site_domain_edit->text().toUTF8().c_str();


  slog( ".............  Publish Site '%s'", path.c_str() );
  auto ln =  tn::tornet_app::instance()->get_chunk_service()->publish( path, 3 );
  
  {
      Wt::Dbo::Transaction trx(my->session.db());
      wlog( "link %s", fc::string(ln).c_str() );

      auto pr = new tn::PublishedResource( path.c_str(),  ("http://"+fc::string(ln)+".chunk").c_str()  );
      pr->availability = 0;
      auto r = my->session.db().add(pr);

      auto dn = new tn::Domain();
      dn->name   = domain.c_str();
      dn->status = "published";
      dn->expires =  Wt::WDateTime();
      dn->resource = r;
      my->session.db().add(dn);
  }
  my->link_model->modelReset()();

  tn::tornet_app::instance()->get_name_service()->reserve_name(domain,ln);

  tn::tornet_app::instance()->get_chunk_service()->enable_publishing(true);
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
