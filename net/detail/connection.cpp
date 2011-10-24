#include <tornet/net/detail/connection.hpp>
#include <boost/cmt/log/log.hpp>
#include <scrypt/super_fast_hash.hpp>
#include <scrypt/scrypt.hpp>
#include <boost/rpc/base64.hpp>
#include <tornet/net/detail/node_private.hpp>
#include <boost/rpc/datastream.hpp>
#include <boost/chrono.hpp>

namespace tornet { namespace detail {

using namespace boost::chrono;

connection::connection( detail::node_private& np, const endpoint& ep )
:m_node(np),m_remote_ep(ep), m_cur_state(uninit),m_advance_count(0) {
}

connection::connection( detail::node_private& np, const endpoint& ep, 
            const node_id& auth_id, state_enum init_state )
:m_node(np),m_advance_count(0) {
   m_remote_ep = ep;
   m_remote_id = auth_id;
   m_cur_state = init_state;
}
connection::~connection() {
}


/**
 *  Start of state machine, switch to the proper handler for the
 *  current state.
 */
void connection::handle_packet( const tornet::buffer& b ) {
  switch( m_cur_state ) {
    case failed:       wlog("failed con");     return;
    case uninit:       handle_uninit(b);       return;
    case generated_dh: handle_generated_dh(b); return;
    case received_dh:  handle_received_dh(b);  return;
    case authenticated:handle_authenticated(b);return;
    case connected:    handle_connected(b);    return;
  }
}


/**
 *  In this state we know nothing about the remote_ep and
 *  we have sent them nothing.  We can only receive public
 *  keys and always respond with our newly 
 *  generated public key.
 */
void connection::handle_uninit( const tornet::buffer& b ) {
  slog("");
  BOOST_ASSERT( m_cur_state == uninit );
  if( (b.size() % 8) && b.size() > 56 ) { // key exchange
    generate_dh();
    send_dh();
    if( process_dh( b ) ) {
        send_auth();
    }
    goto_state( received_dh );
    return;
  } else {
    generate_dh();
    send_dh();
    goto_state( generated_dh );
    return;
  } 
}

/**
 *  In this state we have our public key and simply reuse it
 *  until we get their public key.
 */
void connection::handle_generated_dh( const tornet::buffer& b ) {
  slog("");
  BOOST_ASSERT( m_cur_state == generated_dh );
  if( b.size() % 8 && b.size() > 56 ) { // pub key
   //  send_dh();
    if( process_dh( b ) ) {
      send_auth();
    }
    goto_state( received_dh );
    return;
  }
  send_dh();
}

void connection::handle_received_dh( const tornet::buffer& b ) {
  slog("");
  BOOST_ASSERT( m_cur_state == received_dh );
  if( b.size() % 8 && b.size() > 56 ) { // pub key
    send_dh();
    if( process_dh( b ) ) {
      send_auth();
    }
    return;
  }
  if( b.size() % 8 == 0 ) {
    // we have a dh, so we better be able to decode it
    // if not reset!  Decode message will advance to authenticated
    // if the message is an auth message, otherwise it will ignore it.
    slog("decode" );
    if( !decode_packet( b ) ) {
      wlog( "Reset connection to uninit" );
      goto_state(uninit);
      handle_uninit(b);
    }
    return;
  }
  wlog( "ignoring malformed packet" );
}

void connection::handle_authenticated( const tornet::buffer& b ) {
  slog("");
  BOOST_ASSERT( m_cur_state == authenticated );
  if( 0 == b.size() % 8 ) { // not dh pub key
    if( !decode_packet( b ) ) {
      slog("faield to decode packet... " );
      reset();
      handle_uninit(b);
      return;
    }
  } else if( b.size() > 56 ) { // dh pub key 
    reset();
    handle_uninit(b);
  }
  wlog( "ignoring malformed packet, less than 56 and not multiple of 8" );
}

// same as authenticated state...
void connection::handle_connected( const tornet::buffer& b ) {
  slog("");
  BOOST_ASSERT( m_cur_state == connected );

  if( 0 == b.size() % 8 && decode_packet(b) ) 
    return; // sucess

  slog("faield to decode packet... " );
  reset();
  handle_uninit(b);
}
bool connection::handle_auth_resp_msg( const tornet::buffer& b ) {
  wlog("auth response %1%", int(b[0]) );
  if( b[0] ) { 
    goto_state( connected ); 
    return true; 
  }
  reset();
  return false;
}

bool connection::handle_close_msg( const tornet::buffer& b ) {
  wlog("closed msg" );
  reset();
  return true;
}

void connection::reset() {
  close_channels();
  m_node.update_dist_index( m_remote_id, 0 );
  goto_state(uninit); 
}

void connection::send_close() {
  slog( "sending close" );
  char resp = 1;
  send( &resp, sizeof(resp), close_msg );
}
void connection::send_auth_response(bool r ) {
  char resp = r;
  send( &resp, sizeof(resp), auth_resp_msg );
}

bool connection::decode_packet( const tornet::buffer& b ) {
    slog("");
    m_bf->reset_chain();
    m_bf->decrypt( (unsigned char*)b.data(), b.size(), scrypt::blowfish::CBC );

    uint32_t checksum = scrypt::super_fast_hash( (char*)b.data()+4, b.size()-4 );
    if( memcmp( &checksum, b.data(), 3 ) != 0 ) {
      elog( "decrytpion checksum failed" );
      return false;
    }
    uint8_t pad = b[3] & 0x07;
    uint8_t msg_type = b[3] >> 3;

    slog( "%1% bytes type %2%", b.size(), int(msg_type) );

    switch( msg_type ) {
      case data_msg:       return handle_data_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_msg:       return handle_auth_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_resp_msg:  return handle_auth_resp_msg( b.subbuf(4,b.size()-4-pad) );  
      case close_msg:      return handle_close_msg( b.subbuf(4,b.size()-4-pad) );  
      default:
        wlog( "Unknown message type" );
    }
    return true;
}

bool connection::handle_data_msg( const tornet::buffer& b ) {
  if( m_cur_state != connected ) {
    wlog( "Data message received while not connected!" );
    close();
    return false;
  }
  uint16_t src, dst;
  boost::rpc::datastream<const char*> ds(b.data(), b.size() );
  ds >> src >> dst;

  // locally channels are indexed by 'local' << 'remote'
  uint32_t k = (uint32_t(dst) << 16) | src;

  
  boost::unordered_map<uint32_t,channel>::iterator itr= m_channels.find(k);
  if( itr != m_channels.end() ) {
    itr->second.recv( b.subbuf(4) );
  } else {
    slog( "src %1% dst %2%", src, dst );
    try {
        channel ch = m_node.create_channel( shared_from_this(), src, dst );
        m_channels[k] = ch;
        ch.recv( b.subbuf(4) );
    } catch( boost::exception& e ) {
      wlog( "%1%", boost::diagnostic_information(e) );
    }
  }
  return true;
}

/**
 *  Returning false, is will send us back to uninit state
 */
bool connection::handle_auth_msg( const tornet::buffer& b ) {
    slog( "" );
    scrypt::signature_t  sig;
    scrypt::public_key_t pubk;
    uint64_t             utc_us;
    uint64_t             nonce[2];

    boost::rpc::datastream<const char*> ds( b.data(), b.size() );
    ds >> sig >> pubk >> utc_us >> nonce[0] >> nonce[1];

    scrypt::sha1_encoder  sha;
    sha.write( &m_dh->shared_key.front(), m_dh->shared_key.size() );
    sha << utc_us;

    if( !pubk.verify( sha.result(), sig ) ) {
      elog( "Invalid authentication" );
      send_auth_response(false);
      send_close();
      return false;
    } else {
      slog( "Authenticated!" );
      send_auth_response(true);

      scrypt::sha1_encoder pkds; pkds << pubk;
      set_remote_id( pkds.result() );

      m_node.update_dist_index( m_remote_id, this );
      // auth resp tru
      goto_state( connected );
    }
    return true;
}

void connection::set_remote_id( const connection::node_id& nid ) {
  m_remote_id = nid;
}

void connection::goto_state( state_enum s ) {
  if( m_cur_state != s )
    state_changed( m_cur_state = s );
}

void connection::generate_dh() {
  slog("");
  m_dh.reset( new scrypt::diffie_hellman() );
  static std::string decode_param = 
        boost::rpc::base64_decode( "lyIvBWa2SzbSeqb4HgBASJEj3SJrYFAIaErwx5GMt71CtFE4FYXDrVw1bPTBaRX4GTDAIBQM8Rs=" );
  m_dh->p.clear();
  m_dh->g = 5;
  m_dh->p.insert( m_dh->p.begin(), decode_param.begin(), decode_param.end() );
  m_dh->pub_key.reserve(63);
  m_dh->generate_pub_key();
}

void connection::send_dh() {
  slog("");
  BOOST_ASSERT( m_dh );
  int init_size = m_dh->pub_key.size();
  if( m_dh->pub_key.size() % 8 == 0 )
      m_dh->pub_key.resize(m_dh->pub_key.size()+ rand()%7+1 );
  m_node.send( &m_dh->pub_key.front(), m_dh->pub_key.size(), m_remote_ep );
  m_dh->pub_key.resize(init_size);
}

/**
 *  Creates a node authentication message.
 */
void connection::send_auth() {
    slog("");
    BOOST_ASSERT( m_dh );
    BOOST_ASSERT( m_dh->shared_key.size() == 56 );
    uint64_t utc_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

    scrypt::sha1_encoder sha;
    sha.write( &m_dh->shared_key.front(), m_dh->shared_key.size() );
    sha << utc_us;

    scrypt::signature_t s;
    m_node.sign( sha.result(), s );

    char buf[sizeof(s)+sizeof(m_node.pub_key())+sizeof(utc_us)+16];
    //BOOST_ASSERT( sizeof(tmp) == ps.tellp() );

    boost::rpc::datastream<char*> ds(buf,sizeof(buf));

    // allocate buffer with unused space in front for header info and back for padding, send() will then use
    // the extra space to put the encryption, pad, and type info.
    ds << s << m_node.pub_key() << utc_us << m_node.nonce()[0] << m_node.nonce()[1];
    send( buf, sizeof(buf), auth_msg );
}


