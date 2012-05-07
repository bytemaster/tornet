#include <tornet/net/detail/connection.hpp>
#include <boost/cmt/log/log.hpp>
#include <scrypt/super_fast_hash.hpp>
#include <scrypt/scrypt.hpp>
#include <scrypt/base64.hpp>
#include <scrypt/bigint.hpp>
#include <tornet/net/detail/node_private.hpp>
#include <tornet/rpc/datastream.hpp>
#include <boost/chrono.hpp>

namespace tornet { namespace detail {

using namespace boost::chrono;

connection::connection( detail::node_private& np, const endpoint& ep, const db::peer::ptr& pptr )
:m_node(np),m_remote_ep(ep), m_cur_state(uninit),m_advance_count(0),m_peers(pptr) {
  if( m_peers->fetch_by_endpoint( ep, m_remote_id, m_record )  ) {
    m_bf.reset( new scrypt::blowfish() );
    wlog( "Known peer at %1%:%2%start bf %3%",  ep.address().to_string(), ep.port(),
        scrypt::to_hex( m_record.bf_key, 56 ) );
    m_bf->start( (unsigned char*)m_record.bf_key, 56 );
    m_node.update_dist_index( m_remote_id, this );
    m_cur_state = connected;
  } else {
    wlog( "Unknown peer at %1%:%2%", ep.address().to_string(), ep.port() );
    m_record.last_ip   = m_remote_ep.address().to_v4().to_ulong();
    m_record.last_port = m_remote_ep.port();
  }
}

connection::connection( detail::node_private& np, const endpoint& ep, 
            const node_id& auth_id, state_enum init_state )
:m_node(np),m_advance_count(0) {
   m_remote_ep = ep;
   m_remote_id = auth_id;
   m_cur_state = init_state;
}

connection::~connection() {
  elog( "~connection" );
  if( m_peers && m_record.valid() ) {
    m_peers->store( m_remote_id, m_record );
  }
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
  if( m_peers->fetch_by_endpoint( m_remote_ep, m_remote_id, m_record )  ) {
    m_bf.reset( new scrypt::blowfish() );
    wlog( "Known peer at %1%:%2%start bf %3%",  m_remote_ep.address().to_string(), m_remote_ep.port(),
        scrypt::to_hex( m_record.bf_key, 56 ) );
    m_bf->start( (unsigned char*)m_record.bf_key, 56 );
    m_node.update_dist_index( m_remote_id, this );
    goto_state(connected);
    handle_connected( b );
    return;
  }

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
  BOOST_ASSERT( m_cur_state == connected );

  if( 0 == b.size() % 8 && decode_packet(b) ) 
    return; // sucess
  slog("failed to decode packet... " );

  if( m_peers && m_record.valid() ) { 
    wlog( "Peer at %1%:%2% sent invalid message, resetting IP/PORT on record and "
          "assuming new node at %1%:%2%", m_remote_ep.address().to_string(), m_record.last_port );
     m_record.last_ip   = 0;
     m_record.last_port = 0;
     memset( m_record.bf_key, 0, sizeof(m_record.bf_key) );
     m_peers->store( get_remote_id(), m_record );
  }
  m_record           = db::peer::record();
  m_record.last_ip   = m_remote_ep.address().to_v4().to_ulong();
  m_record.last_port = m_remote_ep.port();

  reset();
  handle_uninit(b);
}
bool connection::handle_auth_resp_msg( const tornet::buffer& b ) {
  wlog("auth response %1%", int(b[0]) );
  if( b[0] ) { 
    m_record.published_rank = uint8_t(b[0]);
    goto_state( connected ); 
    return true; 
  }
  reset();
  return false;
}

bool connection::handle_update_rank_msg( const tornet::buffer& b ) {
  if( b.size() < 2*sizeof(uint64_t) ) {
    elog( "update rank message is too small" );
    return false;
  }
  scrypt::sha1_encoder  rank_sha;
  rank_sha.write( (char*)b.data(), 2*sizeof(uint64_t) );
  rank_sha.write( m_record.public_key, sizeof(m_record.public_key) );
  scrypt::sha1 r = rank_sha.result();
  uint8_t new_rank = 161 - scrypt::bigint( (const char*)r.hash, sizeof(r.hash) ).log2();
  if( new_rank > m_record.rank ) {
    m_record.rank = new_rank;
    memcpy( (char*)m_record.nonce, b.data(), 2*sizeof(uint64_t) );
    send_auth_response(true);
    slog( "Received rank update for %1% to %2%", m_remote_id, int(m_record.rank) );
    m_peers->store( m_remote_id, m_record );
  }
  return true;
}

bool connection::handle_close_msg( const tornet::buffer& b ) {
  wlog("closed msg" );
  reset();
  return true;
}

void connection::reset() {
  if( m_peers && m_record.valid() ) {
    m_peers->store( m_remote_id, m_record );
  }
  close_channels();
  m_node.update_dist_index( m_remote_id, 0 );
  m_cached_objects.clear();
  goto_state(uninit); 
}

void connection::send_close() {
  slog( "sending close" );
  char resp = 1;
  send( &resp, sizeof(resp), close_msg );
}

/**
 *  Return the received rank
 */
void connection::send_auth_response(bool r ) {
  if( !r ) {
      char resp = r;
      send( &resp, sizeof(resp), auth_resp_msg );
  } else {
      send( (char*)&m_record.rank, sizeof(m_record.rank), auth_resp_msg );
  }
}

void connection::send_update_rank() {
  send( (char*)m_node.nonce(), 2*sizeof(uint64_t), update_rank );
}

bool connection::decode_packet( const tornet::buffer& b ) {
    m_record.last_contact = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

    m_bf->reset_chain();
    m_bf->decrypt( (unsigned char*)b.data(), b.size(), scrypt::blowfish::CBC );

    uint32_t checksum = scrypt::super_fast_hash( (char*)b.data()+4, b.size()-4 );
    if( memcmp( &checksum, b.data(), 3 ) != 0 ) {
      elog( "decrytpion checksum failed" );
      return false;
    }
    uint8_t pad = b[3] & 0x07;
    uint8_t msg_type = b[3] >> 3;

//    slog( "%1% bytes type %2%  pad %3%", b.size(), int(msg_type), int(pad) );

    switch( msg_type ) {
      case data_msg:         return handle_data_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_msg:         return handle_auth_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_resp_msg:    return handle_auth_resp_msg( b.subbuf(4,b.size()-4-pad) );  
      case route_lookup_msg: return handle_lookup_msg( b.subbuf(4,b.size()-4-pad) );
      case route_msg:        return handle_route_msg( b.subbuf(4,b.size()-4-pad) );
      case close_msg:        return handle_close_msg( b.subbuf(4,b.size()-4-pad) );  
      case update_rank:      return handle_update_rank_msg( b.subbuf(4,b.size()-4-pad) );  
      default:
        wlog( "Unknown message type" );
    }
    return true;
}

/// sha1(target_id) << uint32_t(num)  
bool connection::handle_lookup_msg( const tornet::buffer& b ) {
  node_id target; uint32_t num; uint8_t l=0; node_id limit;
  {
      tornet::rpc::datastream<const char*> ds(b.data(), b.size() );
      ds >> target >> num >> l;
      slog( "Lookup %1% near %2%  l: %3%", num, target, int(l) );
      if( l ) ds >> limit;
  }
  std::map<node_id, endpoint> r = m_node.find_nodes_near( target, num, limit );
  std::vector<char> rb; rb.resize(2048);

  num = r.size();
  
  tornet::rpc::datastream<char*> ds(rb.data(), rb.size() );
  std::map<node_id, endpoint>::iterator itr = r.begin();
  ds << target << num;
  for( uint32_t i = 0; i < num; ++i ) {
    slog( "Reply with %1% @ %2%:%3%", itr->first, itr->second.address().to_string(), itr->second.port() );
    ds << itr->first << uint32_t(itr->second.address().to_v4().to_ulong()) << uint16_t(itr->second.port());
    ++itr;
  }
  send( &rb.front(), ds.tellp(), route_msg );

  return true;
}
// uint16_t(num) << (sha1(distance) << uint32_t(ip) << uint16_t(port))*
bool connection::handle_route_msg( const tornet::buffer& b ) {
  node_id target; uint32_t num;
  tornet::rpc::datastream<const char*> ds(b.data(), b.size() );
  ds >> target >> num;
  // make sure we still have the target.... 

  std::map<node_id, boost::cmt::promise<route_table>::ptr >::iterator itr = route_lookups.find(target);
  if( itr == route_lookups.end() ) { 
    return true;
  }

  // unpack the routing table
  route_table rt;
  node_id dist; uint32_t ip; uint16_t port;
  for( uint32_t i = 0; i < num; ++i ) {
    ds >> dist >> ip >> port; 
    rt[dist] = endpoint( boost::asio::ip::address_v4( (unsigned long)(ip)  ), port );
    slog( "Response of dist: %1% @ %2%:%3%", dist, boost::asio::ip::address_v4((unsigned long)(ip)).to_string(), port );
  }
  itr->second->set_value(rt);
  route_lookups.erase(itr);
  return true;
}

bool connection::handle_data_msg( const tornet::buffer& b ) {
  if( m_cur_state != connected ) {
    wlog( "Data message received while not connected!" );
    close();
    return false;
  }
  uint16_t src, dst;
  tornet::rpc::datastream<const char*> ds(b.data(), b.size() );
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
    uint64_t             remote_nonce[2];

    tornet::rpc::datastream<const char*> ds( b.data(), b.size() );
    ds >> sig >> pubk >> utc_us >> remote_nonce[0] >> remote_nonce[1];


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

      scrypt::sha1_encoder pkds; pkds << pubk;
      set_remote_id( pkds.result() );
      
      m_peers->fetch( m_remote_id, m_record );
      memcpy( m_record.bf_key, &m_dh->shared_key.front(), 56 );

      tornet::rpc::datastream<char*> recds( m_record.public_key, sizeof(m_record.public_key) );
      recds << pubk;
      m_record.nonce[0] = remote_nonce[0];
      m_record.nonce[1] = remote_nonce[1];
      m_record.last_ip   = m_remote_ep.address().to_v4().to_ulong();
      m_record.last_port = m_remote_ep.port();

      uint64_t utc_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
      if( !m_record.first_contact ) 
        m_record.first_contact = utc_us;
      m_record.last_contact = utc_us;

      scrypt::sha1_encoder  rank_sha;
      rank_sha.write( (char*)remote_nonce, sizeof(remote_nonce) );
      rank_sha << pubk;
      scrypt::sha1 r = rank_sha.result();
      m_record.rank = 161 - scrypt::bigint( (const char*)r.hash, sizeof(r.hash) ).log2();

      send_auth_response(true);

      m_peers->store( m_remote_id, m_record );

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
  if( m_cur_state != s ) {
    slog( "goto %1% from %2%", int(s), int(m_cur_state) );
    state_changed( m_cur_state = s );
  }
}

void connection::generate_dh() {
  slog("");
  m_dh.reset( new scrypt::diffie_hellman() );
  static std::string decode_param = 
        scrypt::base64_decode( "lyIvBWa2SzbSeqb4HgBASJEj3SJrYFAIaErwx5GMt71CtFE4FYXDrVw1bPTBaRX4GTDAIBQM8Rs=" );
  m_dh->p.clear();
  m_dh->g = 5;
  m_dh->p.insert( m_dh->p.begin(), decode_param.begin(), decode_param.end() );
  m_dh->pub_key.reserve(63);
  do { m_dh->generate_pub_key(); } while ( m_dh->pub_key.size() != 56 );
}


void connection::send_dh() {
  slog("");
  BOOST_ASSERT( m_dh );
  int init_size = m_dh->pub_key.size();
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

    tornet::rpc::datastream<char*> ds(buf,sizeof(buf));

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
      if( pad == 8 ) pad = 0;

      memcpy( data,buf,size);
      if( pad )
        memset( data + size, 0, pad );
      uint32_t check = scrypt::super_fast_hash( (char*)data, size + pad );
      uint32_t size_pad = size+pad;
      memcpy( buffer, (char*)&check, 3 );
      buffer[3] = pad | (uint8_t(t)<<3);
      //slog( "%1%", scrypt::to_hex( (char*)buffer, size_pad+4 ) );

      uint32_t buf_len = size_pad+4;
      m_bf->reset_chain();
      m_bf->encrypt(  buffer, buf_len, scrypt::blowfish::CBC );

      /// TODO: Throttle Connection if we are sending faster than connection priority allows 

      //slog( "Sending %1% bytes: pad %2%  type: %3%", buf_len, int(pad), int(t) );
      m_node.send( (char*)buffer, buf_len, m_remote_ep );
  }

