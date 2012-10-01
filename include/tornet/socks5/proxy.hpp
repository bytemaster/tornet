#ifndef _SOCKS5_PROXY_HPP_
#define _SOCKS5_PROXY_HPP_

#include <list>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <tornet/socks5/common.hpp>
#include <tornet/socks5/io.hpp>

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

struct address_visitor : public boost::static_visitor<std::string>
{

    std::string operator()(ip::address const& addr) const {
        return addr.to_string();
    }

    std::string operator()(domain_address const& domain) const {
        return domain.domain_;
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

/**
 *  For each connection, this is called 
 */
void proxy::session(boost::shared_ptr<ip::tcp::socket> sock)
{
    identifier ident(read_class<identifier>(*sock));

    BOOST_FOREACH(login_method method, ident.methods_) {
       // DEBUG_MESSAGE("Client supports: " << method);
    }

    // TODO:  use NO_ACCEPTABLE_METH and finish
    if (ident.methods_.find(NO_AUTH_REQUIRED_METHOD) == ident.methods_.end()) {
      //wlog( "No acceptable socks5 login method" );
      return;
    }

    // TODO: proper method selection
    method_selection ms(NO_AUTH_REQUIRED_METHOD);
    write_class(*sock, ms);

    // auth stage, although we don't currently support it

    request req(read_class<request>(*sock));

    if (req.cmd_ != request::CONNECT_COMMAND) {
      //wlog( "Unsupported socks5 command" );
        return;
    }
    #if 0
    write_class(*sock, reply(reply::SUCCEEDED, boost::asio::ip::address_v4::from_string("74.125.228.103"), req.dst_port_));
 //   sock->flush();

    std::string host = boost::apply_visitor( address_visitor(), req.dst_addr_ );
    std::cerr<<" GOT "<<host<<std::endl;


    boost::system::error_code ec;

    const std::size_t buffer_size = 1024;
    std::vector<octet_t> buffer;
    buffer.resize(buffer_size);

    while (true) {

        std::size_t bytes_read(
                sock->read_some(
                    boost::asio::mutable_buffers_1(boost::asio::mutable_buffer(&buffer.front(), buffer.size())),
                    ec));

        if (bytes_read == 0) {
            if (ec == boost::asio::error::eof) {
                DEBUG_MESSAGE("Hit eof.");
                return;
            }
            DEBUG_MESSAGE("Error: " << ec.message());
         }

        DEBUG_MESSAGE("Just read:" << bytes_read << " bytes: " << std::string(buffer.begin(), buffer.end()));
        /*
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
        */
    }
    #endif

    /*


    boost::system::error_code ec;
    while( sock->is_open() && ec != boost::asio::error::eof) {
      char buf[1024];
      size_t r = sock->read_some( boost::asio::buffer( buf, sizeof(buf) ), ec );
      std::cerr<<"read: "<<r<< " "<<std::endl;
      std::cerr.write( buf, r );
      std::cerr.flush();
    }
    boost::asio::streambuf line;
    size_t r = boost::asio::read_until( *sock, line, '\n' );
    line.commit(r);
    std::istream is(&line);
    std::string l;
    std::getline(is,l);
    std::cerr<<"line: "<<l<<std::endl;
    }
    */
    //std::cerr<< "header: "<< line.data()<<std::endl;

    // TODO: req.dst_addr_ is the target 'domain' that we want to grab
    //
    // Then we need to read the HTTP REQUEST header from sock
    //
    // Then we need to parse the URL 
    //
    // Then we need to create the HTTP reply
    //
    // Easy as pie...

    ip::address target = boost::asio::ip::address_v4::from_string( "74.125.228.103"); //(boost::apply_visitor(address_visitor(), req.dst_addr_));

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
