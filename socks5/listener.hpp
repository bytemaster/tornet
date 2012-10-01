#pragma once
#include <memory>
#include <boost/optional.hpp>
#include "../common.hpp"

// (man in the middle)
struct mitm
{
    virtual ~mitm();
 
    /// Triggered when the remote sends data \a attempt, the client
    /// is presented with the data this function returns
    virtual byte_array attempt_receive(byte_array const& attempt) {
        return attempt;
    }
 
    /// Triggered when the client sends data \a attempt, the remote
    /// will recieve the return data
    virtual byte_array attempt_send(byte_array const& attempt) {
        return attempt;
    }
};


struct mitm_manager
{
    virtual ~mitm_manager();
    
    /// Triggered when the client attempts to connect to a remote.
    /// If the result is not initialized, the proxy will deny the
    /// connection to the client.
    virtual boost::optional< std::auto_ptr<mitm> > attempt_connect(ip::address const& addr, port_t port) = 0;
};
