#include "cafs.hpp"
#include <fc/iostream.hpp>
#include <fc/exception.hpp>
#include <fc/raw.hpp>
#include <fc/datastream.hpp>
#include <fc/fstream.hpp>

#include <boost/random.hpp>

#include <assert.h>

// the largest chunk size distributed by the network
#define MAX_CHUNK_SIZE (1024*1024*4)
#define MIN_SLICE_SIZE (1024*64)

// the largest file stored without a file_header 
// file headers allow us to download the file from 
// multiple sources by including 'sub-hashes' for slices.
#define IMBED_THRESHOLD (1024*1024)


extern "C" {
double pochisq(
    	const double ax,    /* obtained chi-square value */
     	const int df	    /* degrees of freedom */
     	);
      }

cafs::file_ref::file_ref()
:seed(0),pos(0),size(0),type(0){}

struct cafs::impl : public fc::retainable {
  fc::path datadir;

  // @pre p is a regular file > 2MB
  cafs::file_ref import_large_file( const fc::path& p );
  cafs::file_ref import_small_file( const fc::path& p );
};

cafs::cafs()
:my( new impl() ){ 
}

cafs::~cafs() {
}

void cafs::open( const fc::path& dir ) {
  my->datadir  = dir;
}

void cafs::close() {
}

fc::sha1 cafs::chunk_header::calculate_id()const {
  fc::sha1::encoder e;
  fc::raw::pack( e, *this );
  return e.result();
}

/**
 *  Divides data into slices and return a chunk header containing those
 *  slices + seed
 *
 *  @pre data is random
 */
cafs::chunk_header slice_chunk( const fc::vector<char>& data ) {
  cafs::chunk_header ch;
  // TODO reserve slices size!
  const char* pos = data.data();
  const char* end = pos + data.size();
  while( pos < end ) {
    auto s = fc::min( size_t(end-pos), size_t(MIN_SLICE_SIZE) );
    ch.slices.push_back( cafs::chunk_header::slice( s, fc::sha1::hash( pos, s ) ) );
    pos += s;
  }
  return ch;
}


cafs::link cafs::import( const fc::path& p ) {
  if( !fc::exists(p) )  {
    FC_THROW_MSG( "Path does not exist %s", p.string() );
  }
  if( fc::is_regular_file( p ) ) {
    if( fc::file_size(p) > IMBED_THRESHOLD ) {
      auto file_head = import_file(p);
      fc::vector<char> data(MAX_CHUNK_SIZE);
      fc::datastream<char*> ds(data.data()+1, data.size()-1);
      fc::raw::pack( ds, file_head );
      data[0] = cafs::file_header_type;

      size_t seed = randomize(data, *((uint64_t*)fc::sha1::hash(data.data(),data.size()).data()) );
      auto chunk_head = slice_chunk( data );
      store_chunk( chunk_head, data );

      return link( chunk_head.calculate_id(), seed );
    } else { // no header, just raw data from the file stored in the chunk
      fc::vector<char> data( fc::file_size(p)+1 );
      data[0] = file_data_type;
      fc::ifstream ifile( p.string(), fc::ifstream::binary );
      ifile.read( data.data()+1, data.size()-1 );
      size_t seed = randomize(data, *((uint64_t*)fc::sha1::hash(data.data(),data.size()).data()) );

      auto chunk_head = slice_chunk( data );

      store_chunk( chunk_head, data );
      return link( chunk_head.calculate_id(), seed );
    }
  }
  else if( fc::is_directory(p) ) {
    auto dir = import_directory(p);

    fc::vector<char> data(MAX_CHUNK_SIZE);
    fc::datastream<char*> ds(data.data()+1, data.size()-1);
    fc::raw::pack( ds, dir );
    data[0] = directory_type;

    size_t seed = randomize(data, *((uint64_t*)fc::sha1::hash(data.data(),data.size()).data()) );
    auto chunk_head = slice_chunk( data );
    link l( chunk_head.calculate_id(), seed );
    store_chunk( chunk_head, data );
    return l;
  }
  FC_THROW_MSG( "Unsupported file type while importing '%s'", p.string() );
}


/**
 *  Imports the file chunks into the database, but does not
 *  import the file_header itself as a chunk, as it may be
 *  imbedded within another chunk.
 *
 *  @pre is_regular_file(p) 
 */
cafs::file_header cafs::import_file( const fc::path& p ) {
  file_header head;  
  head.file_size = fc::file_size(p);

  fc::vector<char> chunk( MAX_CHUNK_SIZE );

  // divide file up into chunks and slices
  fc::ifstream in( p.string(), fc::ifstream::binary );
  uint32_t r = 0;
  while( r < head.file_size ) {
    size_t some = fc::min( size_t(chunk.size()), size_t(head.file_size-r) );
    in.read( chunk.data(), some );
    size_t seed = randomize(chunk, *((uint64_t*)fc::sha1::hash(chunk.data(),chunk.size()).data()) );

    chunk.resize(some);
    auto chunk_head = slice_chunk(chunk);
    auto chunk_id   = store_chunk( chunk_head, chunk );

    head.add_chunk( chunk_id, seed, chunk_head );
    r += some;
  }
  return head;
}

cafs::file_header::chunk::chunk( const fc::sha1& h, uint64_t s )
:hash(h),seed(s){}

void cafs::file_header::add_chunk( const fc::sha1& cid, uint64_t seed, const chunk_header& ch ) {
  chunks.push_back( chunk( cid, seed ) );
}

/**
 *  Validates that the chunk id matches the chunk_header and data.
 *  Stores the chunk to disk
 *
 *  @pre data is randomized
 */
