#ifndef BOOST_PP_IS_ITERATING
  #ifndef TORNET_CLIENT_INTERFACE_HPP
  #define TORNET_CLIENT_INTERFACE_HPP
  #include <boost/bind.hpp>
  #include <boost/preprocessor/repetition.hpp>
  #include <boost/preprocessor/seq/for_each.hpp>

  #include <boost/fusion/container/vector.hpp>
  #include <boost/fusion/container/generation/make_vector.hpp>
  #include <boost/fusion/functional/generation/make_fused_function_object.hpp>
  #include <boost/fusion/functional/generation/make_unfused.hpp>
  #include <boost/reflect/void.hpp>
  #include <boost/signals.hpp>
  #include <boost/reflect/vtable.hpp>

  #include <tornet/rpc/connection.hpp>
  #include <tornet/rpc/client_base.hpp>

  namespace tornet { namespace rpc {
    /**
     *  @brief Specialized to mirror the member 
     *         variable/method pointed to by MemberPtr
     */
    template<typename MemberPtr>
    struct client_member;

    class client_base;

    namespace detail {
      namespace client_interface {
        template<typename VTableType>
        class set_visitor {
          public:
            set_visitor( VTableType& vt, tornet::rpc::client_base* ci, uint16_t& mid )
            :m_method_id(mid),m_ci(ci),vtbl(vt){}

            template<typename M, typename InterfaceName, M (InterfaceName::*m)>
            void operator()( const char* name )const {
              (vtbl.*m).set( m_method_id++, m_ci );
            }
          private:
            uint16_t&              m_method_id;
            tornet::rpc::client_base*   m_ci;
            VTableType&            vtbl;
        };

        struct client_member_base {
          void set( uint16_t mid, tornet::rpc::client_base* ci ) {
            m_method_id    = mid;
            m_ci           = ci;
          }
          protected:
            uint16_t                  m_method_id;
            tornet::rpc::client_base* m_ci;
        };
      }
    }
    
    /**
     *  @brief Interface Delegate that adapts an interface for asynchronous
     *         calls over the tornet network
     *  
     *  To specialize how a particular member is mirrored define
     *  the partial specialization of client_member for your type.
     *
     *  @code
     *  template<typename Type, typename Class>
     *  client_member<Type(Class::*)> 
     *  @endcode
     */
    struct client_interface 
    {
      /**
       * @brief Implements the InterfaceDelegate meta-function to 
       *      determine what type to create to mirror MemberPointer 
       *      in boost::reflect::vtable used by boost::reflect::any_ptr
       */
      template<typename MemberPointer>
      struct calculate_type {
        typedef client_member<MemberPointer>  type; 
      };

      template<typename ClientType>
      static void set( ClientType& cl ) {
        uint16_t mid = 0;
        boost::reflect::visit( cl, detail::client_interface::set_visitor<typename ClientType::vtable_type>( *cl, &cl, mid ) );
      }
    };


  #define PARAM_NAME(z,n,type)         BOOST_PP_CAT(a,n)
  #define PARAM_PLACE_HOLDER(z,n,type) BOOST_PP_CAT(_,BOOST_PP_ADD(n,1) )
  #define PARAM_TYPE_NAME(z,n,type)   BOOST_PP_CAT(typename A,n)
  #define PARAM_TYPE(z,n,type)   BOOST_PP_CAT(A,n)
  #define PARAM_ARG(z,n,type)     PARAM_TYPE(z,n,type) PARAM_NAME(z,n,type)

#        ifndef TORNET_CLIENT_IMPL_SIZE
#           define TORNET_CLIENT_IMPL_SIZE 8
#        endif

#       include <boost/preprocessor/iteration/iterate.hpp>
#       define BOOST_PP_ITERATION_LIMITS (0, TORNET_CLIENT_IMPL_SIZE -1 )
#       define BOOST_PP_FILENAME_1 <tornet/rpc/client_interface.hpp>
#       include BOOST_PP_ITERATE()

  #undef PARAM_NAME
  #undef PARAM_TYPE
  #undef PARAM_ARG

  } } // namespace tornet::rpc
  #endif // TORNET_CLIENT_INTERFACE_HPP

#else // BOOST_PP_IS_ITERATING

#define n BOOST_PP_ITERATION()
#define PARAM_NAMES          BOOST_PP_ENUM(n,PARAM_NAME,A) // name_N
#define PARAM_PLACE_HOLDERS  BOOST_PP_ENUM_TRAILING(n,PARAM_PLACE_HOLDER,A) // _(N+1)
#define PARAM_ARGS           BOOST_PP_ENUM(n,PARAM_ARG,A) // TYPE_N name_N
#define PARAM_TYPE_NAMES     BOOST_PP_ENUM(n,PARAM_TYPE_NAME,A) // typename TYPE_N
#define PARAM_TYPES          BOOST_PP_ENUM(n,PARAM_TYPE,A) // TYPE_N

template<typename R, typename Class BOOST_PP_COMMA_IF(n) PARAM_TYPE_NAMES>
struct client_member<R(Class::*)(PARAM_TYPES)const> : public detail::client_interface::client_member_base
{
  typedef typename boost::reflect::adapt_void<R>::result_type    result_type;
  typedef boost::cmt::future<result_type>                        future_type;
  typedef client_member                                          self_type;
  typedef boost::fusion::vector<PARAM_TYPES>                     fused_params;
  typedef boost::function_traits<result_type(PARAM_TYPES)>       traits;
  static const bool                                              is_const = true;
  static const bool                                              is_signal = false;

  typedef typename boost::remove_pointer<result_type(*)(PARAM_TYPES)>::type   signature;

  inline future_type operator()( PARAM_ARGS )const {
    return (*this)( boost::fusion::make_vector(PARAM_NAMES) );
  }
  inline future_type operator() ( const fused_params& fp )const {
    //slog( "%1% %2% %3%",m_service_type, m_method_id,  m_con.get() );
    return m_ci->call<result_type,fused_params>( m_method_id, fp );
  }
};

template<typename R, typename Class  BOOST_PP_COMMA_IF(n) PARAM_TYPE_NAMES>
struct client_member<R(Class::*)(PARAM_TYPES)>  : public detail::client_interface::client_member_base
{
  typedef typename boost::reflect::adapt_void<R>::result_type                result_type;
  typedef boost::cmt::future<result_type>                    future_type;
  typedef client_member                                      self_type;
  typedef boost::fusion::vector<PARAM_TYPES>                 fused_params;
  typedef boost::function_traits<result_type(PARAM_TYPES)>   traits;
  typedef boost::function<result_type(const fused_params&)>  delegate_type;
  static const bool                                          is_const  = false;
  static const bool                                          is_signal = false;

  // boost::result_of
  typedef typename boost::remove_pointer<result_type(*)(PARAM_TYPES)>::type signature;

  inline future_type operator()( PARAM_ARGS ) {
    return (*this)( boost::fusion::make_vector(PARAM_NAMES) );
  }
  inline future_type operator() ( const fused_params& fp ) {
    //slog( "%1% %2% %3%",m_service_type, m_method_id,  m_con.get() );
    return m_ci->call<result_type,fused_params>( m_method_id, fp );
  }

};


#undef n
#undef PARAM_NAMES         
#undef PARAM_PLACE_HOLDERS
#undef PARAM_ARGS        
#undef PARAM_TYPE_NAMES 
#undef PARAM_TYPES     

#endif // BOOST_PP_IS_ITERATING
