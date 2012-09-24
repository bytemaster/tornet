#include <tornet/download_status.hpp>
#include <tornet/chunk_service.hpp>
#include <tornet/tornet_file.hpp>
#include <fc/thread.hpp>
#include <fc/stream.hpp>
#include <fc/sha1.hpp>
#include <fc/string.hpp>
#include <fc/optional.hpp>
#include <fc/fwd_impl.hpp>

namespace tn {

  class download_status::impl {
    public:
      impl( fc::ostream& o )
      :_out(o){}

      fc::ostream&       _out;    
      chunk_service::ptr _cs;
      fc::sha1           _tid;
      fc::sha1           _checksum;
      fc::optional<tornet_file> _tf;
  };

  download_status::download_status( const fc::shared_ptr<chunk_service>& cs, 
                   const fc::sha1& tornet_id, 
                   const fc::sha1& check_sum, fc::ostream& out )
  :my(out) {
    my->_cs = cs;
    my->_tid = tornet_id;
    my->_checksum = check_sum;
  }

  download_status::~download_status() {
  }

  void  download_status::pause() {
  }

  void  download_status::start() {
  }
  
  void  download_status::cancel() {
  }
  
  const fc::sha1&  download_status::tornet_id()const {
    return my->_tid;
  }

  const fc::sha1&  download_status::tornet_checksum()const {
    return my->_checksum;
  }

  /**
   *  If the tornet description file has been fetched, this will return it.
   *
   *  @return nullptr if the file is not known, otherwise a pointer to the file.
   */
  tornet_file* download_status::get_tornet_file()const {
    return 0;
  }

  double       download_status::percent_complete()const {
    return 0;
  }

}
