#include "WUserItemModel.hpp"
#include <fc/fwd_impl.hpp>
#include <fc/bigint.hpp>

class WUserItemModel::impl {
  public:
    
    tn::db::peer::ptr _peers;
};


WUserItemModel::WUserItemModel( const fc::shared_ptr<tn::db::peer>& pdb, WObject* parent)
:Wt::WAbstractTableModel(parent) {
  my->_peers = pdb;
}
WUserItemModel::~WUserItemModel() {}

int WUserItemModel::columnCount( const Wt::WModelIndex& parent  )const {
  return 8;
}
int WUserItemModel::rowCount( const Wt::WModelIndex& parent )const {
  if( parent != Wt::WModelIndex() ) return 0;
  int c = my->_peers->count();
  return c;
}
int calc_dist( const fc::sha1& d ) {
  fc::bigint bi(d.data(), sizeof(d));
  fc::sha1   mx; memset( mx.data(),0xff,sizeof(mx));
  fc::bigint max(mx.data(),sizeof(mx));

  return ((bi * fc::bigint( 1000 )) / max).to_int64();
}
boost::any WUserItemModel::headerData( int section, Wt::Orientation o, int role)const {
  if( role == Wt::DisplayRole && o == Wt::Horizontal ) {
    switch( section ) {
      case 0: return Wt::WString( "ID" );
      case 1: return Wt::WString( "IP:PORT" );
      case 2: return Wt::WString( "Access" );
      case 3: return Wt::WString( "Dist (0-1000)" );
      case 4: return Wt::WString( "Rank" );
      case 5: return Wt::WString( "RTT" );
      case 6: return Wt::WString( "Sent C" );
      case 7: return Wt::WString( "Recv C" );
    }

  }
  return Wt::WAbstractTableModel::headerData(section,o,role);
}

boost::any WUserItemModel::data( const Wt::WModelIndex& index, int role )const {
  //slog( "" );
  if( role == Wt::DisplayRole ) {
    tn::db::peer::record rec;
    fc::sha1 id;
    my->_peers->fetch_index( index.row()+1, id, rec );
    switch( index.column() ) {
      case 0:
        return Wt::WString( fc::string( id ).c_str() );
      case 1:
        return Wt::WString( fc::string( rec.last_ep ).c_str() );
      case 2:
        return Wt::WString( rec.firewalled ? "Firewalled" : "Public" );
      case 3:
        return Wt::WString( boost::lexical_cast<std::string>(calc_dist(id ^ my->_peers->get_local_id())));
      case 4:
        return Wt::WString( boost::lexical_cast<std::string>(int(rec.rank)) );
      case 5:
        return Wt::WString( boost::lexical_cast<std::string>(int(rec.avg_rtt_us)) );
      case 6:
        return Wt::WString( boost::lexical_cast<std::string>(int(rec.sent_credit)) );
      case 7:
        return Wt::WString( boost::lexical_cast<std::string>(int(rec.recv_credit)) );
      default:
        return boost::any();
    }
  }
  return boost::any();
}

Wt::WModelIndex WUserItemModel::index( int row, int col, const Wt::WModelIndex& p  )const {
  //slog( "" );
  return createIndex( row, col, (void*)0 ); 
}
