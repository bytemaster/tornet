#ifndef _SOCKS5_IO_HPP_
#define _SOCKS5_IO_HPP_
#include <boost/asio.hpp>
#include <boost/static_assert.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant.hpp>
#include <socks5/socks5.hpp>

#include <arpa/inet.h>

template <class T, typename SyncReadStream>
T read_class(SyncReadStream & s);

template <typename SyncWriteStream, class T>
void write_class(SyncWriteStream & s, T const& object);

// nitty gritty implementation below

// TODO: use exceptions, not abort()

template <class T> struct do_read;

template <>
struct do_read<octet_t> {

    template <typename SyncReadStream>
    octet_t operator()(SyncReadStream & s)
    {
        octet_t octet;

        std::size_t bytes_read(
                boost::asio::read(s,
                           boost::asio::mutable_buffers_1(
                                   boost::asio::mutable_buffer(&octet, sizeof(octet)))));
        if (bytes_read != sizeof(octet))
            abort();

        return octet;
    }
};

template <>
struct do_read<identifier> {

    template <typename SyncReadStream>
    identifier operator()(SyncReadStream & s)
    {
        octet_t version(read_class<octet_t>(s));
        assert(version == 5);

        octet_t nmethods(read_class<octet_t>(s));

        std::vector<login_method> methods;
        methods.reserve(nmethods);

        for (std::size_t i(0) ; i < nmethods ; ++i) {
            octet_t meth(read_class<octet_t>(s));

            methods.push_back(static_cast<login_method>(meth));
        }

        return identifier(methods);
    }
};

template <>
struct do_read<port_t>
{
    template <typename SyncReadStream>
    port_t operator()(SyncReadStream & s)
    {
        BOOST_STATIC_ASSERT(sizeof(port_t) == 2);
        port_t port;

        std::size_t port_read(
                    boost::asio::read(s,
                               boost::asio::mutable_buffers_1(
                                       boost::asio::mutable_buffer(&port, sizeof(port)))));

        if (port_read != 2)
            abort();

        return ntohs(port);
    }
};

template <>
struct do_read<ip::address_v4>
{
    // TODO: refactor using ntohs
    template <typename SyncReadStream>
    ip::address_v4 operator()(SyncReadStream & s)
    {
        octet_t input[4];
        std::size_t read(
                    boost::asio::read(s,
                               boost::asio::mutable_buffers_1(
                                       boost::asio::mutable_buffer(input, 4))));

        if (read != 4)
            abort();


        BOOST_STATIC_ASSERT(sizeof(unsigned long) * CHAR_BIT >= 4*8); // ensure that a long can hold an ip address

        // this needs to be ... better.. >:D
        unsigned long addr(input[0]); // load the first octet
        addr <<= 8; // make way for the next
        addr |= input[1]; // etc. ;P
        addr <<= 8;
        addr |= input[2];
        addr <<= 8;
        addr |= input[3];

        ip::address_v4 res(addr);

        DEBUG_MESSAGE("Just read ip address: " << res.to_string());

        return res;
    }
};


template <>
struct do_read<domain_address>
{
    template <typename SyncReadStream>
    domain_address operator()(SyncReadStream & s)
    {
        octet_t domain_size(read_class<octet_t>(s));


        std::vector<char> domain;
        domain.resize(domain_size);

        std::size_t domain_read(
                    boost::asio::read(s,
                               boost::asio::mutable_buffers_1(
                                       boost::asio::mutable_buffer(
                                               &domain.front(),
                                               domain_size))));

        if (domain_read != domain_size)
            abort();

        return domain_address(std::string(domain.begin(), domain.end()));
    }
};

template <>
struct do_read<address>
{
    template <typename SyncReadStream>
    address operator()(SyncReadStream & s)
    {
        octet_t type(read_class<octet_t>(s));

        if (type == address::IPV4_ADDRESS) {
            return read_class<ip::address_v4>(s);
        } else if (type == address::DOMAIN_ADDRESS) {
            return read_class<domain_address>(s);
        } else if (type == address::IPV6_ADDRESS)
            abort();
        else
            abort();
    }
};

