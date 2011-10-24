#include <QApplication>
#include <boost/cmt/log/log.hpp>
#include <boost/program_options.hpp>
#include <tornet/db/chunk.hpp>
#include <boost/exception/all.hpp>
#include <tornet/db/peer.hpp>
#include <boost/cmt/asio.hpp>
#include <tornet/net/node.hpp>
#include <tornet/services/calc.hpp>
#include <tornet/services/chat.hpp>


#include <boost/cmt/thread.hpp>
#include <boost/cmt/signals.hpp>
#include <tornet/rpc/service.hpp>

void handle_sigint( int si );


boost::asio::ip::udp::endpoint to_endpoint( const std::string& str ) {
      boost::asio::ip::udp::resolver r( boost::cmt::asio::default_io_service() );
      std::string host = str.substr( 0, str.find(':') );
      std::string port_id = str.substr( str.find(':')+1, str.size());
      std::string port = port_id.substr(0, port_id.find('-') );
      std::string id   = port_id.substr(port_id.find('-')+1, port_id.size() );
      std::vector<boost::asio::ip::udp::endpoint> eps = boost::cmt::asio::udp::resolve( r,host,port);
      return eps.front();
}

void start_services( int argc, char** argv ) {
  slog( "starting services..." );
      std::string data_dir;
      uint16_t    node_port = 0;
      std::vector<std::string> init_connections;

      namespace po = boost::program_options;
      po::options_description desc("Allowed options");
      desc.add_options()
        ("help,h", "print this help message." )
        ("listen,l", po::value<uint16_t>(&node_port)->default_value(node_port), "Port to run node on." )
        ("data_dir,d", po::value<std::string>(&data_dir)->default_value("data"), "Directory to store data" )
        ("bootstrap,b", po::value<std::vector<std::string> >(&init_connections), "HOST:PORT of a bootstrap node" )
      ;
      po::variables_map vm;
      po::store( po::parse_command_line(argc,argv,desc), vm );
      po::notify(vm);

      if( vm.count("help") ) {
          std::cout << desc << std::endl;
          exit(1);
      }
      
      tornet::node::ptr node(new tornet::node() );
  try {
      node->init( data_dir, node_port );
      
      tornet::rpc::service::ptr srv( new tornet::service::calc_service( node, "rpc", 101 ) );
     
      //tornet::service::chat::ptr cs( new tornet::service::chat( node, 100 ) );

      for( uint32_t i = 0; i < init_connections.size(); ++i ) {
        try {
            wlog( "Attempting to connect to %1%", init_connections[i] );
            scrypt::sha1 id = node->connect_to( to_endpoint(init_connections[i]) );
            wlog( "Connected to %1%", id );

            tornet::channel c = node->open_channel( id, 101 );
            tornet::rpc::connection con(c);
            int r = con.call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con.call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con.call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con.call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
        } catch ( const boost::exception& e ) {
            wlog( "Unable to connect to node %1%, %2%", init_connections[i], boost::diagnostic_information(e) ) ;
        }
      }

    //  boost::cmt::wait( quit_signal );
    wlog( "exec... " );
      boost::cmt::exec();
      //boost::cmt::usleep(1000*1000*1000);
    wlog( "done exec..." );
  } catch ( const boost::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e) );
  } catch ( const std::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e) );
  } catch ( ... ) {
    elog( "unhandled exceptioN!" );
  }
  elog( "gracefully shutdown node." );
  node->close();
  wlog( "\n\nexiting services!\n\n" );
}



int main( int argc, char** argv ) {
  signal( SIGINT, handle_sigint );
  boost::cmt::thread::current().set_name("main");
  try { 
      //boost::shared_ptr<tornet::db::chunk> c( new tornet::db::chunk( scrypt::sha1(), boost::filesystem::path(data_dir)/"chunks" ) );
      //c->init();
      boost::cmt::thread* sthread = boost::cmt::thread::create("service");
      sthread->async( boost::bind(&start_services,argc,argv) );

      QApplication app(argc, argv);
      app.exec();
      //quit_signal();
      slog( "wating for services to clean up" );
      sthread->quit();
//      wlog( "wait for services..." );
//      usleep(1000*1000);
      slog( "Application quit... closing databases" );
  } catch ( const boost::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e)  );
    return -1;
  } catch ( const std::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e)  );
    return -1;
  } catch ( ... ) {
    elog( "Unknown exception" );
    return -1;
  }
  slog( "Exiting cleanly" );
  return 0;
}
void handle_sigint( int si ) {
  slog( "%1%", si );
  static int count = 0;
  if( !count ){ 
    QCoreApplication::exit(0);
  }
  else exit(si);
  ++count;
}
