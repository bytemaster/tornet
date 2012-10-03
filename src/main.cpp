#include <tornet/tornet_app.hpp>
#include <Wt/WApplication>
#include <Wt/WServer>
#include <Wt/Json/Value>
#include <Wt/Json/Object>
#include <Wt/Json/Array>
#include <Wt/Json/Parser>
#include <fc/exception.hpp>
#include <fc/thread.hpp>
#include "webgui/WTornetApplication.hpp"
#include "webgui/WTornetResource.hpp"
#include <tornet/httpd.hpp>

Wt::WApplication* create_application( const Wt::WEnvironment& env ) {
  return new WTornetApplication(env);
}


int main( int argc, char** argv ) {
  try {
      fc::thread::current().set_name("main");

      Wt::WServer server(argv[0]);
      server.setServerConfiguration(argc, argv, WTHTTP_CONFIGURATION);

      tn::tornet_app::config tcfg;
      std::string json_cfg;
      std::string httpd_port;

      server.readConfigurationProperty( "tornet", json_cfg );
      server.readConfigurationProperty( "httpd_port", httpd_port );

      Wt::Json::Value jval;
      Wt::Json::parse( json_cfg, jval );
      Wt::Json::Object jobj = jval;

      tcfg.data_dir = std::string(jobj.get("data_dir")).c_str();

      tcfg.tornet_port = (int)jobj.get("tornet_port");
      Wt::Json::Array bs = jobj.get("bootstrap_hosts");
      for( auto i = bs.begin(); i != bs.end(); ++i )
        tcfg.bootstrap_hosts.push_back( std::string(*i).c_str() );
        
      tn::tornet_app::instance()->configure( tcfg );

      fc::thread httpd_thread("httpd");
      httpd_thread.async( [=](){ 
          try {
           std::shared_ptr<tn::httpd> proxy(new tn::httpd(boost::lexical_cast<uint16_t>(httpd_port)));
           fc::usleep( fc::microseconds( 1000ll*1000ll*60ll*60*24*365*10) );
          } catch ( ... ) { elog( "%s", fc::except_str().c_str() ); }
      });

      server.addEntryPoint(Wt::Application, []( const Wt::WEnvironment& env ) { return create_application(env); }, "", "/favicon.ico" );
      server.addResource( new WTornetResource(), "/fetch" );

      int r = -1;
      if (server.start()) {
          r = Wt::WServer::waitForShutdown(); 
          server.stop();
      } 
      tn::tornet_app::instance()->shutdown();
      return r;
  } catch ( const std::exception& e ) {
      std::cerr<< fc::current_exception().diagnostic_information().c_str()<< std::endl;
      std::cerr<<e.what()<<std::endl;
  } catch ( ... ) {
      std::cerr<< fc::current_exception().diagnostic_information().c_str()<< std::endl;
  }
  return -1;
}
