#include <tornet/chunk_service.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/chunk_search.hpp>
#include <tornet/chunk_service_client.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/download_status.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/buffer.hpp>
#include <fc/raw.hpp>
#include <fc/stream.hpp>
#include <fc/json.hpp>
#include <fc/hex.hpp>
#include <fc/thread.hpp>
#include <stdio.h>

#include <boost/random/mersenne_twister.hpp>
#include <tornet/node.hpp>

#include <fc/interprocess/file_mapping.hpp>
//#include <boost/interprocess/file_mapping.hpp>
//#include <boost/interprocess/mapped_region.hpp>


//#include "chunk_session.hpp"
//#include "reflect_chunk_session.hpp"
//#include <tn/error.hpp>
//#include "chunk_search.hpp"
#include <fc/blowfish.hpp>
#include <fc/super_fast_hash.hpp>

#include <tornet/service_ports.hpp>
#include "chunk_service_connection.hpp"

namespace tn {

  class chunk_service::impl {
    public:
      impl(chunk_service& cs)
      :_self(cs) {
        _publishing = false;
      }
      chunk_service&   _self;
      tn::node::ptr    _node;
      db::chunk::ptr   _cache_db;
      db::chunk::ptr   _local_db;
      db::publish::ptr _pub_db;
      bool             _publishing;
      fc::future<void> _pub_loop_complete;

      void on_new_connection( const channel& c );
      void publish_loop();

      fc::vector<chunk_service_connection::ptr> _cons;
  };

  void chunk_service::impl::on_new_connection( const tn::channel& c ) {
      chunk_service_connection::ptr con( new chunk_service_connection( udt_channel(c,1024), _self ) );
      _cons.push_back(con);
  }


chunk_service::chunk_service( const fc::path& dbdir, const tn::node::ptr& node )
:my(*this) {
    my->_node = node;
    fc::create_directories(dbdir/"cache_db");
    fc::create_directories(dbdir/"local_db");
    my->_cache_db.reset( new db::chunk( node->get_id(), dbdir/"cache_db" ) );
    my->_cache_db->init();
    my->_local_db.reset( new db::chunk( node->get_id(), dbdir/"local_db" ) );
    my->_local_db->init();
    my->_pub_db.reset( new db::publish( dbdir/"publish_db" ) );
    my->_pub_db->init();

    my->_node->start_service( chunk_service_udt_port, "chunkd", [=]( const channel& c ) { this->my->on_new_connection(c); }  );
}

void chunk_service::shutdown() {
    enable_publishing( false );
    my->_node->close_service( chunk_service_udt_port );
    my->_cache_db.reset();
    my->_local_db.reset();
    my->_pub_db.reset();
}


chunk_service::~chunk_service(){
//  slog( "%p", this );
} 

db::chunk::ptr&   chunk_service::get_cache_db() { return my->_cache_db; }
db::chunk::ptr&   chunk_service::get_local_db() { return my->_local_db; }
db::publish::ptr&   chunk_service::get_publish_db() { return my->_pub_db; }
node::ptr&          chunk_service::get_node() { return my->_node; }

//fc::any chunk_service::init_connection( const tn::rpc::connection::ptr& con ) {
  /*
    boost::shared_ptr<chunk_session> cc(new chunk_session(m_cache_db,con) );
    boost::reflect::any_ptr<chunk_session> acc(cc);

    uint16_t mid = 0;
    boost::reflect::visit(acc, service::visitor<chunk_session>(*con, acc, mid) );
  */
//    return acc; 
//}


/**
 *
 *
 */
void chunk_service::import( const fc::path& infile, 
                            fc::sha1& tn_id, fc::sha1& checksum, uint64_t& seed,
                            const fc::path& outfile ) 
{
  if( !fc::exists( infile ) )
    FC_THROW_MSG( "File '%s' does not exist.", infile.string() ); 
  if( fc::is_directory( infile ) )
    FC_THROW_MSG( "'%s' is a directory, expected a file.", infile.string() ); 
  if( !fc::is_regular( infile ) )
    FC_THROW_MSG( "'%s' is not a regular file.", infile.string() ); 

  uint64_t file_size  = fc::file_size(infile);
  slog( "Importing %s of %lld bytes", infile.string().c_str(), file_size );
  if( file_size == 0 )
    FC_THROW_MSG( "'%s' is an empty file.", infile.string() );

  {
     // using namespace boost::interprocess;
      fc::file_mapping  mfile(infile.string().c_str(), fc::read_only );
      fc::mapped_region mregion(mfile,fc::read_only,0,file_size);
      checksum = fc::sha1::hash( (char*)mregion.get_address(), mregion.get_size() );
  } 
  fc::blowfish bf;
  fc::string fhstr = checksum;
  bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );
  slog("bf key %s", (unsigned char*)fhstr.c_str() );

