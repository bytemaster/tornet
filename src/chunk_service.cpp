#include <tornet/chunk_service.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/chunk_search.hpp>
#include <tornet/chunk_service_client.hpp>
#include <tornet/tornet_file.hpp>
#include <tornet/download_status.hpp>
#include <tornet/archive.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/buffer.hpp>
#include <fc/raw.hpp>
#include <fc/stream.hpp>
#include <fc/json.hpp>
#include <fc/hex.hpp>
#include <fc/thread.hpp>
#include <stdio.h>


#include <boost/filesystem.hpp>

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

#include "persist.hpp"
#include <Wt/Dbo/Transaction>

namespace tn {

  class chunk_service::impl {
    public:
      impl(chunk_service& cs, const tn::node::ptr& n)
      :_self(cs),_node(n) {
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
:my(*this,node) {
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


void chunk_service::enable_publishing( bool state ) {
  if( my->_publishing != state ) {
    my->_publishing = state;
    if( state ) {
        my->_pub_loop_complete = fc::async([this](){ my->publish_loop(); } );
    }
  }
}

bool chunk_service::is_publishing()const { return my->_publishing; }



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


namespace fs = boost::filesystem;
tornet_file import_file( chunk_service& self, const fs::path& infile ) {
  // the file we are creating
  tornet_file tf; 
  tf.name = infile.filename().c_str();
  tf.size = fs::file_size(infile);
  if( !tf.size ) return tf;

  slog( "Importing %s of %lld bytes", infile.string().c_str(), tf.size );

  // map the file to memory for quick access
  fc::file_mapping  in_mfile(infile.string().c_str(), fc::read_only );
  fc::mapped_region in_mregion(in_mfile,fc::read_only,0,tf.size);

  const char* rpos = (const char*)in_mregion.get_address();
  const char* rend = rpos + tf.size;

  tf.checksum = fc::sha1::hash( rpos, in_mregion.get_size() );

  tf.mime = "TODO"; // TODO: use libmagic to determine mime type


  // we will be encrypting the file using blowfish
  // the key is the sha1() of the original file.
  fc::blowfish bf;
  bf.start( (unsigned char*)tf.checksum.data(), sizeof(tf.checksum) );
  bf.reset_chain();

  // round the file size up to the nearest 8 byte boundry  
  uint64_t rfile_size = ((tf.size+7)/8)*8; // needs to be a power of 8 for FB

  // the chunk_size is constant 1MB
  const uint64_t max_chunk_size = 1024*1024;

  // this is the temporary buffer used for storing each chunk
  fc::vector<char> chunk( (fc::min)(max_chunk_size,rfile_size) );


  // small files are kept 'inline'
  if( tf.size < (1024 * 1000) ) {
    tf.inline_data.resize(tf.size);
    memcpy( tf.inline_data.data(), rpos, tf.size );
  } else { 
    // large files are broken into chunks
    while( rpos < rend ) {
       // this size of this chunk
      uint64_t csize = (fc::min)( uint64_t(rend-rpos), max_chunk_size );
      
      // we need to pad out small chunks to ensure randomness 
      uint64_t rcsize = csize;
      while( rcsize < 1024*4 ) rcsize += 1024*4; //rand()%(1024*16);

      // round the chunk up to nearest 8 byte bounds for BF encryption
      rcsize = ((rcsize+7)/8)*8;

      // reserve the space
      chunk.resize(rcsize);

      // reset the blowfish chain for each chunk, this will allow
      // us to decode large files in parallel and potentially eliminate
      // the need for 2x the storage space.
      bf.reset_chain();

      // encrypt the chunk, avoding a memcpy if possible
      if( rcsize == csize ) {
        // encrypt the chunk
        bf.encrypt( (const unsigned char*)rpos, (unsigned char*)chunk.data(), rcsize, fc::blowfish::CBC );
      } else {
        memcpy( chunk.data(), rpos, csize );
        memset( chunk.data() + csize, 0, (rcsize-csize) );
        bf.encrypt( (unsigned char*)chunk.data(), rcsize, fc::blowfish::CBC );
      }
      
      // ensure that the chunk has proper randomness to be accepted
      uint64_t seed = randomize(chunk,*((uint64_t*)chunk.data()));

      fc::sha1 chunk_id = fc::sha1::hash(chunk.data(), chunk.size() );
      tf.chunks.push_back( tornet_file::chunk_data( csize, seed, chunk_id ) );

      // calculate 64KB slices for partial requests
      for( uint64_t s = 0; s < rcsize; ) {
        uint64_t ss = (fc::min)( uint64_t(64*1024), uint64_t(rcsize-s) ); 
        tf.chunks.back().slices.push_back( fc::super_fast_hash( chunk.data()+s, ss ) );
        s += ss;
      }
      // store the chunk
      self.get_local_db()->store_chunk( chunk_id, fc::const_buffer( chunk.data(), chunk.size() ) );

      rpos += csize;
    }
  }
  tf.version     = 0;
  tf.compression = 0;
  return tf;
}

tn::link publish_tornet_file( tn::chunk_service& self, const tornet_file& tf, int rep ) {
  tn::link ln;
  
//  slog( "%s", fc::json::to_string( tf ).c_str() );
  auto chunk = fc::raw::pack( tf );

  if( chunk.size() > 1024*1024 ) {
    FC_THROW_MSG( "Tornet file size(%lld) is larger than 1MB", chunk.size() ); 
  }

  // round the size up
  auto old_size = chunk.size();
  uint64_t rcsize = old_size;
  while( rcsize < 1024*4 ) rcsize += 1024*4; //rand()%(1024*16);

  rcsize = ((rcsize+7)/8)*8;

  if( rcsize != old_size ) {
      chunk.resize( rcsize );
      memset( chunk.data() + old_size, 0, rcsize - old_size );
  }

  ln.seed = randomize(chunk,*((uint64_t*)tf.checksum.data()));
  ln.id   = fc::sha1::hash( chunk.data(), chunk.size() );

  // TODO: this could be performed without blocking on the DB thread
  self.get_local_db()->store_chunk( ln.id, fc::const_buffer( chunk.data(), chunk.size() ) );

  for( uint32_t i = 0; i < tf.chunks.size(); ++i ) {
    tn::db::publish::record rec;
    self.get_publish_db()->fetch( tf.chunks[i].id, rec );
    rec.desired_host_count = rep;
    rec.next_update         = 0;
    self.get_publish_db()->store( tf.chunks[i].id, rec );
  }

  tn::db::publish::record rec;
  self.get_publish_db()->fetch( ln.id, rec );
  rec.desired_host_count = rep;
  rec.next_update        = 0;
  self.get_publish_db()->store( ln.id, rec );
  
  return ln;
}



tn::link chunk_service::publish( const fc::path& file, uint32_t rep  ) {
  if( !fs::exists( file ) )
    FC_THROW_MSG( "File '%s' does not exist.", file.string() ); 
  
  if( fs::is_regular_file( file ) ) {
     tornet_file tf = import_file( *this, file );
     return publish_tornet_file( *this, tf, rep );
  } else if( fs::is_directory( file ) ) {
     fs::directory_iterator itr(file);
     fs::directory_iterator end;

     tn::archive adir;

     while( itr != end ) {
        if( fs::path(*itr).filename().string()[0] != '.' )
            adir.add( fs::path(*itr).filename(), publish( fs::path(*itr), rep ) );  
        ++itr;
     }
     tornet_file tf;
     
     tf.inline_data = fc::raw::pack( adir );
     tf.version     = 0;
     tf.compression = 0;
     tf.name        = file.filename().string().c_str();
     tf.size        = tf.inline_data.size();
     tf.mime        = "tn::archive";
     tf.checksum    = fc::sha1::hash( tf.inline_data.data(), tf.size );

     return publish_tornet_file( *this, tf, rep );
  }
  FC_THROW_MSG( "File '%s' is not regular or directory", file.string() ); 
  return tn::link(); // hide warnings
}


tornet_file chunk_service::fetch_tornet( const tn::link& ln ) {
  auto chunk = fetch_chunk( ln.id );
  derandomize( ln.seed, chunk );
  auto tf = fc::raw::unpack<tornet_file>(chunk);

//  slog( "%s", fc::json::to_string( tf ).c_str() );
  return tf;
}

fc::vector<char> chunk_service::download_chunk( const fc::sha1& chunk_id ) {
  try {
    return fetch_chunk( chunk_id );
  } catch ( ... ) {
    wlog( "Unable to fetch %s, searching for it..", fc::string(chunk_id).c_str() );
     tn::chunk_search::ptr csearch( new tn::chunk_search( get_node(), chunk_id, 5, 1, true ) );  
     csearch->start();
     csearch->wait();

     auto hn = csearch->hosting_nodes().begin();
     while( hn != csearch->hosting_nodes().end() ) {
        auto csc = get_node()->get_client<chunk_service_client>(hn->second);

        try {
            // give the node 5 minutes to send 1 MB
            fetch_response fr = csc->fetch( chunk_id, -1 ).wait( fc::microseconds( 1000*1000*300 ) );
            slog( "Response size %d", fr.data.size() );

            auto fhash = fc::sha1::hash( fr.data.data(), fr.data.size() );
            if( fhash == chunk_id ) {
               get_local_db()->store_chunk(chunk_id,fr.data);
               fc::vector<char> tmp = fc::move(fr.data);
               return tmp;
            } else {
                wlog( "Node failed to return expected chunk" );
                wlog( "Received %s of size %d  expected  %s",
                       fc::string( fhash ).c_str(), fr.data.size(),
                       fc::string( chunk_id ).c_str() );
            }
        } catch ( ... ) {
           wlog( "Exception thrown while attempting to fetch %s from %s", 
                    fc::string(chunk_id).c_str(), fc::string(hn->second).c_str() );
           wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
        }
       ++hn;
     }
  }
  FC_THROW_MSG( "Unable to download chunk %s", chunk_id );
  return fc::vector<char>(); // hide warnings
}

tornet_file chunk_service::download_tornet( const tn::link& ln ) {
  slog( "download %s", fc::string(ln.id).c_str() );
  auto chunk = download_chunk(ln.id);
  derandomize( ln.seed, chunk );
  slog( "unpacking chunk" );
  auto tf =  fc::raw::unpack<tornet_file>(chunk);
//  slog( "%s", fc::json::to_string( tf ).c_str() );
  return tf;
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
   //slog( "%s", fc::to_hex( data.data(), 128 ).c_str() );
   //slog( "%d", data.size() );
   float prob = pochisq( x2, 255 );
   //slog( "Prob %f", prob );

   // smaller chunks have a higher chance of 'low' entrempy
   return prob < .80 && prob > .20;
}

uint64_t randomize( fc::vector<char>& data, uint64_t seed ) {
  fc::vector<char> tmp(data.size());
  do {
      ++seed;

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



} // namespace tn

