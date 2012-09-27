#ifndef _TORNET_DOWNLOAD_STATUS_HPP_
#define _TORNET_DOWNLOAD_STATUS_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/signals.hpp>
#include <fc/fwd.hpp>

namespace fc {
  class sha1;
  class ostream;
  class string;
}

namespace tn {
  class chunk_service;
  class tornet_file;

  /**
   *  Downloads are a potentially long-lasting operation with a lot of 
   *  intermediate state.  They can be canceled and progress can be reported
   *  to the user.
   */
  class download_status : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<download_status> ptr;

      download_status( const fc::shared_ptr<chunk_service>& cs, 
                       const fc::sha1& tornet_id, 
                       const fc::sha1& check_sum, uint64_t seed, fc::ostream& out );

      ~download_status();

      void  pause();
      void  start();
      void  cancel();
      
      const fc::sha1&  tornet_id()const;
      const fc::sha1&  tornet_checksum()const;

      /**
       *  If the tornet description file has been fetched, this will return it.
       *
       *  @return nullptr if the file is not known, otherwise a pointer to the file.
       */
      const tornet_file* get_tornet_file()const;
      double             percent_complete()const;
      
      /**
       *  Every time an event occurs while downloading this signal is emited.
       *
       *  The first arg is a percent complete, the second argument is a 'status message'.
       */
      boost::signal<void(double,const fc::string&)> progress;
      boost::signal<void()>                         complete;

    private:
      class impl;
      fc::fwd<impl,552> my;
  };

}  // namespace tn

#endif // _TORNET_DOWNLOAD_STATUS_HPP_
