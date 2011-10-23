#ifndef _QBIND_HPP_
#define _QBIND_HPP_
#include <QObject>
#include <boost/function.hpp>

class QBind : public QObject {
  Q_OBJECT
  public:
    // posts f to run in QCoreApplication's main thread.
    static void post( const boost::function<void()>& f );

  private:
    static QBind* instance();
    void customEvent( QEvent* evt ); 
};

#endif
