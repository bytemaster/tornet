FIND_PACKAGE( Wt REQUIRED )
INCLUDE_DIRECTORIES( ${Wt_INCLUDE_DIR} )
find_package( Qt4 REQUIRED )
add_definitions( -DQT_NO_KEYWORDS )

#find_package( Protobuf REQUIRED )


include_directories( ${QT_QTCORE_INCLUDE_DIR} )
include_directories( ${QT_QTGUI_INCLUDE_DIR} )
include_directories( ../cmt/include )
include_directories( ../reflect/include )
#include_directories( ../rpc/include )
include_directories( ../context/include )
include_directories( ../atomic/include )
include_directories( ../ )
include_directories( /usr/include )
include_directories( /opt/local/include/db52 )

link_directories( /opt/local/lib/db52 )
set( BerkeleyDB_LIBRARY /opt/local/lib/db52/libdb_cxx.a )

SET( sources 
    main.cpp
    gui/QBind.cpp
    net/channel.cpp
    net/node.cpp
    net/kad.cpp
    net/udt_channel.cpp
    net/detail/miss_list.cpp
    net/detail/connection.cpp
    net/detail/node_private.cpp
    rpc/connection.cpp
    rpc/service.cpp
    rpc/udp_connection.cpp
    rpc/udt_connection.cpp
    services/accounting.cpp
    services/chat.cpp
    services/calc.cpp
    services/chunk_service.cpp
    services/chunk_session.cpp
    services/chunk_search.cpp
    db/peer.cpp
    db/chunk.cpp 
    db/publish.cpp 
   )

SET( moc_headers 
    gui/QBind.hpp
   )          


#QT4_WRAP_UI( sources gui/main.ui )
QT4_WRAP_CPP( sources ${moc_headers} )

SET( CXX_FLAGS -Wall )

SET( libraries 
    boost_cmt
    json
    boost_context 
    ltl
    scrypt
    ${BerkeleyDB_LIBRARY}
    ${Boost_THREAD_LIBRARY} 
    ${Boost_CHRONO_LIBRARY} 
    ${Boost_DATE_TIME_LIBRARY} 
    ${Boost_ASIO_LIBRARY} 
    ${Boost_SYSTEM_LIBRARY} 
    ${Boost_FILESYSTEM_LIBRARY} 
    ${Boost_PROGRAM_OPTIONS_LIBRARY} 
    ${Boost_SIGNALS_LIBRARY}
    ${QT_QTCORE_LIBRARY}
    ${QT_QTGUI_LIBRARY}
    )

    SET( MACOSX_BUNDLE_INFO_STRING "TornetInfoString" )
    SET( MACOSX_BUNDLE_BUNDLE_NAME "TornetBundleName"  )
add_executable( tornet 
  MACOSX_BUNDLE 
  ${sources} )
target_link_libraries( tornet ${libraries} )
