#pragma once 
#include <cafs.hpp>
#include <fc/optional.hpp>

class cafs_file_db {
  public:
    cafs_file_db();
    ~cafs_file_db();
    
    void open( const fc::path& p );
    void close();

    void                          store( const cafs::file_ref& r );
    fc::optional<cafs::file_ref>  fetch( const fc::sha1& id );
    void                          remove( const fc::sha1& id );

  private:
    class impl;
    fc::shared_ptr<impl> my;

};