  bool connection::process_dh( const tornet::buffer& b ) {
    slog( "" );
    BOOST_ASSERT( b.size() >= 56 );
    m_dh->compute_shared_key( b.data(), 56 );
    while( m_dh->shared_key.size() < 56 ) 
      m_dh->shared_key.push_back('\0');
    wlog( "shared key: %1%", 
            scrypt::base64_encode( (const unsigned char*)&m_dh->shared_key.front(), m_dh->shared_key.size() ) ); 

    // make sure shared key make sense!

    m_bf.reset( new scrypt::blowfish() );
    m_bf->start( (unsigned char*)&m_dh->shared_key.front(), 56 );
    wlog( "start bf %1%", scrypt::to_hex( (char*)&m_dh->shared_key.front(), 56 ) );
    memcpy( m_record.bf_key, &m_dh->shared_key.front(), 56 );
    return true;
  }

  void connection::close_channel( const channel& c ) {
    wlog("not implemented");
  }

  connection::node_id connection::get_remote_id()const { return m_remote_id; }

  boost::cmt::thread& connection::get_thread()const {
    return m_node.get_thread();
  }

  /**
   *  @todo - find a way to preallocate the header info and the just fill in
   *  before the start of buffer... 
   */
  void connection::send( const channel& c, const tornet::buffer& b  ) {
    if( &boost::cmt::thread::current() != &m_node.get_thread() )
      m_node.get_thread().async<void>( boost::bind( &connection::send, this, 
                                                    boost::ref(c), boost::ref(b) ) ).wait();
    else {
        //wlog("send from %1% to %2%", c.local_channel_num(), c.remote_channel_num() );
        char buf[2048];
        tornet::rpc::datastream<char*> ds(buf,sizeof(buf));
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
    if( m_cur_state >= received_dh ) {
        send_close();
        // save....
    }
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

  channel connection::find_channel( uint16_t remote_channel_num )const {
      boost::unordered_map<uint32_t,channel>::const_iterator itr= m_channels.begin();
      while( itr != m_channels.end() ) {
        if( itr->first & 0xffff == remote_channel_num )
          return itr->second;
        ++itr;
      }
      return channel();
  }
  void connection::add_channel( const channel& ch ) {
    uint32_t k = (uint32_t(ch.local_channel_num()) << 16) | ch.remote_channel_num();
    m_channels[k] = ch;
  }

  connection::endpoint connection::get_endpoint()const { return m_remote_ep; }

  /**
   *  Sends a message requesting a node lookup and waits up to 1s for a response.  If no
   *  response in 1 second, then a timeout exception is thrown.  
   */
  std::map<node::id_type, node::endpoint> connection::find_nodes_near( const node::id_type& target, 
                                                                       uint32_t n, const boost::optional<node::id_type>& limit  ) {
    try {                                                                        
      if( m_record.published_rank < m_node.rank() )
        send_update_rank();

      boost::cmt::promise<route_table>::ptr prom( new boost::cmt::promise<route_table>() );
      using namespace boost::chrono;
      uint64_t start_time_utc = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

      route_lookups[ target ] = prom;

      std::vector<char> buf( !!limit ? 45 : 25 ); 
      tornet::rpc::datastream<char*> ds(&buf.front(), buf.size());
      ds << target << n << uint8_t(!!limit);
      if( !!limit ) { ds << *limit; }
      send( &buf.front(), buf.size(), route_lookup_msg );

      wlog( "waiting on remote response!\n");
      const route_table& rt = prom->wait( boost::chrono::seconds(5) );

      uint64_t end_time_utc  = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
      elog( "%1% - %2% = %3%", end_time_utc, start_time_utc, (end_time_utc-start_time_utc) );
      m_record.avg_rtt_us =  (m_record.avg_rtt_us*1 + (end_time_utc-start_time_utc)) / 2;

      assert( route_lookups.end() == route_lookups.find(target) );
      return rt;
    } catch ( const boost::exception& e ) {
      elog( "%1%", boost::diagnostic_information(e) );
      std::map<node_id, boost::cmt::promise<route_table>::ptr >::iterator ritr = route_lookups.find(target);
      if( ritr != route_lookups.end() )
          route_lookups.erase( ritr );
      throw;
    }
  }

  void       connection::cache_object( const std::string& key, const boost::any& v ) {
    m_cached_objects[key] = v;
  }
  boost::any connection::get_cached_object( const std::string& key )const {
    boost::unordered_map<std::string,boost::any>::const_iterator itr = m_cached_objects.find(key);
    if( itr == m_cached_objects.end() ) return boost::any();
    return itr->second;
  }

} } // tornet::detail