fc::sha1 cafs::store_chunk( const chunk_header& head, const fc::vector<char>& data ) {
  // TODO: validate pre-conditions
  fc::sha1 h = head.calculate_id();
  fc::string cid(h);
  fc::path cdir = my->datadir / cid.substr(0,2) / cid.substr(2,2) / cid.substr(4, 2);
  create_directories( cdir );
  fc::path cfile = cdir / cid.substr( 6 );
  slog( "store chunk %s", cfile.string().c_str() );
  fc::ofstream out( cfile.string(), fc::ofstream::binary );
  fc::raw::pack( out, head );
  out.write( data.data(), data.size() );
  return h;
}

/**
 *  Imports the files in a directory and returns a directory object.
 */
cafs::directory cafs::import_directory( const fc::path& p ) {
   directory dir;

   fc::directory_iterator itr(p);
   fc::directory_iterator end;

   chunk_header cur_chunk;
   fc::vector<char> cur_data(MAX_CHUNK_SIZE);
   char*            cur_pos = cur_data.data();
   char*            cur_end = cur_pos + MAX_CHUNK_SIZE;

   int last_dir_entry = 0;

   while( itr != end ) {
       fc::path    cur_path = *itr;
       fc::string  name = cur_path.filename().string();

      if( name[0] != '.' ) {
         fc::vector<char>         file_data;
         file_type                file_t;

         if( fc::is_regular_file(cur_path) ) {
             uint64_t fsize = fc::file_size(cur_path); 

             // large files do not get stored 'inline'
             if( fsize > IMBED_THRESHOLD  ) {
                file_header head = import_file( cur_path );
                fc::vector<char> head_data = fc::raw::pack(head);
                if( head_data.size() > IMBED_THRESHOLD ) {
                  assert( !"Not implemented" );
                  // must be a very big file!
                  // we need to push it off to its own chunk
                  // create a file_header_type and chunk head_data

                  file_t = file_header_type;
                } else {
                  file_data = fc::move(head_data);
                  file_t = file_header_type;
                }
             } else {
                fc::ifstream in( name.c_str(), fc::ifstream::binary );
                file_data.resize(fsize);
                in.read( cur_pos, fsize );
                file_t     = file_data_type;
             }
         } else if ( fc::is_directory(cur_path) ) {
              directory d = import_directory(cur_path);
              file_data   = fc::raw::pack(d);
              file_t      = cafs::directory_type;
              if( file_data.size() > IMBED_THRESHOLD ) {
                // really big directory object you got there!  
                assert( !"Large Directory Not implemented" );
                // TODO encode this directory as a file_header with directory_data flag. 
                // Make the file_data the packed file_header and the file_type a file_header_type
              }
         }
         // calculate he hash for this slice

         // TODO: DO WE ALREADY KNOW A FILE_REF for this file_hash, if so we can use it 
         // instead of inserting the data into a new chunk.
         if( false ) { // we already have this chunk
            // what about two files in the same directory with the same contents but
            // different names??  We would have to check dir.files since the last_dir_entry
            
            
            // dir.insert( name, fhash, existing_file_ref );
         } else { // we don't have this chunk yet
             // if we don't have room in the current chunk, close it out and start a new one
             if( file_data.size() >= uint64_t(cur_end-cur_pos ) ) {
                // finish current chunk, start next chunk
                
                cur_data.resize(cur_pos - cur_data.data());
                uint64_t seed = randomize(cur_data, *((uint64_t*)fc::sha1::hash(cur_data.data(),cur_data.size()).data() ));
                cur_pos = cur_data.data();
                
                // update header with sha1 of randomized slices

                auto itr = cur_chunk.slices.begin();
                for( ;itr != cur_chunk.slices.end(); ++itr ) {
                  itr->hash = fc::sha1::hash( cur_pos, itr->size );
                  cur_pos += itr->size;
                }

                // store the chunk on disk
                fc::sha1 chunk_id = store_chunk( cur_chunk, cur_data );

                for( uint32_t i = last_dir_entry; i < dir.entries.size(); ++i ) {
                  // -1 is reserved for uninit seed, TODO: ensure randomize() method never uses -1
                  if( dir.entries[i].ref.seed == uint64_t(-1) ) { 
                      dir.entries[i].ref.chunk = chunk_id;
                      dir.entries[i].ref.seed  = seed;

                      // TODO: store this file ref in our DB so that we
                      // can reuse it if we see it again.
                  }
                }
                last_dir_entry = dir.entries.size();
                
                // reset the data
                cur_data.resize(1024*1024*2);
                cur_pos = cur_data.data();
                cur_chunk.slices.resize(0);
             }
             // ASSERT there is room for the data now.

             // push the data into the current chunk

             
             // copy the slice into the chunk
             memcpy( cur_pos, file_data.data(), file_data.size() );
             // add the slice to the chunk slices table
             cur_chunk.slices.push_back( chunk_header::slice(file_data.size()) ); // we will calc the hash later

             // move the position in the current chunk.
             cur_pos += cur_data.size();

             // add the partial file hash to the directory
             dir.entries.push_back( directory::entry( name ) );
             dir.entries.back().ref.content = fc::sha1::hash(file_data.data(),file_data.size() );
             dir.entries.back().ref.type = file_t;
             dir.entries.back().ref.pos = cur_pos - cur_data.data(); 
             dir.entries.back().ref.size = cur_data.size();
             // chunk_id + seed to be set at the end of the current chunk.
         }
      }
      ++itr;
   }
   return dir;
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


