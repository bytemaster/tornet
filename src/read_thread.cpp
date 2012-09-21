
namespace tn {

void read_thread::listen( uint16_t p ) {
    slog( "Listening on port %d", p );
   _sock.open();
   _sock.set_receive_buffer_size( 3*1024*1024 );
   _sock.bind( fc::ip::endpoint( fc::ip::address(), p ) );
   _read_loop_complete = _thread.async( [=](){ read_loop(); } );
}

      /**
        One thread reads messages, find the connection, and enques the encrypted message and then
        places the connection in the 'decode' queue if it is not already there.

        One thread processes connections in the 'active' queue and focuses on 'decoding' messages
        in each connection.  Messages are decoded according to the connections 'priority'. Then
        they are inserted into the 'execute' queue.

        The execute queue happens in the 'node' thread and is responsible for the business logic. These
        requests are executed 'asynchronously' and given a priority level according to the connection.



        Messages come in and the connection object for the endpoint is found.  The message
        is pushed into the connection's queue (without decrypting it).  The connection is
        put into the active priority queue.  
          - read access to ep_to_con queue for most packets
          - write access for 'new connections' 
          - 
  
      */
void read_thread::read_loop() {
  try {
    uint32_t count = 0;
    while( !_done ) {
       // allocate a new buffer for each packet... we have no idea how long it may be around
       tn::buffer b;
       fc::ip::endpoint from;
       size_t s = _sock.receive_from( b.data(), b.size(), from );

       if( s ) {
          b.resize( s );

        auto itr = _ep_to_con.find(ep);
        if( itr == _ep_to_con.end() ) {
          slog( "creating new connection" );
          // failing that, create
          connection::ptr c( new connection( _node, from, _peers ) );
          { 
              fc::unique_lock<fc::mutex> lock(_ep_to_con_mutex);
              _ep_to_con[ep] = c;
          }
          c->handle_packet(b);
        } else { 
          itr->second->handle_packet(b); 
        }
       }
    }
  } catch ( ... ) { elog( "%s", fc::current_exception().diagnostic_information().c_str() ); }
}

connection* read_thread::get_connection( const fc::ip::endpoint& ep ) {
  fc::unique_lock<fc::mutex> lock(_ep_to_con_mutex);
  auto itr = _ep_to_con.find(ep);
  if( itr != _ep_to_con.end() ) return itr->second.get();
  return 0;
}



}