  bf.reset_chain();
  slog( "Checksum %s", fc::string(fhstr).c_str() );

  fc::ifstream in(infile.string().c_str(), fc::ifstream::in | fc::ifstream::binary );

  uint64_t rfile_size = ((file_size+7)/8)*8; // needs to be a power of 8 for FB
  uint64_t chunk_size = 1024*1024;
  fc::vector<char> chunk( (fc::min)(chunk_size,rfile_size) );

  tornet_file tf(infile.filename().string(),file_size);
  int64_t r = 0;

  while( r < int64_t(file_size) ) {
    int64_t c = (fc::min)( uint64_t(file_size-r), (uint64_t)chunk.size() );
    if( c < int64_t(chunk.size()) ) 
      memset( chunk.data() + c, 0, chunk.size()-c );
    in.read( chunk.data(), c );
    int64_t pc = c;

    // minimum chunk size is 32K + some random additional amount to
    // prevent end pieces from 'stick out' at 32K even.   32K is needed
    // to ensure proper entropy at the randomize step.
    if( c < 1024*32 ) c = 1024*32 + (rand() % (1024*16)) ;

    c = ((c+7)/8)*8;


    bf.encrypt((unsigned char*)chunk.data(), c, fc::blowfish::CBC );

    // ensure that the encrypted chunk is properly random
    uint64_t seed = randomize(chunk);

    fc::sha1 chunk_id = fc::sha1::hash(chunk.data(), c );
    tf.chunks.push_back( tornet_file::chunk_data( pc, seed, chunk_id ) );
    slog( "Chunk %d id %s", tf.chunks.size(), fc::string(chunk_id).c_str() );

    // 64KB slices to allow parallel requests
    int64_t s = 0;
    while( s < c ) {
     int64_t ss = (fc::min)( int64_t(64*1024), int64_t(c-s) ); 
     tf.chunks.back().slices.push_back( fc::super_fast_hash( chunk.data()+s, ss ) );
     s += ss;
    }
    // store the chunk in the database
    my->_local_db->store_chunk( chunk_id, fc::const_buffer( chunk.data(), c ) );

    r += c;
  }
  tf.checksum = checksum;

  chunk = fc::raw::pack( tf );
      fc::datastream<size_t> ps; 
      fc::raw::pack(ps,tf );


 // slog( "%s", fc::json::to_string( tf ).c_str() );
 // slog( "packsize %d", ps.tellp() );

  auto old_size = chunk.size();

  // You cannot get 'random' data if your chunk size is near the size of your keyspace (256)
  // so this places a minimum size on our chunks to something around 
  if( chunk.size() < 32*1024 ) chunk.resize(32*1024 + (rand() % (1024*16)) );

  chunk.resize( ((chunk.size()+7)/8)*8 );

  if( old_size != chunk.size() ) 
    memset( chunk.data() + old_size, 0, chunk.size() - old_size );

  bf.reset_chain();
 // slog( "pre-encrypt chunk hex %s size %d", fc::to_hex(chunk.data(), chunk.size() ).c_str(), chunk.size() );
  bf.encrypt( (unsigned char*)chunk.data(), chunk.size(), fc::blowfish::CBC );

  seed = randomize(chunk);
 
//  slog( "chunk hex %s size %d", fc::to_hex(chunk.data(), chunk.size() ).c_str(), chunk.size() );
  tn_id = fc::sha1::hash(  chunk.data(), chunk.size() );
 // slog( "store chunk %s", fc::string(tn_id).c_str() );
  my->_local_db->store_chunk( tn_id, fc::const_buffer( chunk.data(), chunk.size() ) );