  /**
   *  All data is sent via the m_node and is encrypted.  That means that 
   *  the ultimate buffer must be a multiple of 8 bytes long and that the decryption
   *  must be verified and the number of pad bytes must be known. 
   *
   *  The ultimate packet format is:
   *
   *  uint24_t  checksum; 
   *  uint5     flags;
   *  uint3     pad_bytes;
   *  data+pad
   *
   */
  void connection::send( const char* buf, uint32_t size, connection::proto_message_type t ) {
      BOOST_ASSERT( size <= 2044 );
      BOOST_ASSERT( m_bf );

      unsigned char  buffer[2048];
      unsigned char* data = buffer+4;
      uint8_t  pad = 8 - ((size + 4) % 8);

      memcpy( data,buf,size);
      if( pad )
        memset( data + size, 0, pad );
      uint32_t check = scrypt::super_fast_hash( (char*)data, size + pad );
      uint32_t size_pad = size+pad;
      memcpy( buffer, (char*)&check, 3 );
      pad |= uint8_t(t) << 3;
      buffer[3] = pad;
      //slog( "%1%", scrypt::to_hex( (char*)buffer, size_pad+4 ) );

      uint32_t buf_len = size_pad+4;
      m_bf->reset_chain();
      m_bf->encrypt(  buffer, buf_len, scrypt::blowfish::CBC );

      /// TODO: Throttle Connection if we are sending faster than connection priority allows 

      slog( "Sending %1% bytes", buf_len );
      m_node.send( (char*)buffer, buf_len, m_remote_ep );
  }

