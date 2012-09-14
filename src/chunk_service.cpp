#include <tornet/chunk_service.hpp>
#include <tornet/db/chunk.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/tornet_file.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/buffer.hpp>


#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <fstream>


//#include "chunk_session.hpp"
//#include "reflect_chunk_session.hpp"
//#include <tn/error.hpp>
//#include "chunk_search.hpp"
#include <fc/blowfish.hpp>
#include <fc/super_fast_hash.hpp>

namespace tn {

  class chunk_service::impl {
    public:
      impl() {
        _publishing = false;
      }
      db::chunk::ptr   _cache_db;
      db::chunk::ptr   _local_db;
      db::publish::ptr _pub_db;
      bool             _publishing;



    private:

  };


chunk_service::chunk_service( const fc::path& dbdir, const tn::node::ptr& node, const fc::string& name, uint16_t port )
{
  slog( "%p", this );
    fc::create_directories(dbdir/"cache_db");
    fc::create_directories(dbdir/"local_db");
    my->_cache_db.reset( new db::chunk( node->get_id(), dbdir/"cache_db" ) );
    my->_cache_db->init();
    my->_local_db.reset( new db::chunk( node->get_id(), dbdir/"local_db" ) );
    my->_local_db->init();
    my->_pub_db.reset( new db::publish( dbdir/"publish_db" ) );
    my->_pub_db->init();
}
chunk_service::~chunk_service(){
  slog( "%p", this );
} 


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
                            fc::sha1& tn_id, fc::sha1& checksum,
                            const fc::path& outfile ) 
{
  if( !fc::exists( infile ) )
    FC_THROW_MSG( "File '%s' does not exist.", %infile.string().c_str() ); 
  if( fc::is_directory( infile ) )
    FC_THROW_MSG( "'%s' is a directory, expected a file.", %infile.string().c_str() ); 
  if( !fc::is_regular( infile ) )
    FC_THROW_MSG( "'%s' is not a regular file.", %infile.string().c_str() ); 

  uint64_t file_size  = fc::file_size(infile);
  slog( "Importing %s of %lld bytes", infile.string().c_str(), file_size );
  if( file_size == 0 )
    FC_THROW_MSG( "'%s' is an empty file.", %infile.string().c_str() );

  {
      using namespace boost::interprocess;
      file_mapping  mfile(infile.string().c_str(), boost::interprocess::read_only );
      mapped_region mregion(mfile,boost::interprocess::read_only,0,file_size);
      checksum = fc::sha1::hash( (char*)mregion.get_address(), mregion.get_size() );
  } 
  fc::blowfish bf;
  fc::string fhstr = checksum;
  bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );
  bf.reset_chain();
  slog( "Checksum %s", fc::string(fhstr).c_str() );

  std::ifstream in(infile.string().c_str(), std::ifstream::in | std::ifstream::binary );

  uint64_t rfile_size = ((file_size+7)/8)*8; // needs to be a power of 8 for FB
  uint64_t chunk_size = 1024*1024;
  std::vector<char> chunk( (std::min)(chunk_size,rfile_size) );

  tornet_file tf(infile.filename().string(),file_size);
  int64_t r = 0;

  while( r < int64_t(file_size) ) {
    int64_t c = (std::min)( uint64_t(file_size-r), (uint64_t)chunk.size() );
    if( c < int64_t(chunk.size()) ) 
      memset( &chunk.front() + c, 0, chunk.size()-c );
    in.read( &chunk.front(), c );
    int64_t pc = c;
    c = ((c+7)/8)*8;

    bf.encrypt((unsigned char*)&chunk.front(), c, fc::blowfish::CBC );

    fc::sha1 chunk_id = fc::sha1::hash(chunk.data(), c );
    tf.chunks.push_back( tornet_file::chunk_data( pc, chunk_id ) );
    slog( "Chunk %1% id %2%", tf.chunks.size(), fc::string(chunk_id).c_str() );

    // 64KB slices to allow parallel requests
    int64_t s = 0;
    while( s < c ) {
     int64_t ss = (std::min)( int64_t(64*1024), int64_t(c-s) ); 
     tf.chunks.back().slices.push_back( fc::super_fast_hash( chunk.data()+s, ss ) );
     s += ss;
    }
    // store the chunk in the database
    my->_local_db->store_chunk( chunk_id, fc::const_buffer( chunk.data(), c ) );

    r += c;
  }
  tf.checksum = checksum;