  fc::string of;
  if( outfile == fc::path() )
    of = infile.string() + ".tn";
  else
    of = outfile.string();
  fc::ofstream out( of, fc::ofstream::binary | fc::ofstream::out );
 
 // TODO:::
  fc::raw::pack( out, tf );
}

void chunk_service::export_tornet( const fc::sha1& tn_id, const fc::sha1& checksum, uint64_t seed ) {
  tornet_file tf = fetch_tornet( tn_id, checksum, seed );

  fc::blowfish bf;
  fc::string fhstr = checksum;
  bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );

  // create a file of tf.size rounded up to the nearest 8 bytes (for blowfish)

  //int64_t rsize = ((tf.size+7)/8)*8;
  int64_t rsize = tf.size;
  FILE* f = fopen(tf.name.c_str(), "w+b");
  fseeko( f, rsize-1, SEEK_SET );
  char l=0;
  fwrite( &l, 1, 1, f );
  fflush(f);
  fclose(f);

  fc::file_mapping  mfile(tf.name.c_str(), fc::read_write );
  fc::mapped_region mregion(mfile,fc::read_write );

  bf.reset_chain();
  char* start = (char*)mregion.get_address();
  char* pos = start;
  char* end = pos + mregion.get_size();
  for( uint32_t i = 0; i < tf.chunks.size(); ++i ) {
    slog( "writing chunk %d %s at pos %d size: %d,   %d remaining", 
    i, fc::string(tf.chunks[i].id).c_str() , uint64_t(pos-start), tf.chunks[i].size, uint64_t(end-pos)); 
    if( (pos + tf.chunks[i].size) > end ) {
      FC_THROW_MSG( "Attempt to write beyond end of file!" );
    }
    uint32_t adj_size = ((tf.chunks[i].size+7)/8)*8;
    if( adj_size != tf.chunks[i].size ) {
      fc::vector<char> tmp(adj_size);
      if( !my->_local_db->fetch_chunk( tf.chunks[i].id, fc::mutable_buffer(tmp.data(),tmp.size()) ) ) {
        FC_THROW_MSG( "Error fetching chunk %s", tf.chunks[i].id );
      }

      derandomize( tf.chunks[i].seed, tmp );
      bf.decrypt( (unsigned char*)&tmp.front(), adj_size, fc::blowfish::CBC );
      memcpy( pos, &tmp.front(), tf.chunks[i].size );
    } else {
      if( !my->_local_db->fetch_chunk( tf.chunks[i].id, fc::mutable_buffer(pos,adj_size) ) ) {
        FC_THROW_MSG( "Error fetching chunk %s", tf.chunks[i].id );
      }
      derandomize( tf.chunks[i].seed, fc::mutable_buffer(pos,adj_size) );

      //assert( rcheck == tf.chunks[i].id );
      bf.decrypt( (unsigned char*)pos, adj_size, fc::blowfish::CBC );
    }
    //fc::sha1 rcheck; fc::sha1_hash( rcheck, pos, adj_size );
    //slog( "decrypt sha1 %1%", rcheck );

    pos += tf.chunks[i].size;
  }

  //bf.decrypt( (unsigned char*)mregion.get_address(), rsize, fc::blowfish::CBC );
  fc::sha1 fcheck = fc::sha1::hash( (char*)mregion.get_address(), tf.size );
  if( fcheck != checksum ) {
    FC_THROW_MSG( "File checksum mismatch, got %s expected %s", fcheck, checksum );
  }
}

fc::vector<char> chunk_service::fetch_chunk( const fc::sha1& chunk_id ) {
  fc::vector<char> d;
  tn::db::chunk::meta met;
  try {
  if( my->_local_db->fetch_meta( chunk_id, met, false ) ) {
      d.resize(met.size);
      if( my->_local_db->fetch_chunk( chunk_id, fc::mutable_buffer(d.data(),met.size) ) )
        return d;
  }
  } catch(...) {}
  try {
  if( my->_cache_db->fetch_meta( chunk_id, met, false ) ) {
      d.resize(met.size);
      if( my->_cache_db->fetch_chunk( chunk_id, fc::mutable_buffer(d.data(),met.size) ) )
        return d;
  }
  } catch(...) {}
  FC_THROW_MSG( "Unknown chunk %s", chunk_id );
  return fc::vector<char>();
}

