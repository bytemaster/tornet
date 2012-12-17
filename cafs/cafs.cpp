#include "cafs.hpp"
#include <fc/base58.hpp>
#include <fc/error_report.hpp>
#include <fc/iostream.hpp>
#include <fc/exception.hpp>
#include <fc/raw.hpp>
#include <fc/datastream.hpp>
#include <fc/fstream.hpp>
#include <fc/json.hpp>
#include <fc/hex.hpp>
#include <fc/thread.hpp>
#include "cafs_file_db.hpp"

#include <boost/random.hpp>

#include <assert.h>

// the largest chunk size distributed by the network
#define MAX_CHUNK_SIZE (1024*1024*4)
#define MIN_SLICE_SIZE (1024*64)

// the largest file stored without a file_header 
// file headers allow us to download the file from 
// multiple sources by including 'sub-hashes' for slices.
#define IMBED_THRESHOLD (1024*1024)
#define RAND_THRESHOLD (1024*4)


extern "C" {
double pochisq(
    	const double ax,    /* obtained chi-square value */
     	const int df	    /* degrees of freedom */
     	);
      }

cafs::file_ref::file_ref()
:seed(0),pos(0),size(0),type(0){}

struct cafs::impl : public fc::retainable {

  ~impl(){
    file_db.close();
  }
  fc::path datadir;
  cafs_file_db file_db;

  // @pre p is a regular file > 2MB
  cafs::file_ref import_large_file( const fc::path& p );
  cafs::file_ref import_small_file( const fc::path& p );
};

cafs::cafs(){}

cafs::cafs( const cafs& c )
:my(c.my){}
cafs::cafs( cafs&& c ):my(fc::move(c.my)){}
cafs& cafs::operator = ( const cafs& c ) {
  my = c.my;
  return *this;
}
cafs& cafs::operator = ( cafs&& c ) {
  fc_swap(my,c.my);
  return *this;
}
cafs::~cafs() {
}

void cafs::open( const fc::path& dir ) {
  my.reset( new impl() );
  my->datadir  = dir;
  my->file_db.open( dir / "file_db" );
}

void cafs::close() {
  my.reset( nullptr );
}

fc::sha1 cafs::chunk_header::calculate_id()const {
  fc::sha1::encoder e;
  fc::raw::pack( e, *this );
  return e.result();
}
size_t cafs::chunk_header::calculate_size()const {
  size_t s = 0;
  for( uint32_t i = 0; i < slices.size(); ++ i ) {
    s += slices[i].size;
  }
  return s;
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
      fc::vector<char>      data(MAX_CHUNK_SIZE);
      fc::datastream<char*> ds(data.data()+1, data.size()-1);
      fc::raw::pack( ds, file_head );
      data[0] = cafs::file_header_type;
      
      //fc::datastream<const char*> ds2(data.data()+1, data.size()-1);
      //file_header tmp;
      //fc::raw::unpack( ds2, tmp );
      //slog( "test unpack %s", fc::json::to_string( tmp ).c_str() );
      data.resize( ds.tellp() );

      //slog( "pre randomized... '%s'", fc::to_hex( data.data(), 16 ).c_str() );
      size_t seed = randomize(data, *((uint64_t*)fc::sha1::hash(data.data(),data.size()).data()) );
      auto chunk_head = slice_chunk( data );
      //slog( "slice chunk %s", fc::json::to_string( chunk_head ).c_str() );
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
    data.resize( ds.tellp()+1 );

    size_t seed = randomize(data, *((uint64_t*)fc::sha1::hash(data.data(),data.size()).data()) );
    auto chunk_head = slice_chunk( data );
    link l( chunk_head.calculate_id(), seed );
    store_chunk( chunk_head, data );
    return l;
  }
  FC_THROW_MSG( "Unsupported file type while importing '%s'", p.string() );
  return cafs::link();
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
  slog( "%s %llu", fc::string(cid).c_str(), seed );
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
  if( fc::exists( cfile ) ) {
      wlog( "store chunk %s already exists", cfile.string().c_str() );
      return h;
  }
  slog( "store chunk %s size %d", cfile.string().c_str(), data.size() );
  fc::ofstream out( cfile.string(), fc::ofstream::binary );
  fc::raw::pack( out, head );
//  slog( "header size %d", fc::raw::pack(head).size() );
//  slog( "data %llu  %s", data.size(), fc::to_hex( data.data(), 16 ).c_str() );
  out.write( data.data(), data.size() );
  return h;
}

