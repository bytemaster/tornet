#ifndef _SOCKS5_PROXY_HPP_
#define _SOCKS5_PROXY_HPP_

#include <list>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <socks5/common.hpp>
#include <socks5/io.hpp>

struct proxy {
    proxy(port_t port);

    void async_listen();
    void listen();
private:
    void session(boost::shared_ptr<ip::tcp::socket> sock);

    boost::asio::io_service service_;
    boost::asio::ip::tcp::acceptor acceptor_;
};



proxy::proxy(port_t port)
    : service_()
    , acceptor_(service_, ip::tcp::endpoint(ip::tcp::v4(), port))
{
}

void proxy::async_listen()
{
    boost::thread(boost::bind(&proxy::listen, this));
}

struct address_visitor : public boost::static_visitor<ip::address>
{

    ip::address operator()(ip::address const& addr) const {
        return addr;
    }

    ip::address operator()(domain_address const& domain) const {
        boost::asio::io_service service;
        ip::tcp::resolver resolver(service);
        ip::tcp::resolver::query q(domain.domain_, "http");
        ip::tcp::resolver::iterator it(resolver.resolve(q));


        if (it == ip::tcp::resolver::iterator())
            abort();
        else {
            ip::tcp::endpoint ep(*it);

            DEBUG_MESSAGE("Resolved domain " << domain.domain_ << " to:" << ep.address());

            return ep.address();
        }

        return ip::address();
    }
};


void echoer(ip::tcp::socket & in, ip::tcp::socket & out, std::string const& debug_message)
{
    assert(in.is_open());
    assert(out.is_open());

    boost::system::error_code ec;

    const std::size_t buffer_size = 1024;
    std::vector<octet_t> buffer;
    buffer.resize(buffer_size);

    while (true) {

        std::size_t bytes_read(
                in.read_some(
                    boost::asio::mutable_buffers_1(boost::asio::mutable_buffer(&buffer.front(), buffer.size())),
                    ec));

        if (bytes_read == 0) {
            if (ec == boost::asio::error::eof) {
                DEBUG_MESSAGE("Hit eof.");
                return;
            }

            DEBUG_MESSAGE("Error: " << ec.message());
            abort();
         }

        DEBUG_MESSAGE("Just read:" << bytes_read << " bytes: " << std::string(buffer.begin(), buffer.end()));

        std::size_t bytes_wrote(
                boost::asio::write(
                        out,
                        boost::asio::const_buffers_1(boost::asio::const_buffer(&buffer.front(), bytes_read)),
                        boost::asio::transfer_all(),
                        ec));

        if (ec || bytes_wrote != bytes_read) {
            DEBUG_MESSAGE("Error: " << ec.message() << " in " << debug_message);
            abort();
        }
    }
}

// TODO: make this an auto-pointer (or unique_ptr?)
void proxy::session(boost::shared_ptr<ip::tcp::socket> sock)
{
    identifier ident(read_class<identifier>(*sock));

    BOOST_FOREACH(login_method method, ident.methods_) {
        DEBUG_MESSAGE("Client supports: " << method);
    }

    // TODO:  use NO_ACCEPTABLE_METH and finish
    if (ident.methods_.find(NO_AUTH_REQUIRED_METHOD) == ident.methods_.end())
        abort();

    // TODO: proper method selection
    method_selection ms(NO_AUTH_REQUIRED_METHOD);
    write_class(*sock, ms);

    // auth stage, although we don't currently support it

    request req(read_class<request>(*sock));

    // TODO: we only support connect for now
    if (req.cmd_ != request::CONNECT_COMMAND)
        abort();

    ip::address target(boost::apply_visitor(address_visitor(), req.dst_addr_));

    DEBUG_MESSAGE("Trying to connect to:" << target.to_string() << " on port: " << req.dst_port_);

    ip::tcp::endpoint endpoint(target, req.dst_port_);

    boost::system::error_code ec;
    boost::asio::io_service service;
    ip::tcp::socket remote(service);
    remote.connect(endpoint, ec);

    if (ec) {
        DEBUG_MESSAGE("Error: " << ec.message());
        abort();
    }

    ip::tcp::endpoint bnd(remote.local_endpoint());

    assert(sock->is_open());

    write_class(*sock, reply(reply::SUCCEEDED, bnd.address(), bnd.port()));

    boost::thread client_to_remote(boost::bind(&echoer, boost::ref(*sock), boost::ref(remote), std::string("client-to-remote")));
    boost::thread remote_to_client(boost::bind(&echoer, boost::ref(remote), boost::ref(*sock), std::string("remote-to-client")));

    remote_to_client.join();
    client_to_remote.join();

    DEBUG_MESSAGE("Session finished.");
}



void proxy::listen()
{
    while (true) {;
        DEBUG_MESSAGE("Pausing to accept");

        boost::asio::io_service service;

        boost::shared_ptr<ip::tcp::socket> sock(new ip::tcp::socket(service));
        boost::system::error_code ec;
        acceptor_.accept(*sock, ec);

        if (ec) { DEBUG_MESSAGE(ec.message()); abort(); }

        boost::thread a(boost::bind(&proxy::session, this, sock));
    }

}

#endif // _SOCKS5_PROXY_HPP_