template <>
struct do_read<request>
{
    template <typename SyncReadStream>
    request operator()(SyncReadStream & s)
    {
        octet_t ver(read_class<octet_t>(s));
        assert(ver == 5);

        octet_t cmd(read_class<octet_t>(s));

        octet_t rsv(read_class<octet_t>(s)); // TODO: rsv or cmd first?
        DEBUG_MESSAGE("rsv is" << int(rsv));
        assert(rsv == 0); // What's the point of rsv if it's always going to be 0 :S

        address addr(read_class<address>(s));
        port_t port(read_class<port_t>(s));

        DEBUG_MESSAGE("port: " << port);

        return request(static_cast<request::command>(cmd), addr, port);
    }
};

template <class name> struct do_write;

template <>
struct do_write<octet_t>
{
    template <typename SyncWriteStream>
    void operator()(SyncWriteStream & s, octet_t o)
    {
        std::size_t wrote(
                boost::asio::write(s, boost::asio::const_buffers_1(
                        boost::asio::const_buffer(&o, sizeof(o)))));
        if (wrote != sizeof(o))
            abort();
    }
};

template <>
struct do_write<method_selection>
{
    template <typename SyncWriteStream>
    void operator()(SyncWriteStream & s, method_selection const& ms)
    {
        write_class(s, octet_t(5)); // version

        assert(ms.method_ <= 0xFF);
        write_class(s, static_cast<octet_t>(ms.method_));
    }
};

template <>
struct do_write<port_t>
{
    template <typename SyncWriteStream>
    void operator()(SyncWriteStream & s, port_t p)
    {
        BOOST_STATIC_ASSERT(sizeof(port_t) == 2);

        port_t nhost(htons(p));

        std::size_t wrote(
                boost::asio::write(s, boost::asio::const_buffers_1(
                        boost::asio::const_buffer(&nhost, sizeof(nhost)))));

        DEBUG_MESSAGE("Wrote port:" << nhost);

        if (wrote != sizeof(nhost))
            abort();
    }
};

template <>
struct do_write<address>
{
    template <typename SyncWriteStream>
    void operator()(SyncWriteStream & s, address const& addr)
    {
        if (boost::asio::ip::address_v4 const* a = boost::get<ip::address_v4>(&addr)) {

            DEBUG_MESSAGE("Writing a v4 address");

            write_class(s, static_cast<octet_t>(address::IPV4_ADDRESS));

            BOOST_STATIC_ASSERT(sizeof(ip::address_v4::bytes_type) == 4);

            ip::address_v4::bytes_type bytes(a->to_bytes());
            assert(bytes.size() == sizeof(octet_t) * 4);

            for (int i(0); i < 4; ++i) {
                write_class(s, static_cast<octet_t>(bytes[i]));
            }
        } else
            abort();

    }

};

template <>
struct do_write<reply>
{
    template <typename SyncWriteStream>
    void operator()(SyncWriteStream & s, reply const& r)
    {
        write_class(s, octet_t(5)); // version

        assert(r.type_ <= 0xFF);
        DEBUG_MESSAGE("Writing a reply with type:" << r.type_);
        write_class(s, static_cast<octet_t>(r.type_));

        write_class(s, octet_t(0)); // reserved

        write_class(s, r.bnd_address_);
        write_class(s, r.bnd_port_);
    }
};


template <class T, typename SyncReadStream>
T read_class(SyncReadStream & s) {
    return do_read<T>()(s);
}

template <typename SyncWriteStream, class T>
void write_class(SyncWriteStream & s, T const& object) {
    return do_write<T>()(s, object);
}
#endif// _SOCKS5_IO_HPP_