cafs::link::link( const fc::string& s ) {
  try {
    fc::vector<char> bin(16+40);
    size_t ns = fc::from_hex( s, bin.data(), bin.size() );
    bin.resize(ns);
    *this = fc::raw::unpack<link>( bin );
  } catch ( fc::error_report& er ) {
    seed = 0;
    throw FC_REPORT_PUSH( er, "Unable to decode link ${link}", fc::value().set("link",s) ); 
  }
}
cafs::link::operator fc::string()const {
  fc::vector<char> bin = fc::raw::pack( *this );
  return fc::to_hex( bin.data(), bin.size() );
}

void cafs::export_link( const cafs::link& l, const fc::path& d ) {
 try {
    if( !fc::exists( d.parent_path() ) ) { 
      slog( "create '%s'", d.generic_string().c_str() );
      fc::create_directories(d.parent_path()); 
    }
    fc::vector<char> ch = get_chunk( l.id );
    derandomize( l.seed, ch  );
    //  slog( "derandomized... '%s'", fc::to_hex( ch.data(), 16 ).c_str() );
    if( ch.size() == 0 ) {
      FC_THROW_MSG( "Empty Chunk!" );
    }
    switch( ch[0] ) {
      case file_data_type: {
        slog( "file data..." );
    //    slog( "post randomized... '%s'", fc::to_hex( ch.data(), ch.size() ).c_str() );
        fc::ofstream ofile( d, fc::ofstream::binary );
        ofile.write( ch.data()+1, ch.size()-1 );
        break;
      }
      case directory_type: {
        slog( "directory data..." );
        fc::datastream<const char*> ds(ch.data()+1, ch.size()-1);
        directory d;
        fc::raw::unpack( ds, d );
        slog( "%s", fc::json::to_string( d ).c_str() );
        for( auto itr = d.entries.begin(); itr != d.entries.end(); ++itr ) {
          slog( "entry: %s", itr->name.c_str() );
          /*
          fc::vector<char> fdat = get_file( itr->ref );
          switch( itr->ref.type ) {
            case file_data_type: {
              break;
            }
            case directory_type: {
              slog( "%s", fc::json::to_string( fc::raw::unpack<directory>(fdat) ).c_str() );
              break;
            }
            case file_header_type: {
              slog( "%s", fc::json::to_string( fc::raw::unpack<file_header>(fdat) ).c_str() );
              break;
            }
            default:
              wlog( "Unknown Type %d", int(itr->ref.type) );
          }
          */
        }
  
        break;
      }
      case file_header_type: {
        slog( "file header..." );
        fc::datastream<const char*> ds(ch.data()+1, ch.size()-1);
        slog( "data %s", fc::to_hex( ch.data(), 16 ).c_str() );
        file_header fh;
        fc::raw::unpack( ds, fh);
        slog( "%s", fc::json::to_string( fh ).c_str() );
  
        fc::ofstream ofile( d, fc::ofstream::binary );
        for( auto i = fh.chunks.begin(); i != fh.chunks.end(); ++i ) {
          fc::vector<char> c = get_chunk( i->hash );
          derandomize( i->seed, c );
          ofile.write( c.data(), c.size() );
        }
        break;
      }
      default:
        FC_THROW_MSG( "Unknown File Type %s", int(ch[0]) );
    }
 } catch ( fc::error_report& e ) {
   throw FC_REPORT_PUSH( e, "Unable to export link ${link} to ${path}", fc::value().set("link", fc::string(l)).set("path", d.generic_string() ) );
 }
}

