#include <QApplication>
#include <boost/cmt/log/log.hpp>
#include <boost/program_options.hpp>
#include <tornet/db/chunk.hpp>
#include <boost/exception/all.hpp>
#include <tornet/db/peer.hpp>
#include <boost/cmt/asio.hpp>
#include <tornet/net/node.hpp>
#include <tornet/net/udt_channel.hpp>
#include <tornet/services/calc.hpp>
#include <tornet/services/chat.hpp>
#include <tornet/rpc/client.hpp>

#include <tornet/net/kad.hpp>

#include <boost/cmt/thread.hpp>
#include <boost/cmt/signals.hpp>
#include <tornet/rpc/service.hpp>
#include <tornet/services/accounting.hpp>

#include <ltl/rpc/reflect.hpp>

#include <tornet/services/chunk_service.hpp>

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

enum chunk_order{
  by_revenue,
  by_opportunity,
  by_distance
};

void print_peers( const tornet::db::peer::ptr& peers ) {

}

void print_chunks( const tornet::db::chunk::ptr& db, int start, int limit, chunk_order order ) {


}

void cli( const chunk_service::ptr& cs, const tornet::node::ptr& nd ) {
  try {
     std::string line;
     while( std::getline(std::cin, line) ) {
       std::stringstream ss(line);
     
       std::string cmd;
       ss >> cmd;
     
       if( cmd == "import" ) {
         std::string infile;
         ss >> infile;
         scrypt::sha1 tid, check;
         cs->import( infile, tid, check );
         std::cout<<"Created "<<infile<<".tornet with TID: "<<std::string(tid)<<" and CHECK: "<<std::string(check)<<std::endl;
       } else if( cmd == "export" ) {
         std::string infile;
         ss >> infile;

       } else if( cmd == "export_tid" ) {
         std::string tid,check;
         ss >> tid >> check;
         cs->export_tornet( scrypt::sha1(tid), scrypt::sha1(check) );
       } else if( cmd == "publish" ) {
         std::string infile;
         ss >> infile;
     
       } else if( cmd == "show" ) {
         std::string what;
         ss >> what;

         if( what == "local" ) {
            std::string start;
            std::string limit;
            std::string order;
            ss >> start >> limit >> order;

         } else if( what == "cache") {

         }
     
         if( what == "chunks" ) {
            int cnt = cs->get_local_db()->count();
            tornet::db::chunk::meta m;
            scrypt::sha1 id;
            std::cerr<<"Index "<< "ID" << "  " << "Size" << "  " << "Queried" <<  "  " << "Dist Rank" << "  Annual Rev\n";
            std::cerr<<"----------------------------------------------------------------------\n";
            for( uint32_t i = 0; i <  cnt; ++i ) {
              cs->get_local_db()->fetch_index( i+1, id, m );
              std::cerr<<i<<"] " << id << "  " << m.size << "  " << m.query_count <<  "  " << (int)m.distance_rank << "  " << m.annual_revenue_rate() << "\n";
            }
     
         } else if( what == "users" ) {
     
         } else if( what == "tornet" ) {
           std::string tornet_file;
           ss >> tornet_file;
         } else if( what == "tid" ) {
           std::string tid,check; 
           ss >> tid >> check;
         }
     
       } else if( cmd == "help" ) {
         std::cerr<<"\nCommands:\n";
         std::cerr<<"  import      FILENAME               - loads FILENAME and creates chunks and dumps FILENAME.tornet\n";
         std::cerr<<"  export      TORNET_FILE            - loads TORNET_FILE and saves FILENAME\n";
         std::cerr<<"  export_tid  TID CHECKSUM [OUT_FILE]- loads TORNET_FILE and saves FILENAME\n";
         std::cerr<<"  publish TORNET_FILE                - pushes chunks from TORNET_FILE out to the network, including the TORNETFILE itself\n";
         std::cerr<<"  show local START LIMIT [by_distance|by_revenue|by_opportunity]\n";
         std::cerr<<"  show cache START LIMIT |by_distance|by_revenue|by_opportunity]\n";
         std::cerr<<"  show users START LIMIT |by_balance|by_rank|...]\n";
         std::cerr<<"  help                               - prints this menu\n\n";
       }
     }
  } catch ( const boost::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e) );
  } catch ( const std::exception& e ) {
    elog( "%1%", boost::diagnostic_information(e) );
  }
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
      
      tornet::rpc::service::ptr        srv( new tornet::service::calc_service( node, "rpc", 101 ) );
      chunk_service::ptr               cs(  new chunk_service( data_dir+"/chunks", node, "chunks", 100 ) );
      tornet::service::accounting::ptr as(  new tornet::service::accounting( data_dir + "/accounting", node, "accounting", 69 ) );

      new boost::thread( boost::bind( cli, cs, node ) );

      for( uint32_t i = 0; i < init_connections.size(); ++i ) {
        try {
            wlog( "Attempting to connect to %1%", init_connections[i] );
            scrypt::sha1 id = node->connect_to( to_endpoint(init_connections[i]) );
            wlog( "Connected to %1%", id );

            slog( "Starting bootstrap process by searching for my node id + 1" );
            scrypt::sha1 sh;
            sh.hash[4] = 0x01000000;
            scrypt::sha1 target = sh ^ node->get_id();
            tornet::kad_search::ptr ks( new tornet::kad_search( node, target ) );
            ks->start();
            ks->wait();

            slog( "Results: \n" );
            const std::map<tornet::node::id_type,tornet::node::id_type>&  r = ks->current_results();
            std::map<tornet::node::id_type,tornet::node::id_type>::const_iterator itr  = r.begin(); 
            while( itr != r.end() ) {
              slog( "   node id: %2%   distance: %1%", 
                        itr->first, itr->second );
              ++itr;
            }
            
            
#if 0
            tornet::channel c = node->open_channel( id, 101 );
            tornet::rpc::connection::ptr  con( new tornet::rpc::connection(c));
            int r = con->call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con->call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con->call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );
            r = con->call<int,int>( 0, 5 ).wait(); slog( "r: %1%", r );

            tornet::rpc::client<tornet::service::calc_connection>   calc_client(con);
            r = calc_client->add( 9 );
            slog( "r: %1%", r );



            tornet::channel ac = node->open_channel( id, 69 );
            tornet::rpc::connection::ptr  acon( new tornet::rpc::connection(ac));
            tornet::rpc::client<ltl::rpc::session>   acnt(acon);
            slog( " create asset: %1%", acnt->create_asset( ltl::rpc::asset( "gold grams", ".999 pure" ) ).wait() );



            slog( "creating test data" );
            std::string test = "hello world.";
            for( uint32_t y = 0; y < 16;++y ) test += test;

            tornet::channel cc = node->open_channel( id, 100 );

           
            tornet::udt_channel uc(cc,1024);
            for( uint32_t x = 0; x < 1000*10; ++x ) {
              uc.write( boost::asio::buffer( test.c_str(),test.size() ) );
            }
#endif
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
