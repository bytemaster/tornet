#include <fc/thread.hpp>
#include <fc/exception.hpp>
#include <fc/log.hpp>
#include <fc/signals.hpp>
#include <fc/stream.hpp>

#include <tornet/node.hpp>
#include <tornet/kad.hpp>
#include <tornet/chunk_service.hpp>


#include <boost/program_options.hpp>

#include <signal.h>

void handle_sigint( int si );

boost::signal<void()> quit;

void cli( const tn::node::ptr& _node, const tn::chunk_service::ptr& _cs );

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

      tn::chunk_service::ptr cs( new tn::chunk_service(fc::path(data_dir.c_str())/"chunks", node, "chunks", 100) );

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

      //auto cli_done = fc::async([=](){cli(node,cs);},"cli");
      cli(node,cs);
      //fc::wait(quit);
  } catch ( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
  }
  wlog( "gracefully shutdown node." );
  node->close();
  wlog( "\n\nexiting services!\n\n" );
}



void cli( const tn::node::ptr& _node, const tn::chunk_service::ptr& _cs ) {
     fc::thread* t = new fc::thread("cin");
     std::string line;
     while( t->async( [&](){ return (bool)std::getline(std::cin, line); } ).wait() ) {
      try {
       std::stringstream ss(line);
     
       std::string cmd;
       ss >> cmd;
       if( cmd == "quit" )  {
        return;
       } if( cmd == "import" )  {

         std::string infile;
         ss >> infile;
         fc::sha1 tid, check;
         _cs->import( fc::path(infile.c_str()), tid, check );
         std::cout<<"Created "<<infile<<".tornet with TID: "<<fc::string(tid).c_str()<<" and CHECK: "<<fc::string(check).c_str()<<std::endl;

       } else if( cmd == "export_tid" ) {

         std::string tid,check;
         ss >> tid >> check;
         _cs->export_tornet( fc::sha1(tid.c_str()), fc::sha1(check.c_str()) );

       } else {
         std::cerr<<"\nCommands:\n";
         std::cerr<<"  import      FILENAME               - loads FILENAME and creates chunks and dumps FILENAME.tornet\n";
         std::cerr<<"  export      TORNET_FILE            - loads TORNET_FILE and saves FILENAME\n";
         std::cerr<<"  find        CHUNK_ID               - looks for the chunk and returns query rate stats for the chunk\n";
         std::cerr<<"  export_tid  TID CHECKSUM [OUT_FILE]- loads TORNET_FILE and saves FILENAME\n";
         std::cerr<<"  publish TID CHECKSUM                - pushes chunks from TORNET_FILE out to the network, including the TORNETFILE itself\n";
         std::cerr<<"  show local START LIMIT [by_distance|by_revenue|by_opportunity]\n";
         std::cerr<<"  show cache START LIMIT |by_distance|by_revenue|by_opportunity]\n";
         std::cerr<<"  show users START LIMIT |by_balance|by_rank|...]\n";
         std::cerr<<"  show publish \n";
         std::cerr<<"  rankeffort EFFORT                  - percent effort to apply towoard improving rank\n";
         std::cerr<<"  help                               - prints this menu\n\n";
       }
     } catch ( ... ) {
        wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
     }
    } 
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
