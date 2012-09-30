#ifndef _TORNET_DOWNLOAD_STATUS_HPP_
#define _TORNET_DOWNLOAD_STATUS_HPP_
#include <fc/shared_ptr.hpp>
#include <fc/vector.hpp>
#include <fc/fwd.hpp>

namespace fc {
  class sha1;
  class ostream;
  class string;
}

namespace tn {
  class chunk_service;
  class tornet_file;
  class link;


  class download_delegate : virtual public fc::retainable {
      public:
        typedef fc::shared_ptr<download_delegate> ptr;
        virtual ~download_delegate(){};
        virtual void download_data( fc::vector<char>&& data ){}
        virtual void download_status( double per, const fc::string& m  ){}
        virtual void download_error( int code, const fc::string& e ){}
        virtual void download_complete(){}
  };

  /**
   *  Downloads are a potentially long-lasting operation with a lot of 
   *  intermediate state.  They can be canceled and progress can be reported
   *  to the user.
   */
  class download_status : virtual public fc::retainable {
    public:
      typedef fc::shared_ptr<download_status> ptr;

      download_status( const fc::shared_ptr<chunk_service>& cs, 
                       const tn::link& ln,
                       const download_delegate::ptr& dd );

      ~download_status();

      void  pause();
      void  start();
      void  cancel();

      const fc::string& fail_string()const; // return the reason for any failure

      
      const tn::link&  get_link()const;

      /**
       *  If the tornet description file has been fetched, this will return it.
       *
       *  @return nullptr if the file is not known, otherwise a pointer to the file.
       */
      const tornet_file* get_tornet_file()const;
      double             percent_complete()const;

    private:
      class impl;
      fc::fwd<impl,672> my;
  };

}  // namespace tn

#endif // _TORNET_DOWNLOAD_STATUS_HPP_
