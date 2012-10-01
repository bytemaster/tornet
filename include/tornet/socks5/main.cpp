#include <tornet/socks5/proxy.hpp>

int main( int argc, char** argv ) {

  proxy p( 1090 );
  p.listen();
  return 0;
}
