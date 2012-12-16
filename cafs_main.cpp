#include <cafs.hpp>
#include <fc/iostream.hpp>
#include <fc/sha1.hpp>

int main( int argc, char** argv ) {
  if( argc < 2 ) {
    fc::cout<<"Usage "<<argv[0]<<" CAFS_DIR\n";
    return -1;
  }
  try {
      cafs fs;
      fs.open( argv[1] );
/*
  wlog( "Testing Large File" );
      cafs::link l = fs.import( "dl.dmg" );

      fc::cout<<"Link: "<<fc::string(l.id) << " " << l.seed <<"\n";

      fs.export_link( l, "export_dir/outfile" );

      if( fc::sha1::hash( fc::path( "export_dir/outfile" ) ) != fc::sha1::hash( fc::path("dl.dmg") ) ){
        elog( "Exported file has different hash than import file" );
      }

  wlog( "Testing Small File" );
      cafs::link sf = fs.import( "tests.txt" );
      fs.export_link( sf, "export_dir/tests.txt" );

      if( fc::sha1::hash( fc::path( "export_dir/tests.txt" ) ) != fc::sha1::hash( fc::path("tests.txt") ) ){
        elog( "Exported file has different hash than import file" );
      }
*/
  wlog( "Testing Directory" );
      cafs::link df = fs.import( "park_dir" );
      fc::cout<<"Dir Link: "<<fc::string(df.id) << " " << df.seed <<"\n";
      fs.export_link( df, "export_dir/park_dir" );
  } catch ( ... ) {
    fc::cerr<<fc::except_str()<<"\n";
  }
  return 0;
}