fc::vector<char> cafs::get_chunk( const fc::sha1& id, uint32_t pos, uint32_t s ) {
  try {
    fc::string cid(id);
    fc::path cdir = my->datadir / cid.substr(0,2) / cid.substr(2,2) / cid.substr(4, 2);
    fc::path cfile = cdir / cid.substr( 6 );
    if( !fc::exists( cfile ) ) {
      FC_THROW_REPORT( "File ${path} does not exist", fc::value().set( "path", cfile ) );
    }
    fc::ifstream in( cfile, fc::ifstream::binary );
    chunk_header ch;
    fc::raw::unpack( in, ch );
    //  slog( "get chunk header: %s", fc::json::to_string( ch ).c_str() );
    if( s == uint32_t(-1) ) {
      s = ch.calculate_size();
    }
    // TODO: validate size is not too big... potential for crash allocating size read from disk
    //slog( "size %llu", s );
    //slog( "pos %llu", pos );
    fc::vector<char> v(s);
  //  in.read(v.data(),s);

    // validate that we actually have the data for the range pos -> pos +s
    fc::datastream<char*> ds(v.data(),s);
  #if 1
    uint32_t cpos = 0;
    for( auto itr = ch.slices.begin(); itr != ch.slices.end(); ++itr ) {
      if( pos >= cpos && pos < cpos + itr->size ) {
          fc::vector<char> tmp(itr->size);
          in.read(tmp.data(),itr->size);

          if( fc::sha1::hash( tmp.data(), tmp.size() ) != itr->hash ) {
            FC_THROW_REPORT( "Slice hash does not match header for chunk ${chunk_id}",
               fc::value().set( "chunk_id", id ).set("pos", pos).set("size",s).set("chunk_header",ch));
          }
          //slog( "pos %d   cpos %d  size %d  remain %d", pos, cpos, s, ds.remaining() );

          int start  = (pos-cpos);
          int left   = tmp.size()-start;
          int cbytes = (fc::min)(size_t(left),size_t(ds.remaining()));
          ds.write( tmp.data() + start, cbytes );
          pos += cbytes;
          s   -= cbytes;
      } else {
          in.seekg( itr->size, fc::ifstream::cur );
      }
      cpos += itr->size;
      if( ds.remaining() == 0 )
        return v;
    }

    //slog( "data %llu  %s", v.size(), fc::to_hex( v.data(), 16 ).c_str() );
  #endif

    return v;
  } catch ( fc::error_report& er ) {
    throw FC_REPORT_PUSH( er, "Unable to get chunk id ${chunk_id}", 
                              fc::value().set( "chunk_id", id ).set("pos", pos).set("size",s) );
  }
}

bool add_to_chunk( fc::datastream<char*>& chunk_ds, 
                   const fc::vector<char>& cur_file, 
                   const fc::string& name,
                   cafs::directory& dir,
                   cafs::file_type cur_file_type) {
   if( chunk_ds.remaining() > cur_file.size() ) {
      int  cur_file_pos = chunk_ds.tellp();
      slog( "add to chunk at pos %d", cur_file_pos );
      chunk_ds.write( cur_file.data(), cur_file.size() );
      dir.entries.push_back( cafs::directory::entry( name ) );
      dir.entries.back().ref.content = fc::sha1::hash( cur_file.data(), cur_file.size() );
      dir.entries.back().ref.pos     = cur_file_pos;
      dir.entries.back().ref.size    = cur_file.size();
      dir.entries.back().ref.type    = cur_file_type;
      dir.entries.back().ref.seed    = 0;
      return true;
   }
   return false;
}

void save_chunk( cafs& self, cafs::directory& dir, fc::vector<char>& cdata ) {
   uint64_t seed = randomize(cdata, rand() );
   cafs::chunk_header  chead    = slice_chunk( cdata );
   fc::sha1            chunk_id = self.store_chunk( chead, cdata );

   for( auto itr = dir.entries.begin(); itr != dir.entries.end(); ++itr ) {
      if( itr->ref.chunk == fc::sha1() ) {
        itr->ref.seed  = seed;
        itr->ref.chunk = chunk_id;
      }
   }
}

/**
 *  Imports the files in a directory and returns a directory object.
 */
