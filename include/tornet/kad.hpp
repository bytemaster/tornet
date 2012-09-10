#ifndef _TORNET_NET_KAD_HPP_
#define _TORNET_NET_KAD_HPP_
#include <tornet/node.hpp>

namespace tn {

 /**
  *   @class kad_search
  *   @breif Maintains state durring KAD node lookup.  
  *
  *   Doing a KAD lookup is not an exact science and is something that will be extended by derived 
  *   classes that deal with 'special cases' such as finding a chunk or 
  *   a particular service and where near matches count.
  *
  *   The general algorithm is to perform the lookup in parallel of 3 to
  *   reduce latency.  The algorithm will exit when the closest known node
  *   fails to return any closer nodes or when the target node is found.
  *
  */
 class kad_search : public fc::retainable {
   public:
      typedef fc::shared_ptr<kad_search> ptr;

      enum status {
        idle,
        searching,
        canceled,
        done
      };

      /**
       *  @param N - the number of results to return, default 20
       *  @param P - the level of parallelism, default 3
       */
      kad_search( const node::ptr& local_node, const fc::sha1& target, uint32_t N = 20, uint32_t P = 3 );
      virtual ~kad_search(){}

      void   start();
      void   cancel();
      void   wait( const fc::microseconds& s = fc::microseconds::max() );
      const  fc::sha1& target()const;

      status get_status()const;

      /**
       *  Returns a map of 'distance-to-target' to 'node_id'.  This map is updated every time
       *  new results are returned.  If the target is found, it will be the first item in
       *  the map.  
       *
       *  TODO: Document the thread this update occurs in... 
       */
      const std::map<fc::sha1,host>&  current_results()const {
        return m_current_results;
      }

      const node::ptr& get_node()const { return m_node; }

   protected:
      /**
       *  This method can be overloaded by derived classes to perform 
       *  operations on each node in the search path.
       *  
       */
      virtual void filter( const fc::sha1& id ){};
      void  set_status( status s ) { m_cur_status = s; }

      uint32_t m_n;
      uint32_t m_p;

   private:
      void search_thread();

      node::ptr                               m_node;                
      fc::sha1                                m_target;
      fc::sha1                                m_target_dist;


      // all pending operations that must complete
      // before the search is marked as 'done'
      fc::vector< fc::future<void> >        m_pending;

      status                                m_cur_status;

      /// stores endpoints that are on deck ordered by distance.
      std::map<fc::sha1, host>  m_search_queue;
      std::map<fc::sha1, host>  m_current_results;
 };




} // namespace tn

#endif