  bool connection::process_dh( const tornet::buffer& b ) {
    slog( "" );
    BOOST_ASSERT( b.size() >= 56 );
    m_dh->compute_shared_key( b.data(), 56 );
    while( m_dh->shared_key.size() < 56 ) 
      m_dh->shared_key.push_back('\0');
    wlog( "shared key: %1%", 
            boost::rpc::base64_encode( (const unsigned char*)&m_dh->shared_key.front(), m_dh->shared_key.size() ) ); 

    // make sure shared key make sense!

    m_bf.reset( new scrypt::blowfish() );
    m_bf->start( (unsigned char*)&m_dh->shared_key.front(), 56 );
    return true;
  }

  void connection::close_channel( const channel& c ) {
    wlog("not implemented");
  }

  connection::node_id connection::get_remote_id()const { return m_remote_id; }

  /**
   *  @todo - find a way to preallocate the header info and the just fill in
   *  before the start of buffer... 
   */
  void connection::send( const channel& c, const tornet::buffer& b  ) {
    if( &boost::cmt::thread::current() != &m_node.get_thread() )
      m_node.get_thread().async<void>( boost::bind( &connection::send, this, 
                                                    boost::ref(c), boost::ref(b) ) ).wait();
    else {
        wlog("send from %1% to %2%", c.local_channel_num(), c.remote_channel_num() );
        char buf[2048];
        boost::rpc::datastream<char*> ds(buf,sizeof(buf));
        ds << c.local_channel_num() << c.remote_channel_num(); 
        ds.write( b.data(), b.size() );
        send( buf, ds.tellp(), data_msg );
    }
  }

  /**
   *  This method attempts to advance toward connected by sending the
   *  appropriate message for the current state.  All connections
   *  after 10 calls, the state transitions to failed.
   */
  void connection::advance() {
    wlog( "advance!" );
    if( ++m_advance_count >= 10 ) {
      goto_state(failed);
      return;
    }
    switch( m_cur_state ) {
      case failed: return;
      case uninit:
        generate_dh();
        send_dh();
        goto_state( generated_dh );
        return;
      case generated_dh:
        send_dh();
        return;
      case received_dh:
        send_dh();
        send_auth();
        return;
      case authenticated:
        send_auth_response(true);
        return;
      case connected: 
        m_advance_count = 0;
        return;
    }
  }
  void connection::close() {
    if( m_cur_state >= received_dh )
        send_close();
    reset();
  }
  void connection::close_channels() {
    boost::unordered_map<uint32_t,channel>::iterator itr = m_channels.begin();
    tornet::buffer b; b.resize(0);
    while( itr != m_channels.end() ) {
      itr->second.reset();
      itr->second.recv( b, channel::closed );
      ++itr;
    }
    m_channels.clear();
  }

  void connection::add_channel( const channel& ch ) {
    uint32_t k = (uint32_t(ch.local_channel_num()) << 16) | ch.remote_channel_num();
    m_channels[k] = ch;
  }

} } // tornet::detail