cafs::directory cafs::import_directory( const fc::path& p ) {
   directory dir;

   fc::directory_iterator itr(p);
   fc::directory_iterator end;

   chunk_header            cur_chunk;
   fc::vector<char>        cur_chunk_data(MAX_CHUNK_SIZE);
   fc::datastream<char*>   cur_chunk_ds(cur_chunk_data.data(),cur_chunk_data.size());

   while( itr != end ) {
       fc::path    cur_path = *itr;
       slog( "%s", cur_path.generic_string().c_str() );
       fc::string  name = cur_path.filename().string();
       file_type   cur_file_type = unknown;

       fc::vector<char> cur_file;

       if( fc::is_directory( cur_path ) && name[0] != '.' ) {
           directory d = fc::async( [=]() { return import_directory( cur_path ); } );
           cur_file = fc::raw::pack( d );
           cur_file_type = directory_type;
       } else if( fc::is_regular_file( cur_path ) ) {
           if( fc::file_size(cur_path) > IMBED_THRESHOLD ) {
              cur_file = fc::raw::pack( import_file(cur_path) );
              cur_file_type = file_header_type;
           } else {
              cur_file.resize( fc::file_size(cur_path) ); 
              fc::ifstream inf( cur_path, fc::ifstream::binary );
              inf.read(cur_file.data(), cur_file.size() );
              cur_file_type = file_data_type;
           }
       }
       ++itr;

       fc::sha1 h = fc::sha1::hash(cur_file.data(),cur_file.size());
       fc::optional<file_ref> ofr = my->file_db.fetch(h);
       //slog( "                                               %s", fc::string(h).c_str() );
       if( ofr ) {
       //  wlog( "We already have a file ref for this..." );
         dir.entries.push_back( directory::entry(name) );
         dir.entries.back().ref = *ofr;
       }
      
       if( !ofr && !add_to_chunk( cur_chunk_ds, cur_file, name, dir, cur_file_type ) ) {
          // save cur chunk... 
        //  wlog( "                           save chunk size %llu", cur_chunk_ds.tellp() );
          cur_chunk_data.resize(cur_chunk_ds.tellp());
          save_chunk( *this, dir, cur_chunk_data );
          cur_chunk_data.resize(MAX_CHUNK_SIZE);
          cur_chunk_ds = fc::datastream<char*>(cur_chunk_data.data(),
                                               cur_chunk_data.size());
          add_to_chunk( cur_chunk_ds, cur_file, name, dir, cur_file_type );
       } 
       if( itr == end && cur_chunk_ds.tellp() > 0 ) {
        //  wlog( "                            save chunk size %llu", cur_chunk_ds.tellp() );
          cur_chunk_data.resize(cur_chunk_ds.tellp());
          save_chunk( *this, dir, cur_chunk_data );
       }
   }
   for( auto itr = dir.entries.begin(); itr != dir.entries.end(); ++itr ) {
     my->file_db.store( itr->ref );
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
   if( data.size() < RAND_THRESHOLD ) 
      return true;
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
      if( data.size() > 3 ) {
          gen.generate( dst, dst + (tmp.size()/sizeof(uint32_t)) );
          while( src != end ) {
            *dst = *src ^ *dst;
            ++dst; 
            ++src;
          }
      }
      if( int extra = data.size() % 4 ) {
        int t = 0;
        int r = 0;
        gen.generate( &r, (&r)+1 );
        memcpy( &t, src, extra );
        t ^= r;
        memcpy( dst, &t, extra );
      }
  } while ( !is_random(tmp) );
  fc_swap(data,tmp);
  return seed;
}

void derandomize( uint64_t seed, const fc::mutable_buffer& b, uint64_t offset ) {
  FC_THROW_REPORT( "this version of derandomize has not been updated yet" );
/*
  boost::random::mt19937 gen(seed);
  fc::vector<char> tmp(b.size+offset);
  uint32_t* src = (uint32_t*)b.data;
  uint32_t* end = src +(b.size/sizeof(uint32_t)); 
  uint32_t* dst = (uint32_t*)tmp.data();
  gen.generate( dst, dst + (tmp.size()/sizeof(uint32_t)) );
  while( src != end ) {
    *src = *src ^ *dst;
    ++dst; 
    ++src;
  }
  if( int extra = b.size % 4 ) {
    int t = 0;
    int r = 0;
    gen.generate( &r, (&r)+1 );
    memcpy( &t, src, extra );
    t ^= r;
    memcpy( src, &t, extra );
  }
  */
}

void derandomize( uint64_t seed, fc::vector<char>& data, uint64_t offset ) {
  boost::random::mt19937 gen(seed);
  fc::vector<char> tmp(data.size()+offset+3);
  uint32_t* src = (uint32_t*)data.data();
  uint32_t* end = src +(data.size()/sizeof(uint32_t)); 
  uint32_t* dst1 = (uint32_t*)(tmp.data());
  gen.generate( dst1, dst1 + (tmp.size()/sizeof(uint32_t)) );
  uint32_t* dst = (uint32_t*)(tmp.data()+offset);
  while( src != end ) {
    *src = *src ^ *dst;
    ++dst; 
    ++src;
  }
  if( int extra = data.size() % 4 ) {
    uint32_t t = 0;
    memcpy( &t, src, extra );
    t ^= *dst;
    memcpy( src, &t, extra );
  }
}
cafs::resource::resource(){}
cafs::resource::resource( fc::vector<char>&& r ):_data(fc::move(r)){}
cafs::resource::resource( const resource& r ):_data(r._data){}
cafs::resource::resource( resource&& r ):_data(fc::move(r._data)){}
cafs::resource& cafs::resource::operator = ( const cafs::resource& c ) {
  _data = c._data;
  return *this;
}
cafs::resource& cafs::resource::operator = ( cafs::resource&& c ) {
  fc_swap(c._data,_data);
  return *this;
}

