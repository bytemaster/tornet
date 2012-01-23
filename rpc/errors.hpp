#ifndef _BOOST_RPC_ERRORS_HPP_
#define _BOOST_RPC_ERRORS_HPP_
#include <boost/exception/all.hpp>
#include <boost/throw_exception.hpp>

namespace tornet { namespace rpc { 

    struct required_field_not_set : public virtual boost::exception, public virtual std::exception
    {
        const char*  what()const throw()    { return "required field not set";     }
        virtual void rethrow()const         { BOOST_THROW_EXCEPTION(*this);        }
    };

    namespace protocol_buffer { 

        template<typename T>
        void pack( std::vector<char>& msg, const T& v );

        struct key_type_mismatch : public virtual boost::exception, public virtual std::exception
        {
            key_type_mismatch( const char* name=0):m_name(name){}
            const char*  name(){ return m_name; }
            const char*  what()const throw()    { return "key/type mismatch";     }
            virtual void rethrow()const         { BOOST_THROW_EXCEPTION(*this);   }

            private:
                const char* m_name;
        };
        struct field_size_larger_than_buffer : public virtual boost::exception, public virtual std::exception
        {
            field_size_larger_than_buffer( const char* name=0):m_name(name){}
            const char*  name(){ return m_name; }
            const char*  what()const throw()    { return "field size is larger than buffer"; }
            virtual void rethrow()const         { BOOST_THROW_EXCEPTION(*this);               }

            private:
                const char* m_name;
        };
        struct field_not_found : public virtual boost::exception, public virtual std::exception
        {
            const char*  what()const throw()    { return "field not found";     }
            virtual void rethrow()const         { BOOST_THROW_EXCEPTION(*this);   }
        };
    } // namespace protocol buffer

} } // boost::rpc

#endif 
