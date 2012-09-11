#include <tornet/connection.hpp>
#include <tornet/node.hpp>

#include <fc/log.hpp>
#include <fc/datastream.hpp>
#include <fc/bigint.hpp>
#include <fc/future.hpp>
#include <fc/pke.hpp>
#include <fc/base64.hpp>
#include <fc/hex.hpp>
#include <fc/thread.hpp>
#include <fc/sha1.hpp>
#include <fc/buffer.hpp>

#include <fc/blowfish.hpp>
#include <fc/dh.hpp>
#include <fc/super_fast_hash.hpp>

#include <boost/scoped_ptr.hpp>
#include <map>
#include <boost/unordered_map.hpp>

#include <fc/fwd_impl.hpp>
#include "node_impl.hpp"

namespace tn { 

  typedef fc::vector<host> route_table;
  class connection::impl {
    public:
        impl( node& n ):_node(n),_behind_nat(false){}

        uint16_t                                              _advance_count;
        node&                                                 _node;
                                                              
        boost::scoped_ptr<fc::diffie_hellman>             _dh;
        boost::scoped_ptr<fc::blowfish>                   _bf;
        state_enum                                            _cur_state;
                                                              
        node_id                                               _remote_id;
        fc::ip::endpoint                                      _remote_ep;
        fc::ip::endpoint                                      _public_ep; // endpoint remote host reported seeing from us
        bool                                                  _behind_nat;


