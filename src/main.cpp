#include <fc/thread.hpp>
#include <fc/exception.hpp>
#include <fc/log.hpp>
#include <fc/signals.hpp>

#include <tornet/node.hpp>
#include <tornet/kad.hpp>


#include <boost/program_options.hpp>

#include <signal.h>

void handle_sigint( int si );

boost::signal<void()> quit;

void start_services( int argc, char** argv ) {
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
      //QCoreApplication::exit(1);
      exit(1);
      return;
  }
  tn::node::ptr node(new tn::node());

  try {
      node->init( data_dir.c_str(), node_port );

      for( uint32_t i = 0; i < init_connections.size(); ++i ) {
        try {
            wlog( "Attempting to connect to %s", init_connections[i].c_str() );
            fc::sha1 id = node->connect_to( fc::ip::endpoint::from_string( init_connections[i].c_str() ) );
            wlog( "Connected to %s", fc::string(id).c_str() );

            fc::sha1 target = node->get_id();;
            target.data()[19] += 1;
            slog( "Starting bootstrap process by searching for my node id (%s) + 1  (%s)  diff %s",
                  fc::string(node->get_id()).c_str(), fc::string(target).c_str(), fc::string(target^node->get_id()).c_str() );
            tn::kad_search::ptr ks( new tn::kad_search( node, target ) );
            ks->start();
            ks->wait();

            slog( "Results: \n" );
            //const std::map<fc::sha1,tn::host>&
            auto r = ks->current_results();
            auto itr  = r.begin(); 
            while( itr != r.end() ) {
              slog( "   distance: %s   node: %s  endpoint: %s", 
                       fc::string(itr->first).c_str(), fc::string(itr->second.id).c_str(), fc::string(itr->second.ep).c_str() );
              ++itr;
            }
        } catch ( ... ) {
            wlog( "Unable to connect to node %s, %s", 
                  init_connections[i].c_str(), fc::current_exception().diagnostic_information().c_str() );
        }
      }
      
      slog( "services started, ready for action" );
      fc::wait(quit);
  } catch ( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
  }
  wlog( "gracefully shutdown node." );
  node->close();
  wlog( "\n\nexiting services!\n\n" );
}

fc::future<void> start_services_complete;

fc::thread* service_thread = 0;
int main( int argc, char** argv ) {
  fc::thread::current().set_name("main");
  service_thread = &fc::thread::current();
  signal( SIGINT, handle_sigint );

  try {
    start_services_complete = fc::async( [=]() { start_services( argc, argv ); }, "start_services" );
    start_services_complete.wait();
  } catch ( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
  }

  return 0;
}


void handle_sigint( int si ) {
  slog( "%d", si );
  static int count = 0;
  if( !count ){ 
      quit();
      service_thread->poke();
//    QCoreApplication::exit(0);
  }
  else exit(si);
  ++count;
}
