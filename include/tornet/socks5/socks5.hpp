#ifndef _SOCKS5_SOCKS5_HPP
#define _SOCKS5_SOCKS5_HPP
#include <stdint.h>
#include <set>
#include <vector>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <tornet/socks5/common.hpp>

using namespace boost::asio;

enum login_method {
    NO_AUTH_REQUIRED_METHOD = 0x0,
    GSSAPI_METHOD = 0x01,
    USERNAME_PASSWORD_METHOD = 0x02,
    NO_ACCEPTABLE_METHOD_METHOD = 0xFF,
};

// this is what the client sends the server to say what it supports
struct identifier
{
    identifier(std::vector<login_method> const& methods)
    {
        BOOST_FOREACH(login_method lm, methods) {
            methods_.insert(lm);
        }

    }

    std::set<login_method> methods_;
};

// this is the response a server sends to the clients identifier client
struct method_selection
{
    method_selection(login_method method)
        : method_(method)
    {}

    login_method method_;
};

// this is a domain i.e. www.google.com
struct domain_address
{

    domain_address(std::string const& domain)
        : domain_(domain)
    {}

    std::string domain_;
};

// TODO: address needs to support ipv6_address
struct address : public  boost::variant<ip::address_v4, domain_address>
{
    enum atyp { IPV4_ADDRESS = 0x1, DOMAIN_ADDRESS = 0x3, IPV6_ADDRESS = 0x4 };

    address(ip::address_v4 const& ip)
        : boost::variant<ip::address_v4, domain_address>(ip)
    {}

    address(domain_address const& da)
        : boost::variant<ip::address_v4, domain_address>(da)
    {}

    address(ip::address const& ip)
        : boost::variant<ip::address_v4, domain_address>(to_variant(ip))
    {}

private:
    boost::variant<ip::address_v4, domain_address> to_variant(ip::address const& addr)
    {
        if (addr.is_v4())
            return addr.to_v4();
        else
            abort();
    }

    // bool is(atyp);
    // atype type();
};

struct request
{
    enum command {CONNECT_COMMAND = 0x1, BIND_COMMAND = 0x2, UDP_ASSOCIATE = 0x3};

    request(command c, address a, port_t p)
        : cmd_(c), dst_addr_(a), dst_port_(p)
    {}

    command cmd_;
    address dst_addr_;
    port_t dst_port_;
};

struct reply {    
    enum rep {
        SUCCEEDED = 0x0,
        GENERAL_SOCKS_SERVER_FAIL = 0x1,
        CONNECTION_NOT_ALLOWED_BY_RULESET = 0x2,
        NETWORK_UNREACHABLE = 0x3,
        HOST_UNREACHABLE = 0x4,
        CONNECTION_REFUSED = 0x5,
        TTL_EXPIRED = 0x6,
        COMMAND_NOT_SUPPORT = 0x7,
        ADDRESS_TYPE_NOT_SUPPORTED = 0x8
    };

    reply(rep type, address const& bnd_address, port_t bnd_port)
        : type_(type), bnd_address_(bnd_address), bnd_port_(bnd_port)
    {}


    rep type_;
    address bnd_address_;
    port_t bnd_port_;
};

#endif //_SOCKS5_SOCKS5_HPP