        std::map<node_id,fc::promise<route_table>::ptr>       _route_lookups;
            
        
        boost::unordered_map<std::string,fc::any>             _cached_objects;
        boost::unordered_map<uint32_t,channel>                _channels;
        db::peer::ptr                                         _peers;
        db::peer::record                                      _record;
  };
const db::peer::record&  connection::get_db_record()const { return my->_record; }

connection::connection( node& np, const fc::ip::endpoint& ep, const db::peer::ptr& pptr )
:my(np) {
  my->_remote_ep = ep;
  my->_cur_state = uninit;
  my->_advance_count = 0;
  my->_peers = pptr;

  if( my->_peers->fetch_by_endpoint( ep, my->_remote_id, my->_record )  ) {
    my->_bf.reset( new fc::blowfish() );
    wlog( "Known peer at %s:%d start bf %s",  fc::string(ep.get_address()).c_str(), ep.port(),
        fc::to_hex( my->_record.bf_key, 56 ).c_str() );
    my->_bf->start( (unsigned char*)my->_record.bf_key, 56 );
    my->_node.update_dist_index( my->_remote_id, this );
    my->_cur_state = connected;
  } else {
    wlog( "Unknown peer at %s:%d", fc::string(ep.get_address()).c_str(), ep.port() );
    my->_record.last_ip   = my->_remote_ep.get_address();
    my->_record.last_port = my->_remote_ep.port();
  }
}

connection::connection( node& np, const fc::ip::endpoint& ep, const node_id& auth_id, state_enum init_state )
:my(np) {
   my->_remote_ep = ep;
   my->_remote_id = auth_id;
   my->_cur_state = init_state;
   my->_advance_count = 0;
   my->_peers = np.get_peers();
}

connection::~connection() {
  elog( "~connection" );
  if( my->_peers && my->_record.valid() ) {
    my->_peers->store( my->_remote_id, my->_record );
  }
}


/**
 *  Start of state machine, switch to the proper handler for the
 *  current state.
 */
void connection::handle_packet( const tn::buffer& b ) {
  switch( my->_cur_state ) {
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
void connection::handle_uninit( const tn::buffer& b ) {
  slog("");
  if( my->_peers->fetch_by_endpoint( my->_remote_ep, my->_remote_id, my->_record )  ) {
    my->_bf.reset( new fc::blowfish() );
    wlog( "Known peer at %s:%d start bf %s",  fc::string(my->_remote_ep.get_address()).c_str(), my->_remote_ep.port(),
        fc::to_hex( my->_record.bf_key, 56 ).c_str() );
    my->_bf->start( (unsigned char*)my->_record.bf_key, 56 );
    my->_node.update_dist_index( my->_remote_id, this );
    goto_state(connected);
    handle_connected( b );
    return;
  }

  BOOST_ASSERT( my->_cur_state == uninit );
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
void connection::handle_generated_dh( const tn::buffer& b ) {
  slog("");
  BOOST_ASSERT( my->_cur_state == generated_dh );
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

void connection::handle_received_dh( const tn::buffer& b ) {
  slog("");
  BOOST_ASSERT( my->_cur_state == received_dh );
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
    if( !decode_packet( b ) ) {
      wlog( "Reset connection to uninit" );
      goto_state(uninit);
      handle_uninit(b);
    }
    return;
  }
  wlog( "ignoring malformed packet" );
}

void connection::handle_authenticated( const tn::buffer& b ) {
  slog("");
  BOOST_ASSERT( my->_cur_state == authenticated );
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
void connection::handle_connected( const tn::buffer& b ) {
  BOOST_ASSERT( my->_cur_state == connected );

  if( 0 == b.size() % 8 && decode_packet(b) ) 
    return; // sucess
  slog("failed to decode packet... " );

  if( my->_peers && my->_record.valid() ) { 
    wlog( "Peer at %s:%d sent invalid message, resetting IP/PORT on record and assuming new node at %s:%d", 
          fc::string(my->_remote_ep.get_address()).c_str(), my->_record.last_port,
          fc::string(my->_remote_ep.get_address()).c_str(), my->_record.last_port );

     my->_record.last_ip   = 0;
     my->_record.last_port = 0;
     memset( my->_record.bf_key, 0, sizeof(my->_record.bf_key) );
     my->_peers->store( get_remote_id(), my->_record );
  }
  my->_record           = db::peer::record();
  my->_record.last_ip   = my->_remote_ep.get_address();
  my->_record.last_port = my->_remote_ep.port();

  reset();
  handle_uninit(b);
}
bool connection::handle_auth_resp_msg( const tn::buffer& b ) {
  wlog("auth response %d", int(b[0]) );
  if( b[0] ) { 
    slog( "rank %d", uint16_t(b[0]) );
    my->_record.published_rank = uint8_t(b[0]);
    goto_state( connected ); 
    return true; 
  }
  reset();
  return false;
}

bool connection::handle_update_rank_msg( const tn::buffer& b ) {
  if( b.size() < 2*sizeof(uint64_t) ) {
    elog( "update rank message is too small" );
    return false;
  }
  fc::sha1::encoder  rank_sha;
  rank_sha.write( (char*)b.data(), 2*sizeof(uint64_t) );
  rank_sha.write( my->_record.public_key, sizeof(my->_record.public_key) );
  fc::sha1 r = rank_sha.result();
  uint8_t new_rank = 161 - fc::bigint( (const char*)r.data(), sizeof(r) ).log2();
  if( new_rank > my->_record.rank ) {
    my->_record.rank = new_rank;
    memcpy( (char*)my->_record.nonce, b.data(), 2*sizeof(uint64_t) );
    send_auth_response(true);
    slog( "Received rank update for %s to %d", fc::string(my->_remote_id).c_str(), int(my->_record.rank) );
    my->_peers->store( my->_remote_id, my->_record );
  }
  return true;
}

bool connection::handle_close_msg( const tn::buffer& b ) {
  wlog("closed msg" );
  reset();
  return true;
}

void connection::reset() {
  wlog( "?" );
  if( my->_peers && my->_record.valid() ) {
    my->_peers->store( my->_remote_id, my->_record );
  }
  close_channels();
  my->_node.update_dist_index( my->_remote_id, 0 );
  my->_cached_objects.clear();
  goto_state(uninit); 
}

void connection::send_close() {
  slog( "sending close" );
  char resp = 1;
  send( &resp, sizeof(resp), close_msg );
}
void connection::send_punch() {
  slog( "sending punch to %s", fc::string(get_endpoint()).c_str() );
  char resp = 1;
  my->_node.send( &resp, 1, my->_remote_ep );
}


/**
 *    First, local host should send a punch_through to ep.
 *    Then, ask this connection to forward a 'reverse connect' request
 *        to ep.
 *
 *    The above two steps should repeat every .25 seconds up to 3 times
 *    before concluding that a connection could not be established.
 *
 *
 *
 *    Put the connection in 'waiting for punch through state' so that
 *
 *
 *    Send request_nat_punch_through_msg to remote ep.
 *    The remote ep will then send a nat_punch_through_msg to ep
 *    The remote ep will then send a request_nat_punch_through_ack_msg back to us.
 *
 *    This call will block for up to 1 second or until it receives the ack msg.
 *    The request will be sent every .25 seconds until 1 second has elapsed in case
 *    a packet was dropped.
 */
void connection::request_reverse_connect( const fc::ip::endpoint& ep ) {
  slog( "send request reverse connection %s", fc::string(ep).c_str());
  char rnpt[6]; 
  fc::datastream<char*> ds(rnpt,sizeof(rnpt));
  ds << uint32_t(ep.get_address()) << ep.port();
  send( rnpt, sizeof(rnpt), req_reverse_connect_msg );
}
void connection::send_request_connect( const fc::ip::endpoint& ep ) {
  slog( "send request connection %s", fc::string(ep).c_str());
  char rnpt[6]; 
  fc::datastream<char*> ds(rnpt,sizeof(rnpt));
  ds << uint32_t(ep.get_address()) << ep.port();
  send( rnpt, sizeof(rnpt), req_connect_msg );
}

/**
 *   Connect to given IP and PORT (should already be connected)  and request that they connect to sender of this
 *   request.
 */
bool connection::handle_request_reverse_connect_msg( const tn::buffer& b ) {
   fc::datastream<const char*> ds(b.data(), b.size() );
   uint32_t ip; uint16_t port;
   ds >> ip >> port;
   fc::ip::endpoint ep( ip, port );
   wlog( "handle reverse connect msg %s", fc::string(ep).c_str() );
   
   my->_node.get_thread().async( [=]() { 
        auto ep_con = my->_node.my->_ep_to_con.find(ep);
        if( ep_con != my->_node.my->_ep_to_con.end() ) { 
          ep_con->second->send_request_connect( get_endpoint() );
        }
     } 
   );
   return true;
}

/**
 *   Connect to given IP and PORT (should already be connected)  and request that they connect to sender of this
 *   request.
 */
bool connection::handle_request_connect_msg( const tn::buffer& b ) {
   fc::datastream<const char*> ds(b.data(), b.size() );
   uint32_t ip; uint16_t port;
   ds >> ip >> port;
   fc::ip::endpoint ep( ip, port );
   wlog( "handle reverse connect msg %s", fc::string(ep).c_str() );
   
   my->_node.get_thread().async( [=]() { 
        my->_node.connect_to( ep );
     } 
   );
   return true;
}



/**
 *  Return the received rank
 */
void connection::send_auth_response(bool r ) {
  if( !r ) {
      char resp = r;
      send( &resp, sizeof(resp), auth_resp_msg );
  } else {
      send( (char*)&my->_record.rank, sizeof(my->_record.rank), auth_resp_msg );
  }
}

void connection::send_update_rank() {
  send( (char*)my->_node.nonce(), 2*sizeof(uint64_t), update_rank );
}

bool connection::decode_packet( const tn::buffer& b ) {
    my->_record.last_contact = fc::time_point::now().time_since_epoch().count();

    my->_bf->reset_chain();
    my->_bf->decrypt( (unsigned char*)b.data(), b.size(), fc::blowfish::CBC );

    uint32_t checksum = fc::super_fast_hash( (char*)b.data()+4, b.size()-4 );
    if( memcmp( &checksum, b.data(), 3 ) != 0 ) {
      elog( "decrytpion checksum failed" );
      return false;
    }
    uint8_t pad = b[3] & 0x07;
    uint8_t msg_type = b[3] >> 3;

//    slog( "%1% bytes type %2%  pad %3%", b.size(), int(msg_type), int(pad) );

    switch( msg_type ) {
      case data_msg:                    return handle_data_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_msg:                    return handle_auth_msg( b.subbuf(4,b.size()-4-pad) );  
      case auth_resp_msg:               return handle_auth_resp_msg( b.subbuf(4,b.size()-4-pad) );  
      case route_lookup_msg:            return handle_lookup_msg( b.subbuf(4,b.size()-4-pad) );
      case route_msg:                   return handle_route_msg( b.subbuf(4,b.size()-4-pad) );
      case close_msg:                   return handle_close_msg( b.subbuf(4,b.size()-4-pad) );  
      case update_rank:                 return handle_update_rank_msg( b.subbuf(4,b.size()-4-pad) );  
      case req_reverse_connect_msg:     return handle_request_reverse_connect_msg( b.subbuf(4,b.size()-4-pad) );  
      case req_connect_msg:             return handle_request_connect_msg( b.subbuf(4,b.size()-4-pad) );  
      default:
        wlog( "Unknown message type" );
    }
    return true;
}

/// Message In:     sha1(target_id) << uint32_t(num)  
/// Message Out:    sha1(target_id) << uint32_t(num) << *(dist << ip << port << uin8_t(is_nat) )
bool connection::handle_lookup_msg( const tn::buffer& b ) {
  node_id target; uint32_t num; uint8_t l=0; node_id limit;
  {
      fc::datastream<const char*> ds(b.data(), b.size() );
      ds >> target >> num >> l;
      if(l) ds >> limit; 
    //  slog( "Lookup %1% near %2%  l: %3%", num, target, int(l) );
    //  if( l ) ds >> limit;
  }
  fc::vector<host> r = my->_node.find_nodes_near( target, num, limit );

  // TODO: put this on the stack to eliminate heap alloc
  fc::vector<char> rb; rb.resize(2048);

  num = r.size();
  
  fc::datastream<char*> ds(rb.data(), rb.size() );
  auto itr = r.begin();
  ds.write(target.data(),sizeof(target));
  ds << num;

  for( uint32_t i = 0; i < num; ++i ) {
    slog( "Reply with %s @ %s:%d", fc::string(itr->id).c_str(), fc::string(itr->ep.get_address()).c_str(), itr->ep.port() );
    ds << itr->id << uint32_t(itr->ep.get_address()) << uint16_t(itr->ep.port());
    ds << uint8_t( itr->nat_hosts.size() );
    ++itr;
  }
  send( &rb.front(), ds.tellp(), route_msg );

  return true;
}


/// Message IN:    sha1(target_id) << uint32_t(num) << *(dist << ip << port << uin8_t(is_nat) )
bool connection::handle_route_msg( const tn::buffer& b ) {
  node_id target; uint32_t num;
  fc::datastream<const char*> ds(b.data(), b.size() );
  ds >> target >> num;
  // make sure we still have the target.... 

  auto itr = my->_route_lookups.find(target);
  if( itr == my->_route_lookups.end() ) { 
    return true;
  }

  // unpack the routing table
  route_table rt;
  rt.reserve(num);
  node_id dist; uint32_t ip; uint16_t port;
  uint8_t is_nat = 0;
  for( uint32_t i = 0; i < num; ++i ) {
    ds >> dist >> ip >> port >> is_nat; 
    rt.push_back( host( dist, fc::ip::endpoint( ip, port ) ) );
    if( is_nat ) rt.back().nat_hosts.push_back( my->_remote_ep );
    slog( "Response of dist: %s @ %s:%d", fc::string(dist).c_str(), fc::string(fc::ip::address(ip)).c_str(), port );
  }
  slog( "set_value..." );
  itr->second->set_value(rt);
  my->_route_lookups.erase(itr);
  return true;
}

bool connection::handle_data_msg( const tn::buffer& b ) {
  if( my->_cur_state != connected ) {
    wlog( "Data message received while not connected!" );
    close();
    return false;
  }
  uint16_t src, dst;
  fc::datastream<const char*> ds(b.data(), b.size() );
  ds >> src >> dst;

  // locally channels are indexed by 'local' << 'remote'
  uint32_t k = (uint32_t(dst) << 16) | src;

  
  boost::unordered_map<uint32_t,channel>::iterator itr= my->_channels.find(k);
  if( itr != my->_channels.end() ) {
    itr->second.recv( b.subbuf(4) );
  } else {
 //   slog( "src %1% dst %2%", src, dst );
    try {
        // TODO:  verify this   channel ch = my->_node.create_channel( shared_from_this(), src, dst );
        channel ch = my->_node.create_channel( this, src, dst );
        my->_channels[k] = ch;
        ch.recv( b.subbuf(4) );
    } catch( ... ) {
      wlog( "%s", fc::current_exception().diagnostic_information().c_str() );
    }
  }
  return true;
}

/**
 *  Returning false, it will send us back to uninit state
 *
 *  sig pub_key utc nonce[2] ip port
 */
bool connection::handle_auth_msg( const tn::buffer& b ) {
    slog( "" );
    fc::signature_t  sig;
    fc::public_key_t pubk;
    uint64_t             utc_us;
    uint64_t             remote_nonce[2];

    fc::datastream<const char*> ds( b.data(), b.size() );
    ds >> sig >> pubk >> utc_us >> remote_nonce[0] >> remote_nonce[1];
    uint32_t rip;
    uint16_t rport;
    ds >> rip >> rport;
    my->_public_ep = fc::ip::endpoint( fc::ip::address(rip), rport );


    fc::sha1::encoder  sha;
    sha.write( &my->_dh->shared_key.front(), my->_dh->shared_key.size() );
    sha.write( (char*)&utc_us, sizeof(utc_us) );
    

    if( !pubk.verify( sha.result(), sig ) ) {
      elog( "Invalid authentication" );
      send_auth_response(false);
      send_close();
      return false;
    } else {
      if( my->_public_ep.get_address() != my->_remote_ep.get_address() ) {
        wlog( "Behind NAT  remote side reported %s but we received %s",
              fc::string(my->_public_ep).c_str(),
              fc::string(my->_remote_ep).c_str()
            );
        if( rport != my->_remote_ep.port() ) {
          wlog( "Warning: NAT Port Rewrite!!" );
        }
        my->_behind_nat = true;
      } else {
        slog( "Public IP" );
        my->_behind_nat = false;
      }
      

      slog( "Authenticated!" );

      fc::sha1::encoder pkds; pkds << pubk;
      set_remote_id( pkds.result() );
      my->_peers->fetch( my->_remote_id, my->_record );
      memcpy( my->_record.bf_key, &my->_dh->shared_key.front(), 56 );

      fc::datastream<char*> recds( my->_record.public_key, sizeof(my->_record.public_key) );
      recds << pubk;
      my->_record.nonce[0] = remote_nonce[0];
      my->_record.nonce[1] = remote_nonce[1];
      my->_record.last_ip   = my->_remote_ep.get_address();
      my->_record.last_port = my->_remote_ep.port();

      uint64_t utc_us = fc::time_point::now().time_since_epoch().count();
      if( !my->_record.first_contact ) 
        my->_record.first_contact = utc_us;
      my->_record.last_contact = utc_us;

      fc::sha1::encoder  rank_sha;
      rank_sha.write( (char*)remote_nonce, sizeof(remote_nonce) );
      rank_sha << pubk;
      fc::sha1 r = rank_sha.result();
      my->_record.rank = 161 - fc::bigint( r.data(), sizeof(r) ).log2();

      send_auth_response(true);
      my->_peers->store( my->_remote_id, my->_record );

      my->_node.update_dist_index( my->_remote_id, this );

      // auth resp tru
      goto_state( connected );
    }
    return true;
}

void connection::set_remote_id( const connection::node_id& nid ) {
  my->_remote_id = nid;
}

void connection::goto_state( state_enum s ) {
  if( my->_cur_state != s ) {
    state_changed( my->_cur_state = s );
  }
}

void connection::generate_dh() {
  try {
      my->_dh.reset( new fc::diffie_hellman() );
      static std::string decode_param = 
            fc::base64_decode( "lyIvBWa2SzbSeqb4HgBASJEj3SJrYFAIaErwx5GMt71CtFE4FYXDrVw1bPTBaRX4GTDAIBQM8Rs=" );
      my->_dh->p.clear();
      my->_dh->g = 5;
      my->_dh->p.insert( my->_dh->p.begin(), decode_param.begin(), decode_param.end() );
      my->_dh->pub_key.reserve(63);
      do {
        my->_dh->generate_pub_key(); 
      } while ( my->_dh->pub_key.size() != 56 );
  } catch ( ... ) {
    elog( "%s", fc::current_exception().diagnostic_information().c_str() );
    throw;
  }
}


void connection::send_dh() {
  slog("");
  BOOST_ASSERT( my->_dh );
  int init_size = my->_dh->pub_key.size();
  my->_dh->pub_key.resize(my->_dh->pub_key.size()+ rand()%7+1 );
  my->_node.send( &my->_dh->pub_key.front(), my->_dh->pub_key.size(), my->_remote_ep );
  my->_dh->pub_key.resize(init_size);
}

/**
 *  Creates a node authentication message. This message is used to prove that the
 *  node owns the public_key it claims.  It also is used to report the IP:PORT 
 *  
 *  sign( sha1(shared_key + utc) ) + pub_key + utc + nonce[2] + uint32_t(local_ip) + uint16_t(local_port)
 *
 */
void connection::send_auth() {
    slog("");
    BOOST_ASSERT( my->_dh );
    BOOST_ASSERT( my->_dh->shared_key.size() == 56 );
    uint64_t utc_us = fc::time_point::now().time_since_epoch().count();

    fc::sha1::encoder sha;
    sha.write( &my->_dh->shared_key.front(), my->_dh->shared_key.size() );
    sha.write( (char*)&utc_us, sizeof(utc_us) );

    fc::signature_t s = my->_node.sign( sha.result() );

    char buf[sizeof(s)+sizeof(my->_node.pub_key())+sizeof(utc_us)+16 + 6];
    //BOOST_ASSERT( sizeof(tmp) == ps.tellp() );

    fc::datastream<char*> ds(buf,sizeof(buf));

    // allocate buffer with unused space in front for header info and back for padding, send() will then use
    // the extra space to put the encryption, pad, and type info.
    ds << s << my->_node.pub_key() << utc_us << my->_node.nonce()[0] << my->_node.nonce()[1];
    ds << uint32_t(my->_node.local_endpoint(my->_remote_ep).get_address()) << my->_node.local_endpoint().port();
    send( buf, sizeof(buf), auth_msg );
}


  /**
   *  All data is sent via the my->_node and is encrypted.  That means that 
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
      BOOST_ASSERT( my->_bf );

      unsigned char  buffer[2048];
      unsigned char* data = buffer+4;
      uint8_t  pad = 8 - ((size + 4) % 8);
      if( pad == 8 ) pad = 0;

      memcpy( data,buf,size);
      if( pad )
        memset( data + size, 0, pad );
      uint32_t check = fc::super_fast_hash( (char*)data, size + pad );
      uint32_t size_pad = size+pad;
      memcpy( buffer, (char*)&check, 3 );
      buffer[3] = pad | (uint8_t(t)<<3);
      //slog( "%1%", fc::to_hex( (char*)buffer, size_pad+4 ) );

      uint32_t buf_len = size_pad+4;
      my->_bf->reset_chain();
      my->_bf->encrypt(  buffer, buf_len, fc::blowfish::CBC );

      /// TODO: Throttle Connection if we are sending faster than connection priority allows 

      //slog( "Sending %1% bytes: pad %2%  type: %3%", buf_len, int(pad), int(t) );
      my->_node.send( (char*)buffer, buf_len, my->_remote_ep );
  }

  bool connection::process_dh( const tn::buffer& b ) {
    slog( "" );
    BOOST_ASSERT( b.size() >= 56 );
    my->_dh->compute_shared_key( b.data(), 56 );
    while( my->_dh->shared_key.size() < 56 ) 
      my->_dh->shared_key.push_back('\0');
    //wlog( "shared key: %s", fc::base64_encode( (const unsigned char*)&my->_dh->shared_key.front(), my->_dh->shared_key.size() ).c_str() ); 

    // make sure shared key make sense!

    my->_bf.reset( new fc::blowfish() );
    my->_bf->start( (unsigned char*)&my->_dh->shared_key.front(), 56 );
    //wlog( "start bf %s", fc::to_hex( (char*)&my->_dh->shared_key.front(), 56 ).c_str() );
    memcpy( my->_record.bf_key, &my->_dh->shared_key.front(), 56 );
    return true;
  }

  void connection::close_channel( const channel& c ) {
    wlog("not implemented");
  }

  connection::node_id connection::get_remote_id()const { return my->_remote_id; }

  node& connection::get_node()const {
    return my->_node;
  }

  /**
   *  @todo - find a way to preallocate the header info and the just fill in
   *  before the start of buffer... 
   */
  void connection::send( const channel& c, const tn::buffer& b  ) {
    if( !my->_node.get_thread().is_current() ) my->_node.get_thread().async( [=]() { send(c,b); } );
    else {
        //wlog("send from %1% to %2%", c.local_channel_num(), c.remote_channel_num() );
        char buf[2048];
        fc::datastream<char*> ds(buf,sizeof(buf));
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
    if( ++my->_advance_count >= 10 ) {
      goto_state(failed);
      return;
    }
    switch( my->_cur_state ) {
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
        my->_advance_count = 0;
        return;
    }
  }
  void connection::close() {
    if( my->_cur_state >= received_dh ) {
        send_close();
        // save....
    }
    reset();
  }
  void connection::close_channels() {
    boost::unordered_map<uint32_t,channel>::iterator itr = my->_channels.begin();
    tn::buffer b; b.resize(0);
    while( itr != my->_channels.end() ) {
      itr->second.reset();
      itr->second.recv( b, channel::closed );
      ++itr;
    }
    my->_channels.clear();
  }

  channel connection::find_channel( uint16_t remote_channel_num )const {
      boost::unordered_map<uint32_t,channel>::const_iterator itr= my->_channels.begin();
      while( itr != my->_channels.end() ) {
        if( (itr->first & 0xffff) == remote_channel_num )
          return itr->second;
        ++itr;
      }
      return channel();
  }
  void connection::add_channel( const channel& ch ) {
    uint32_t k = (uint32_t(ch.local_channel_num()) << 16) | ch.remote_channel_num();
    my->_channels[k] = ch;
  }

  fc::ip::endpoint connection::get_endpoint()const { return my->_remote_ep; }

  bool connection::is_behind_nat()const {
    slog( "%p", this );
    return my->_behind_nat;
  }

  /**
   *  Sends a message requesting a node lookup and waits up to 1s for a response.  If no
   *  response in 1 second, then a timeout exception is thrown.  
   */
  fc::vector<tn::host> connection::find_nodes_near( const node::id_type& target, uint32_t n, const fc::optional<node::id_type>& limit  ) {
    try {                                                                        
      if( my->_record.published_rank < my->_node.rank() )
        send_update_rank();

      fc::promise<route_table>::ptr prom( new fc::promise<route_table>() );
      uint64_t start_time_utc = fc::time_point::now().time_since_epoch().count();

      my->_route_lookups[ target ] = prom;

      fc::vector<char> buf( !!limit ? 45 : 25 ); 
      fc::datastream<char*> ds(&buf.front(), buf.size());
      ds << target << n << uint8_t(!!limit);
      if( !!limit ) { ds << *limit; }
      send( &buf.front(), buf.size(), route_lookup_msg );

      wlog( "waiting on remote response!\n");
      const route_table& rt = prom->wait( fc::seconds(5) );

      uint64_t end_time_utc  = fc::time_point::now().time_since_epoch().count(); 
      elog( "latency: %lld - %lld = %lld us", end_time_utc, start_time_utc, (end_time_utc-start_time_utc) );
      my->_record.avg_rtt_us =  (my->_record.avg_rtt_us*1 + (end_time_utc-start_time_utc)) / 2;

      assert( my->_route_lookups.end() == my->_route_lookups.find(target) );

      return rt;
    } catch( ... ) {
      elog( "%s", fc::current_exception().diagnostic_information().c_str() );
      auto ritr = my->_route_lookups.find(target);
      if( ritr != my->_route_lookups.end() )
          my->_route_lookups.erase( ritr );
      throw;
    }
  }
  uint8_t                connection::get_remote_rank()const { return my->_record.rank; }
  connection::state_enum connection::get_state()const { return my->_cur_state; }

  /*
  void       connection::cache_object( const std::string& key, const boost::any& v ) {
    my->_cached_objects[key] = v;
  }
  boost::any connection::get_cached_object( const std::string& key )const {
    boost::unordered_map<std::string,boost::any>::const_iterator itr = my->_cached_objects.find(key);
    if( itr == my->_cached_objects.end() ) return boost::any();
    return itr->second;
  }
  */

}  // tn