// TODO::: 
  tn::rpc::raw::pack_vec( chunk, tf );


  chunk.resize( ((chunk.size()+7)/8)*8 );
  bf.reset_chain();
  bf.encrypt( (unsigned char*)&chunk.front(), chunk.size(), fc::blowfish::CBC );

  tn_id = fc::sha1::hash(  chunk.data(), chunk.size() );
  my->_local_db->store_chunk( tn_id, fc::const_buffer( chunk.data(), chunk.size() ) );

  fc::string of;
  if( outfile == fc::path() )
    of = infile.string() + ".tn";
  else
    of = outfile.string();
  std::ofstream out( of.c_str(), std::ostream::binary | std::ostream::out );
 
 // TODO:::
  tn::rpc::raw::pack( out, tf );
}

void chunk_service::export_tornet( const fc::sha1& tn_id, const fc::sha1& checksum ) {
#if 0
  tornet_file tf;
  fetch_tornet( tn_id, checksum, tf );

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

  using namespace boost::interprocess;
  file_mapping  mfile(tf.name.c_str(), read_write );
  mapped_region mregion(mfile,read_write );

  bf.reset_chain();
  char* start = (char*)mregion.get_address();
  char* pos = start;
  char* end = pos + mregion.get_size();
  for( uint32_t i = 0; i < tf.chunks.size(); ++i ) {
    slog( "writing chunk %1% %5% at pos %2% size: %3%,   %4% remaining", i, uint64_t(pos-start), tf.chunks[i].size, uint64_t(end-pos), tf.chunks[i].id );
    if( (pos + tf.chunks[i].size) > end ) {
      TORNET_THROW( "Attempt to write beyond end of file!" );
    }
    int adj_size = ((tf.chunks[i].size+7)/8)*8;
    if( adj_size != tf.chunks[i].size ) {
      std::vector<char> tmp(adj_size);
      if( !m_local_db->fetch_chunk( tf.chunks[i].id, boost::asio::buffer(tmp) ) ) {
        TORNET_THROW( "Error fetching chunk %1%", %tf.chunks[i].id );
      }
      bf.decrypt( (unsigned char*)&tmp.front(), adj_size, fc::blowfish::CBC );
      memcpy( pos, &tmp.front(), tf.chunks[i].size );
    } else {
      if( !m_local_db->fetch_chunk( tf.chunks[i].id, boost::asio::buffer(pos,adj_size) ) ) {
        TORNET_THROW( "Error fetching chunk %1%", %tf.chunks[i].id );
      }
      //assert( rcheck == tf.chunks[i].id );
      bf.decrypt( (unsigned char*)pos, adj_size, fc::blowfish::CBC );
    }
    //fc::sha1 rcheck; fc::sha1_hash( rcheck, pos, adj_size );
    //slog( "decrypt sha1 %1%", rcheck );

    pos += tf.chunks[i].size;
  }

  //bf.decrypt( (unsigned char*)mregion.get_address(), rsize, fc::blowfish::CBC );
  fc::sha1 fcheck;
  fc::sha1_hash( fcheck, (char*)mregion.get_address(), tf.size );
  if( fcheck != checksum ) {
    TORNET_THROW( "File checksum mismatch, got %1% expected %2%", %fcheck %checksum );
  }
  #endif
}