/**
 *  Fetch and decode the tn file description, but not the chunks
 */
tornet_file chunk_service::fetch_tornet( const fc::sha1& tn_id, const fc::sha1& checksum, uint64_t seed ) {
  tornet_file tnf;
  tn::db::chunk::meta met;
  if( my->_local_db->fetch_meta( tn_id, met, false ) ) {
      fc::vector<char> tnet(met.size);
      if( my->_local_db->fetch_chunk( tn_id, fc::mutable_buffer(tnet.data(),tnet.size()) ) ) {
          derandomize( seed, tnet );
          fc::blowfish bf;
          fc::string fhstr = checksum;
          slog("bf key %s", (unsigned char*)fhstr.c_str() );
  slog( "pre decrypt chunk hex %s size %d", fc::to_hex(tnet.data(), tnet.size() ).c_str(), tnet.size() );

          bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );
          bf.decrypt( (unsigned char*)tnet.data(), tnet.size(), fc::blowfish::CBC );

  slog( "post decrypt chunk hex %s size %d", fc::to_hex(tnet.data(), tnet.size() ).c_str(), tnet.size() );
          fc::sha1 check = fc::raw::unpack<fc::sha1>(tnet );
          if( check != checksum ) {
            FC_THROW_MSG( "Checksum mismatch, got %s expected %s", fc::string(check).c_str(), fc::string(checksum).c_str() );
          }

          tnf = fc::raw::unpack<tornet_file>(tnet);
          if( tnf.checksum != checksum ) {
            FC_THROW_MSG("Checksum mismatch, got %s tn file said %s", checksum,tnf.checksum );
          } else {
            slog( "Decoded checksum %s", fc::string(tnf.checksum).c_str() );
          }
          slog( "File name: %s  size %d", tnf.name.c_str(), tnf.size );
          return tnf;
      } else { 
        FC_THROW_MSG( "Unknown to find data for chunk %s", tn_id );
      }
  } else {
    FC_THROW_MSG( "Unknown chunk %s", tn_id );
  }
  return tnf;
}

/**
 *  This method should fetch the tn file from local storage and then decode the chunks.
 *
 *  To publish a chunk search for the N nearest nodes and track the availability of that
 *  chunk.  If less than the desired number of hosts are found, then pick the host closest
 *  to the chunk and upload a copy.  If the desired number of hosts are found simply note
 *  the popularity of that chunk and schedule a time to check again.
 *
 */
void chunk_service::publish_tornet( const fc::sha1& tid, const fc::sha1& cs, uint64_t seed, uint32_t rep ) {
  tornet_file tf = fetch_tornet( tid, cs, seed );
  slog( "publish %s", fc::json::to_string( tf ).c_str() );
  for( uint32_t i = 0; i < tf.chunks.size(); ++i ) {
    //if( !my->_local_db->exists(tf.chunks[i]) && !m_cache_db->exists(tf.chunks[i] ) ) {
    //  FC_THROW_MSG( "Unable to publish tn file because not all chunks are known to this node." );
    //  // TODO: Should we publish the parts we know?  Should we attempt to fetch the parts we don't?
   // }
    tn::db::publish::record rec;
    my->_pub_db->fetch( tf.chunks[i].id, rec );
    rec.desired_host_count = rep;
    rec.next_update        = 0;
    //slog("%s", fc::string(tf.chunks[i].id).c_str() );

    my->_pub_db->store( tf.chunks[i].id, rec );
  }
  tn::db::publish::record rec;
  my->_pub_db->fetch( tid, rec );
  rec.desired_host_count = rep;
  rec.next_update        = 0;
  my->_pub_db->store( tid, rec );
}