fc::optional<cafs::directory>   cafs::resource::get_directory() {
  try {
      if( _data.size() && _data[0] == cafs::directory_type ) {
          directory d;
          fc::datastream<const char*> ds(_data.data()+1,_data.size()-1);
          fc::raw::unpack( ds, d );
          return d;
      }
      return fc::optional<directory>();
  } catch ( fc::error_report& e ) {
     throw FC_REPORT_PUSH( e, "Unable to read directory header" );
  }
}
char*                     cafs::resource::get_file_data() {
  if( _data.size() && _data[0] == cafs::file_data_type ) 
    return _data.data()+1;
  return nullptr;
}
size_t                    cafs::resource::get_file_size() {
  if( _data.size() && _data[0] == cafs::file_data_type ) 
    return _data.size()-1;
  return 0;
}
fc::optional<cafs::file_header> cafs::resource::get_file_header() {
  try {
    if( _data.size() && _data[0] == cafs::file_header_type ) {
        file_header d;
        fc::datastream<const char*> ds(_data.data()+1,_data.size()-1);
        fc::raw::unpack( ds, d );
        return d;
    }
    return fc::optional<file_header>();
  } catch ( fc::error_report& e ) {
     throw FC_REPORT_PUSH( e, "Unable to read file header" );
  }
}

fc::optional<cafs::resource> cafs::get_resource( const fc::sha1& h ) {
   auto ofr = my->file_db.fetch(h);
   if( ofr ) {
       return get_resource( *ofr );
   }
   return fc::optional<cafs::resource>();
}
fc::optional<cafs::resource> cafs::get_resource( const file_ref& r ) {
#if 0
    fc::vector<char> rd = get_chunk( r.chunk );
    //wlog( "rand  %lld %s", rd.size(), fc::to_hex(rd.data()+r.pos,r.size).c_str() );
    derandomize( r.seed, rd );//, r.pos );
    //wlog( "derand  %lld %s", rd.size(), fc::to_hex(rd.data()+r.pos,r.size).c_str() );
    fc::vector<char> tmp( r.size+1 );//rd.data() + r.pos, rd.data()+r.pos+r.size );
    tmp[0] = r.type;
    memcpy( tmp.data()+1, rd.data()+r.pos, r.size );
    return cafs::resource( fc::move(tmp) );

#else
  try {
    fc::vector<char> rd = get_chunk( r.chunk, r.pos, r.size );
//    fc::vector<char> rd2 = get_chunk( r.chunk );
//    if( 0 != memcmp( rd.data(), rd2.data()+r.pos, r.size )  ) {
//      FC_THROW_REPORT( "get_chunk failed to match");
//    }
    //wlog( "rand  %lld %s", rd.size(), fc::to_hex(rd.data()+r.pos,r.size).c_str() );
    derandomize( r.seed, rd, r.pos );
//    derandomize( r.seed, rd2 );
//    if( 0 != memcmp( rd.data(), rd2.data()+r.pos, r.size )  ) {
//      slog( "r.pos %d, \n%s\n%s", r.pos, fc::to_hex( rd.data(), rd.size() ).c_str(), 
//                        fc::to_hex( rd2.data()+r.pos, r.size ).c_str() );
//      FC_THROW_REPORT( "derandomize failed to match", fc::value().set("r.size", r.size).set("rd.size",rd.size()));
//    }
    //wlog( "derand  %lld %s", rd.size(), fc::to_hex(rd.data()+r.pos,r.size).c_str() );
    fc::vector<char> tmp( r.size+1 );//rd.data() + r.pos, rd.data()+r.pos+r.size );
    tmp[0] = r.type;
    memcpy( tmp.data()+1, rd.data(), r.size );
    return cafs::resource( fc::move(tmp) );
  } catch ( fc::error_report& e ) {
    throw FC_REPORT_PUSH( e, "Error getting resource", fc::value().set("file_ref",r) );
  }
#endif
}
fc::optional<cafs::resource> cafs::get_resource( const cafs::link& l ) {
  try {
      fc::vector<char> ch = get_chunk( l.id );
      derandomize( l.seed, ch  );
      return cafs::resource(fc::move(ch));
  } catch( ... ) {
      wlog( "%s", fc::except_str().c_str() );
  }
  return fc::optional<cafs::resource>();
}

