#include <fc/thread.hpp>
#include <fc/exception.hpp>
#include <fc/log.hpp>
//#include <fc/signals.hpp>
#include <fc/stream.hpp>
#include <fc/bigint.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <tornet/node.hpp>
#include <tornet/kad.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/db/peer.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <iomanip>

#include <fc/program_options.hpp>
#include <boost/program_options.hpp>

#include <signal.h>

void handle_sigint( int si );

//boost::signal<void()> quit;
enum chunk_order{
  by_revenue,
  by_opportunity,
  by_distance
};


void cli( const tn::node::ptr& _node, const tn::chunk_service::ptr& _cs );
//#include <iostream>

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
  //vm.parse_command_line( argc, argv, desc );

  if( vm.count("help") ) {
      std::cout << desc << "\n";
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



void print_chunks( const tn::db::chunk::ptr& db, int start, int limit, chunk_order order ) {

    int cnt = db->count();
    if( order == by_distance ) {
    std::cerr<<std::setw(6)<<"Index"<<"  "<<std::setw(40)<< "ID" << "  " << std::setw(8) << "Size" << "  " 
            <<std::setw(8) << "Queries"  << "  " 
            <<std::setw(10)<< "Q/Year"   << "  "
            <<std::setw(5) << "DistR"    << "  " 
            <<std::setw(8) << "Price"    << "  " 
            <<"Rev/Year\n";
    std::cerr<<"------------------------------------------------------------------------------------\n";
        tn::db::chunk::meta m;
        fc::sha1 id;
        for( uint32_t i = 0; i <  cnt; ++i ) {
          db->fetch_index( i+1, id, m );
          std::cerr<< std::setw(6) << i                        << "  "
                   <<                fc::string(id).c_str()    << "  " 
                   << std::setw(8)  << m.size                  << "  " 
                   << std::setw(8)  << m.query_count           << "  " 
                   << std::setw(10) << m.annual_query_count()  << "  "
                   << std::setw(5)  << (int)m.distance_rank    << "  " 
                   << std::setw(8)  << m.price()               << "  "
                   << std::setw(10) << m.annual_revenue_rate() << "\n";
        }
    } else if( order == by_opportunity ) {
    std::cerr<<std::setw(6)<<"Index"<<"  "<<std::setw(40)<< "ID" << "  " << std::setw(8) << "Size" << "  " 
            <<std::setw(8) << "Queries"  << "  " 
            <<std::setw(10)<< "Q/Year"   << "  "
            <<std::setw(5) << "DistR"    << "  " 
            <<std::setw(8) << "Price"    << "  " 
            <<"Rev/Year*\n";
    std::cerr<<"------------------------------------------------------------------------------------\n";
      fc::vector<tn::db::chunk::meta_record> recs;
      db->fetch_best_opportunity( recs, 10 );
      for( uint32_t i = 0; i < recs.size(); ++i ) {
          std::cerr<< std::setw(6) << i                        << "  "
                   <<                fc::string(recs[i].key).c_str()                      << "  " 
                   << std::setw(8)  << recs[i].value.size                  << "  " 
                   << std::setw(8)  << recs[i].value.query_count           << "  " 
                   << std::setw(10) << recs[i].value.annual_query_count()  << "  "
                   << std::setw(5)  << (int)recs[i].value.distance_rank    << "  " 
                   << std::setw(8)  << recs[i].value.price()               << "  "
                   << std::setw(10) << recs[i].value.annual_revenue_rate() << "\n";
      }
    } else if( order == by_revenue ) {
      fc::vector<tn::db::chunk::meta_record> recs;
      db->fetch_worst_performance( recs, 10 );
      for( uint32_t i = 0; i < recs.size(); ++i ) {
          std::cerr<< std::setw(6) << i                        << "  "
                   <<                fc::string(recs[i].key).c_str()                      << "  " 
                   << std::setw(8)  << recs[i].value.size                  << "  " 
                   << std::setw(8)  << recs[i].value.query_count           << "  " 
                   << std::setw(10) << recs[i].value.annual_query_count()  << "  "
                   << std::setw(5)  << (int)recs[i].value.distance_rank    << "  " 
                   << std::setw(8)  << recs[i].value.price()               << "  "
                   << std::setw(10) << recs[i].value.annual_revenue_rate() << "\n";
      }
    }
}

void print_record_header() {
    std::cerr<<std::setw(21) <<"Host:Port"<<" "
             <<std::setw(40) <<"ID"<<" "
             <<std::setw(10) <<"Dist"<<" "
             <<std::setw(5)  <<"Rank"<<" "
             <<std::setw(5)  <<"PRank"<<" "
             //<<std::setw(8)  <<"% Avail"<<" "
             <<std::setw(8)  <<"priority"<<" "
             <<std::setw(8)  <<"RTT(us)"<<" "
             <<std::setw(10) <<"EstB(B/s)"<<" "
             <<std::setw(10) <<"SentC"<<" "
             <<std::setw(10) <<"RecvC"<<" "
             <<std::setw(3)  <<"FW"<<" "
             <<std::setw(30) <<"First Contact"<<" "
             <<std::setw(30) <<"Last Contact"<<" "
             <<std::setw(10) <<"nonce[0]"<<" "
             <<std::setw(10) <<"nonce[1]"<<" "
             <<std::endl;
   std::cerr<<"----------------------------------------------------------"
            <<"----------------------------------------------------------"
            <<"----------------------------------------------------------\n";
}
int calc_dist( const fc::sha1& d ) {
  fc::bigint bi(d.data(), sizeof(d));
  fc::sha1   mx; memset( mx.data(),0xff,sizeof(mx));
  fc::bigint max(mx.data(),sizeof(mx));

  return ((bi * fc::bigint( 1000 )) / max).to_int64();
}


boost::posix_time::ptime to_system_time( uint64_t utc_us ) {
//    typedef boost::chrono::microseconds duration_t;
//    typedef duration_t::rep rep_t;
//    rep_t d = boost::chrono::duration_cast<duration_t>(t.time_since_epoch()).count();
    static boost::posix_time::ptime epoch(boost::gregorian::date(1970, boost::gregorian::Jan, 1));
    return epoch + boost::posix_time::seconds(long(utc_us/1000000)) + boost::posix_time::microseconds(long(utc_us%1000000));
}
void print_record( const tn::db::peer::record& rec, const tn::node::ptr& nd ) {
  std::cerr<< std::setw(21) 
           << fc::string(rec.last_ep).c_str()
           << rec.id() << " "
           << std::setw(10)  << calc_dist(rec.id() ^ nd->get_id()) << " "
           << std::setw(5)   << int(rec.rank) << " "
           << std::setw(5)   << int(rec.published_rank) << " "
      //     << std::setw(8)   << double(rec.availability) / uint16_t(0xffff) <<" "
           << std::setw(8)   << rec.priority
           << std::setw(8)   << (rec.avg_rtt_us) <<" "
           << std::setw(10)  << rec.est_bandwidth << " "
           << std::setw(10)  << rec.sent_credit << " "
           << std::setw(10)  << rec.recv_credit << " "
           << std::setw(3)   << (rec.firewalled ? 'Y' : 'N') << " "
           << std::setw(30)  << boost::posix_time::to_simple_string(to_system_time( rec.first_contact )) <<" "
           << std::setw(30)  << boost::posix_time::to_simple_string(to_system_time( rec.last_contact ))  <<" "
           << std::setw(10)  << (rec.nonce[0]) << " " << std::setw(10) << (rec.nonce[1]) << " "
           << std::endl;
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
         _cs->import( fc::path(infile.c_str()), tid, check, fc::path() );
         std::cout<<"Created "<<infile<<".tn with TID: "<<fc::string(tid).c_str()<<" and CHECK: "<<fc::string(check).c_str()<<"\n";

       } else if( cmd == "export" ) {
         std::string infile;
         ss >> infile;
         elog( "not implemented" );
       } else if( cmd == "export_tid" ) {

         std::string tid,check;
         ss >> tid >> check;
         _cs->export_tornet( fc::sha1(tid.c_str()), fc::sha1(check.c_str()) );

       } else if( cmd == "show" ) {
         std::string what;
         std::string order;
         ss >> what;
         ss >> order;
         chunk_order o = by_distance;
         if( order == "by_revenue" ) o = by_revenue;
         else if( order == "by_opportunity" ) o = by_opportunity;
         else if( order == "by_distance" || order == "" ) o = by_distance;
         else { 
          std::cerr<<"Invalid order '"<<order<<"'\n  options are by_revenue, by_opportunity, by_distance\n";
         }

         if( what == "rank" ) {
          std::cerr<<"Local Rank: "<< _node->rank()<<std::endl;
         }

         if( what == "local" ) {
          print_chunks( _cs->get_local_db(), 0, 0xffffff, o );
         } else if( what == "cache") {
          print_chunks( _cs->get_cache_db(), 0, 0xffffff, o );
          /*
            int cnt = cs->get_cache_db()->count();
            tn::db::chunk::meta m;
            fc::sha1 id;
            std::cerr<<"Index "<< "ID" << "  " << "Size" << "  " << "Queried" <<  "  " << "Dist Rank" << "  Annual Rev\n";
            std::cerr<<"----------------------------------------------------------------------\n";
            for( uint32_t i = 0; i <  cnt; ++i ) {
              cs->get_cache_db()->fetch_index( i+1, id, m );
              std::cerr<<i<<"] " << id << "  " << m.size << "  " << m.query_count <<  "  " << (int)m.distance_rank << "  " << m.annual_revenue_rate() << "\n";
            }
          */
         } else if( what == "connections" ) {
            fc::vector<tn::db::peer::record> recs = _node->active_peers();
            print_record_header();
            for( uint32_t i =0; i < recs.size(); ++i ) {
              print_record( recs[i], _node );
            }
         } else if( what == "publish" ) {
         /*
            uint32_t cnt = _cs->get_publish_db()->count(); 
            tn::db::publish::record rec;
            fc::sha1                id;
            std::cerr<< std::setw(40) << "ID" << " "
                     << std::setw(10) << "AI" << " "
                     << std::setw(10) << "Next" << " "
                     << std::setw(10) << "Hosts" << " "
                     << std::setw(10) << "Desired Hosts" << "\n";
            std::cerr<<"-----------------------------------------------------------------------------\n";
            for( uint32_t i = 1; i <= cnt; ++i ) {
              _cs->get_publish_db()->fetch_index( i, id, rec );
              std::cerr << id << " " 
                        << std::setw(10) << rec.access_interval << " " 
                        << std::setw(10) << (rec.next_update == 0 ? std::string("now") : boost::posix_time::to_simple_string( to_system_time( rec.next_update ) )) << " "
                        << std::setw(5)  << rec.host_count << " " 
                        << std::setw(5)  << rec.desired_host_count << std::endl;
            }
        */ 
         } else if( what == "users" ) {
            print_record_header();
            tn::db::peer::ptr p = _node->get_peers();
            int cnt = p->count();
            tn::db::peer::record r;
            fc::sha1 rid;
            for( uint32_t i = 0; i < cnt; ++i ) {
              p->fetch_index( i+1, rid, r );
              print_record( r, _node );
            }
         } else if( what == "tornet" ) {
           std::string tornet_file;
           ss >> tornet_file;
         } else if( what == "tid" ) {
           std::string tid,check; 
           ss >> tid >> check;
         }

       } else {
         fc::cerr<<"\nCommands:\n";
         fc::cerr<<"  import      FILENAME               - loads FILENAME and creates chunks and dumps FILENAME.tornet\n";
         fc::cerr<<"  export      TORNET_FILE            - loads TORNET_FILE and saves FILENAME\n";
         fc::cerr<<"  find        CHUNK_ID               - looks for the chunk and returns query rate stats for the chunk\n";
         fc::cerr<<"  export_tid  TID CHECKSUM [OUT_FILE]- loads TORNET_FILE and saves FILENAME\n";
         fc::cerr<<"  publish TID CHECKSUM                - pushes chunks from TORNET_FILE out to the network, including the TORNETFILE itself\n";
         fc::cerr<<"  show local START LIMIT [by_distance|by_revenue|by_opportunity]\n";
         fc::cerr<<"  show cache START LIMIT |by_distance|by_revenue|by_opportunity]\n";
         fc::cerr<<"  show users START LIMIT |by_balance|by_rank|...]\n";
         fc::cerr<<"  show publish \n";
         fc::cerr<<"  rankeffort EFFORT                  - percent effort to apply towoard improving rank\n";
         fc::cerr<<"  help                               - prints this menu\n\n";
         fc::cerr.flush();
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
      //quit();
      service_thread->poke();
//    QCoreApplication::exit(0);
  }
  else exit(si);
  ++count;
}