void chunk_service::enable_publishing( bool state ) {
  if( my->_publishing != state ) {
    my->_publishing = state;
    if( state ) {
        my->_pub_loop_complete = fc::async([this](){ my->publish_loop(); } );
    }
  }
#if 0
  if( &boost::cmt::thread::current() != get_thread() ) {
    get_thread()->sync( boost::bind( &chunk_service::enable_publishing, this, state ) );
    return;
  }
  if( state != my->_publishing ) {
      my->m_publishing = state;
      slog( "state %1%", state );
      if( state ) { 
        wlog( "async!" );
        get_thread()->async( boost::bind( &chunk_service::publish_loop, this ) );
      } else {
      }
  }
  #endif
}

bool chunk_service::publishing_enabled()const { 
  return my->_publishing;
}


fc::shared_ptr<download_status> chunk_service::download_tornet( const fc::sha1& tornet_id, const fc::sha1& checksum, uint64_t seed, fc::ostream& out ) {
  download_status::ptr down( new download_status( chunk_service::ptr(this,true), tornet_id, checksum, seed, out ) );
  down->start();
  return down;
}


void chunk_service::impl::publish_loop() {
  while( _publishing ) {
    fc::sha1 cid; 
    tn::db::publish::record next_pub;
    if ( _pub_db->fetch_next( cid, next_pub ) ) {

       auto time_till_update = next_pub.next_update - fc::time_point::now().time_since_epoch().count();

       if( time_till_update > 0 ) {
          slog( "waiting %lld us for next publish update.", time_till_update );
          fc::usleep( fc::microseconds(time_till_update) );
       }

       // TODO: increase parallism of search?? This should be a background task 
       // so latency is not an issue... 

       slog( "Searching for chunk %s", fc::string(cid).c_str() );
       tn::chunk_search::ptr csearch(new tn::chunk_search(_node,cid, 10, 1, true )); 
       csearch->start();
       csearch->wait();

      
       typedef std::map<fc::sha1,tn::host> chunk_map;
       typedef std::map<fc::sha1,fc::sha1> host_map;
       const host_map&  hn = csearch->hosting_nodes();

       next_pub.host_count = hn.size();
       // if the chunk was not found on the desired number of nodes, we need to find a node
       // to push it to.
       if( hn.size() < next_pub.desired_host_count ) {
            wlog( "Published chunk %s found on at least %d hosts, desired replication is %d",
                 to_string(cid).c_str(), hn.size(), next_pub.desired_host_count );

            slog( "Hosting nodes: " );
            auto itr = hn.begin();
            while( itr != hn.end() ) {
              slog( "    node-dist: %s  node id: %s", 
                          fc::string(itr->first).c_str(), fc::string(itr->second).c_str() );
              ++itr;
            }
            slog( "Near nodes: " );

            fc::optional<fc::sha1> store_on_node;

            const chunk_map  nn = csearch->current_results();
            for( auto nitr = nn.begin(); nitr != nn.end(); ++nitr ) {
              slog( "    node-dist: %s  node id: %s  %s", fc::string(nitr->first).c_str(), fc::string(nitr->second.id).c_str(), fc::string(nitr->second.ep).c_str() );
              if( hn.find( nitr->first ) == hn.end() && !store_on_node ) {
                if( !store_on_node ) store_on_node = nitr->second.id; 
              }
            }

            if( !store_on_node ) { wlog( "No new hosts available to store chunk" ); }
            else {
                slog( "Storing chunk %s on node %s", fc::string(cid).c_str(), fc::string(*store_on_node).c_str() );
                auto csc   = _node->get_client<tn::chunk_service_client>(*store_on_node);
                store_response r = csc->store( _self.fetch_chunk(cid) ).wait();
                if( r.result == 0 ) next_pub.host_count++;
                slog( "Response: %d", int(r.result));
            }
            next_pub.next_update = (fc::time_point::now() + fc::microseconds(30*1000*1000)).time_since_epoch().count();
            _pub_db->store( cid, next_pub );
       } else {
            wlog( "Published chunk %s found on at least %d hosts, desired replication is %d",
                 to_string(cid).c_str(), hn.size(), next_pub.desired_host_count );

         // TODO: calculate next update interval for this chunk based upon its current popularity
         //       and host count and how far away the closest host was found.
         next_pub.next_update = 
          (fc::time_point::now() + fc::microseconds(30*1000*1000)).time_since_epoch().count();
         _pub_db->store( cid, next_pub );
      }
    
    } else {
      // TODO: do something smarter, like exit publish loop until there is something to publish
      fc::usleep( fc::microseconds(1000 * 1000)  );
      wlog( "nothing to publish..." );
    }


  }
}



