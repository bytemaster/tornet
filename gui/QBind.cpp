#include "QBind.hpp"
#include <boost/scoped_ptr.hpp>
#include <QEvent>
#include <QCoreApplication>

class QBindDelegate : public QEvent {
  public:
    QBindDelegate( const boost::function<void()>& f )
    :QEvent( (QEvent::Type)event_type() ),run(f){}
    static int event_type();
  protected:
    friend class QBind;
    boost::function<void()> run;
};

QBind* QBind::instance() {
  static boost::scoped_ptr<QBind> obj(new QBind());
  return obj.get();
}
void QBind::customEvent( QEvent* e ) {
  QBindDelegate* bd = dynamic_cast<QBindDelegate*>(e);
  if( bd ) { bd->run(); }
}

int QBindDelegate::event_type() {
    static int et = QEvent::registerEventType(); 
    return et;
}
void QBind::post( const boost::function<void()>& f ) {
  QCoreApplication::postEvent( instance(), new QBindDelegate( f ) );
}
