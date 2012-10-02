#ifndef _WUSERITEMMODEL_HPP
#define _WUSERITEMMODEL_HPP
#include <Wt/WAbstractTableModel>
#include <tornet/db/peer.hpp>
#include <fc/fwd.hpp>

class WUserItemModel : public Wt::WAbstractTableModel {
  public:
    WUserItemModel( const fc::shared_ptr<tn::db::peer>& pdb, WObject* parent = 0 );
    ~WUserItemModel();
    
   int columnCount( const Wt::WModelIndex& parent = Wt::WModelIndex() )const; 
   int rowCount( const Wt::WModelIndex& parent = Wt::WModelIndex() )const; 

    boost::any headerData( int section, Wt::Orientation o, int role)const;
    boost::any data( const Wt::WModelIndex& index, int role )const;
    Wt::WModelIndex index( int row, int col, const Wt::WModelIndex& p = Wt::WModelIndex() )const;

  private:
    class impl;
    fc::fwd<impl,8> my;
};


#endif // _WUSERITEMMODEL_HPP