/**
 *  Calculate how frequently each byte occurs in data.
 *  If every possible byte occurs with the same frequency then the
 *  data is perfectly random. 
 *
 *  The expected occurence of each possible byte is data.size / 256
 *
 *  If one byte occurs more often than another you get a small error
 *  (1*1) / expected.  If it occurs much more the error grows by
 *  the square.
 */
bool is_random( const fc::vector<char>& data ) {
   fc::vector<uint16_t> buckets(256);
   memset( buckets.data(), 0, buckets.size() * sizeof(uint16_t) );
   for( auto itr = data.begin(); itr != data.end(); ++itr )
     buckets[(uint8_t)*itr]++;
   
   double expected = data.size() / 256;
   
   double x2 = 0;
   for( auto itr = buckets.begin(); itr != buckets.end(); ++itr ) {
       double de = *itr - expected;
       x2 +=  (de*de) / expected;
   } 
   slog( "%s", fc::to_hex( data.data(), 128 ).c_str() );
   slog( "%d", data.size() );
   float prob = pochisq( x2, 255 );
   slog( "Prob %f", prob );

   // smaller chunks have a higher chance of 'low' entrempy
   return prob < .80 && prob > .20;
}

uint64_t randomize( fc::vector<char>& data ) {
  uint64_t seed;
  fc::vector<char> tmp(data.size());
  do {
      seed = rand();

      boost::random::mt19937 gen(seed);
      uint32_t* src = (uint32_t*)data.data();
      uint32_t* end = src +(data.size()/sizeof(uint32_t)); 
      uint32_t* dst = (uint32_t*)tmp.data();
      gen.generate( dst, dst + (data.size()/sizeof(uint32_t)) );
      while( src != end ) {
        *dst = *src ^ *dst;
        ++dst; 
        ++src;
      }
  } while (!is_random(tmp) );
  fc::swap(data,tmp);
  return seed;
}

void derandomize( uint64_t seed, const fc::mutable_buffer& b ) {
  boost::random::mt19937 gen(seed);
  fc::vector<char> tmp(b.size);
  uint32_t* src = (uint32_t*)b.data;
  uint32_t* end = src +(b.size/sizeof(uint32_t)); 
  uint32_t* dst = (uint32_t*)tmp.data();
  gen.generate( dst, dst + (tmp.size()/sizeof(uint32_t)) );
  while( src != end ) {
    *src = *src ^ *dst;
    ++dst; 
    ++src;
  }
}
void derandomize( uint64_t seed, fc::vector<char>& data ) {
  boost::random::mt19937 gen(seed);
  fc::vector<char> tmp(data.size());
  uint32_t* src = (uint32_t*)data.data();
  uint32_t* end = src +(data.size()/sizeof(uint32_t)); 
  uint32_t* dst = (uint32_t*)tmp.data();
  gen.generate( dst, dst + (data.size()/sizeof(uint32_t)) );
  while( src != end ) {
    *dst = *src ^ *dst;
    ++dst; 
    ++src;
  }
  fc::swap(data,tmp);
}


/**
 *  
 *
 */
