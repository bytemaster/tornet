#ifndef _CHUNK_SERVICE_HPP_
#define _CHUNK_SERVICE_HPP_
#include <tornet/db/chunk.hpp>
#include <tornet/db/publish.hpp>
#include <tornet/rpc/service.hpp>
#include <boost/reflect/reflect.hpp>


/**
 *  Defines how a tornet file is represented on disk.  For small files < 1MB
 *  the data is kept inline with the chunk.
 */
struct tornet_file {
  struct chunk_data {
    chunk_data():size(0){}
    chunk_data( uint32_t s, const scrypt::sha1& i  )
    :size(s),id(i){}

    uint32_t               size;
    scrypt::sha1           id;
    std::vector<uint32_t>  slices;
  };
  tornet_file():size(0){}
  tornet_file( const std::string& n, uint64_t s )
  :name(n),size(s){}
  
  scrypt::sha1              checksum;
  std::string               name;
  uint64_t                  size;
  std::vector< chunk_data > chunks;
  json::value               properties; 
  std::vector<char>         inline_data;
};

BOOST_REFLECT( tornet_file::chunk_data,
  (size)(id)(slices) )

BOOST_REFLECT( tornet_file,
  (checksum)(name)(size)(chunks)(properties)(inline_data) )


/**
 *  Provides an interface to two chunk databases, one local and one cache.
 *
 *  The local chunk database stores the chunks the user imports or downloads without
 *  respect to their distance from the node or access patterns. 
 *
 *  The cache chunk database stores chunks for other people so that it might earn
 *  credit with other nodes.  This cache database stores chunks based upon their
 *  access rate and distance from the node.
 *
 *  All chunks are random data encrypted via blowfish from the original file.  The blowfish
 *  key is the hash of the original file.  To restore a file you must know the hash of the
 *  file description chunk as well as the hash of the resulting file.  
 */
class chunk_service : public tornet::rpc::service {
    public:
      typedef boost::shared_ptr<chunk_service> ptr;
      chunk_service( const boost::filesystem::path& dbdir, 
                     const tornet::node::ptr& node, const std::string& name, uint16_t port,
                     boost::cmt::thread* t = &boost::cmt::thread::current() );

      boost::shared_ptr<tornet::db::chunk>&    get_cache_db()  { return m_cache_db; }
      boost::shared_ptr<tornet::db::chunk>&    get_local_db()  { return m_local_db; }
      boost::shared_ptr<tornet::db::publish>&  get_publish_db(){ return m_pub_db;   }

      /**
       *  Loads infile from disk, creates a tornet file and returns the tornet_id and thechecksum.
       *  Optionally writes the tornet file to disk as outfile.
       */
      void import( const boost::filesystem::path& infile, 
                   scrypt::sha1& tornet_id, scrypt::sha1& checksum,
                   const boost::filesystem::path& outfile = boost::filesystem::path() );

      /**
       *  Given a tornet_id and checksum, find the chunk, decrypt the tornetfile then find the
       *  chunks from the tornet file and finally reconstruct the file on disk.
       */
      void export_tornet( const scrypt::sha1& tornet_id, const scrypt::sha1& checksum );
     
      void fetch_tornet( const scrypt::sha1& tornet_id, const scrypt::sha1& checksum, tornet_file& f );

      void publish_tornet( const scrypt::sha1& tornet_id, const scrypt::sha1& checksum, uint32_t rep = 3 );

      void enable_publishing( bool state );
      bool publishing_enabled()const;


    protected:
      void publish_loop();
      virtual boost::any init_connection( const tornet::rpc::connection::ptr& con );

      boost::shared_ptr<tornet::db::chunk>    m_cache_db;
      boost::shared_ptr<tornet::db::chunk>    m_local_db;
      boost::shared_ptr<tornet::db::publish>  m_pub_db;
      bool m_publishing;
};

#endif