fc::vector<char> chunk_service::fetch_chunk( const fc::sha1& chunk_id ) {
#if 0
  tn::db::chunk::meta met;
  if( m_local_db->fetch_meta( chunk_id, met, false ) ) {
      d.resize(met.size);
      if( m_local_db->fetch_chunk( chunk_id, boost::asio::buffer(d) ) )
        return;
  }
  if( m_cache_db->fetch_meta( chunk_id, met, false ) ) {
      d.resize(met.size);
      if( m_cache_db->fetch_chunk( chunk_id, boost::asio::buffer(d) ) ) 
        return;
  }
  TORNET_THROW( "Unknown chunk %1%", %chunk_id );
#endif
  return fc::vector<char>();
}

/**
 *  Fetch and decode the tn file description, but not the chunks
 */
tornet_file chunk_service::fetch_tornet( const fc::sha1& tn_id, const fc::sha1& checksum ) {
#if 0
  tn::db::chunk::meta met;
  if( m_local_db->fetch_meta( tn_id, met, false ) ) {
      std::vector<char> tnet(met.size);
      if( m_local_db->fetch_chunk( tn_id, boost::asio::buffer(tnet) ) ) {
          fc::blowfish bf;
          fc::string fhstr = checksum;
          bf.start( (unsigned char*)fhstr.c_str(), fhstr.size() );
          bf.decrypt( (unsigned char*)&tnet.front(), tnet.size(), fc::blowfish::CBC );

          fc::sha1 check;
          tn::rpc::raw::unpack_vec(tnet, check );
          if( check != checksum ) {
            TORNET_THROW( "Checksum mismatch, got %1% expected %2%", %check %checksum );
          }

          tn::rpc::raw::unpack_vec(tnet, tf );
          if( tf.checksum != checksum ) {
            TORNET_THROW("Checksum mismatch, got %1% tn file said %2%", %checksum %tf.checksum );
          } else {
            slog( "Decoded checksum %1%", tf.checksum );
          }
          slog( "File name: %1%  size %2%", tf.name, tf.size );
          return;
      } else { 
        TORNET_THROW( "Unknown to find data for chunk %1%", %tn_id );
      }
  } else {
    TORNET_THROW( "Unknown chunk %1%", %tn_id );
  }
  #endif
  return tornet_file();
}

/**
 *  This method should fetch the tn file from local storage and then decode the chunks.
 *
 *  To publish a chunk search for the N nearest nodes and track the availability of that
 *  chunk.  If less than the desired number of hosts are found, then pick the host closest
 *  to the chunk and upload a copy.  If the desired number of hosts are found simply note
 *  the popularity of that chunk and schedule a time to check again.
 *
 *
 *
 */
void chunk_service::publish_tornet( const fc::sha1& tid, const fc::sha1& cs, uint32_t rep ) {
#if 0
  tornet_file tf;
  fetch_tornet( tid, cs, tf );
  for( uint32_t i = 0; i < tf.chunks.size(); ++i ) {
    //if( !m_local_db->exists(tf.chunks[i]) && !m_cache_db->exists(tf.chunks[i] ) ) {
    //  TORNET_THROW( "Unable to publish tn file because not all chunks are known to this node." );
    //  // TODO: Should we publish the parts we know?  Should we attempt to fetch the parts we don't?
   // }
    tn::db::publish::record rec;
    m_pub_db->fetch( tf.chunks[i].id, rec );
    rec.desired_host_count = rep;
    rec.next_update        = 0;
    m_pub_db->store( tf.chunks[i].id, rec );
  }
  tn::db::publish::record rec;
  m_pub_db->fetch( tid, rec );
  rec.desired_host_count = rep;
  rec.next_update        = 0;
  m_pub_db->store( tid, rec );
  #endif
}

void chunk_service::enable_publishing( bool state ) {
#if 0
  if( &boost::cmt::thread::current() != get_thread() ) {
    get_thread()->sync( boost::bind( &chunk_service::enable_publishing, this, state ) );
    return;
  }
  if( state != m_publishing ) {
      m_publishing = state;
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