#if 0
void chunk_service::publish_loop() {
  slog( "publish loop" );
  while( m_publishing ) {
    // find the next chunk that needs to be published
    fc::sha1    chunk_id;
    tn::db::publish::record next_pub;
    if ( m_pub_db->fetch_next( chunk_id, next_pub ) ) {
       int64_t time_till_update = next_pub.next_update - 
                        boost::chrono::duration_cast<boost::chrono::microseconds>
                        (boost::chrono::system_clock::now().time_since_epoch()).count();
       if( time_till_update > 0 ) {
        slog( "waiting %1% us for next publish update.", -time_till_update );
        boost::cmt::usleep( time_till_update );
       }

       // Attempt a KAD lookup for the chunk, find up to 2x desired hosts, using parallelism of 1
       // TODO: increase parallism of search?? This should be a background task so latency is not an issue... 
       tn::chunk_search::ptr csearch(  boost::make_shared<tn::chunk_search>(get_node(), chunk_id, next_pub.desired_host_count*2, 1, true ) );  
       csearch->start();
       csearch->wait();

      typedef std::map<node::id_type,node::id_type> chunk_map;
      const chunk_map&  hn = csearch->hosting_nodes();

      // If the number of nodes hosting the chunk < next_pub.desired_host_count
      if( hn.size() < next_pub.desired_host_count ) {
        wlog( "Published chunk %1% found on at least %2% hosts, desired replication is %3%",
             chunk_id, hn.size(), next_pub.desired_host_count );

        slog( "Hosting nodes: " );
        chunk_map::const_iterator itr = hn.begin();
        while( itr != hn.end() ) {
          slog( "    node-dist: %1%  node id: %2%", itr->first, itr->second );
          ++itr;
        }
        slog( "Near nodes: " );
        const chunk_map&  nn = csearch->current_results();
        itr = nn.begin();
        while( itr != nn.end() ) {
          slog( "    node-dist: %1%  node id: %2%", itr->first, itr->second );
          ++itr;
        }
        itr = nn.begin();

       //   Find the closest node not hosting the chunk and upload the chunk
       while( itr != nn.end() && itr->second == get_node()->get_id() ) {
        ++itr;
       }
       if( itr == nn.end() ) {
         elog( "No hosts to publish to!" );
       } else {
         tn::rpc::client<chunk_session>&  chunk_client = 
          *tn::rpc::client<chunk_session>::get_udt_connection( get_node(), itr->second );
        
         std::vector<char> chunk_data;
         fetch_chunk( chunk_id, chunk_data );
         slog( "Uploading chunk... size %1% bytes", chunk_data.size() );
         store_response r = chunk_client->store( chunk_data ).wait();
         slog( "Response: %1%  balance: %2%", int(r.result), r.balance );
       }

       //   Wait until the upload has completed 
       //     update the next_pub host count and update time
       //     continue the publishing loop.
       //
       //   TODO: enable publishing N chunks in parallel 
      } else {
        slog( "Published chunk %1% found on at least %2% hosts, desired replication is %3%",
             chunk_id, hn.size(), next_pub.desired_host_count );

      }

      // TODO: calculate next update interval for this chunk based upon its current popularity
      //       and host count and how far away the closest host was found.
      uint64_t next_update = 60*1000*1000;
      next_pub.next_update = 
        chrono::duration_cast<chrono::microseconds>( chrono::system_clock::now().time_since_epoch()).count() + next_update;

      m_pub_db->store( chunk_id, next_pub );
    } else {
      // TODO: do something smarter, like exit publish loop until there is something to publish
      boost::cmt::usleep( 1000 * 1000  );
      wlog( "nothing to publish..." );
    }
//    elog( "wait to send next" );
//    boost::cmt::usleep(1000*1000);
//    elog( "wait to send next" );
  }
}
  #endif

#if 0
void publish_loop() {
  db::publish::ptr pdb;

  while( publishing ) {
    fc::sha1 cid, nid;
    db::publish::record rec;
    pdb->fetch_oldest( cid, nid, rec );

    // get results

    // find 
    tn::chunk_search::ptr cs( new tn::chunk_search( node, cid ) );
    cs->start();
    cs->wait();
  
    const std::map<tn::node::id_type,tn::node::id_type>&  r = ks->current_results();
    std::map<tn::node::id_type,tn::node::id_type>::const_iterator itr  = r.begin(); 
    while( itr != r.end() ) {
      // total weighted access interval
      // pdb->store( cid, nid, now, normalize(access_interval,node distance) );
      ++itr;
    }
    pdb->store( cid, publish::record( now, avg_access_rate, r.size() ) );

    if( r.size() < rec.desired_host_count ) {
      cs->best_node_to_publish_to()->publish( chunk );
    }
  }
}
#endif

} // namespace tn

