PROJECT( tornet )

CMAKE_MINIMUM_REQUIRED( VERSION 2.8.0 )
SET( CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}" )

INCLUDE( VersionMacros )
INCLUDE( SetupTargetMacros )

SET( DEFAULT_HEADER_INSTALL_DIR include/\${target} )
SET( DEFAULT_LIBRARY_INSTALL_DIR lib/ )
SET( DEFAULT_EXECUTABLE_INSTALL_DIR bin/ )
SET( CMAKE_DEBUG_POSTFIX _debug )

IF( WIN32 )
	ADD_DEFINITIONS( -DBOOST_CONTEXT_NO_LIB )
	ADD_DEFINITIONS( -D_SCL_SECURE_NO_WARNINGS )
	ADD_DEFINITIONS( -D_WIN32_WINNT=0x0501 )
	ADD_DEFINITIONS( -D_CRT_SECURE_NO_WARNINGS )
ELSE(WIN32)
   SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall")
ENDIF(WIN32)

if( UNIX ) 
  if( NOT APPLE )  
    set(rt_library rt )
	set(pthread_library pthread) 
  endif() 
endif()



FIND_PACKAGE( Wt REQUIRED )
INCLUDE_DIRECTORIES( ${Wt_INCLUDE_DIR} )
find_package( Qt4 REQUIRED )
add_definitions( -DQT_NO_KEYWORDS )

#find_package( Protobuf REQUIRED )

FIND_PACKAGE( OpenSSL REQUIRED )
SET(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE(Boost 1.51 COMPONENTS thread date_time system filesystem program_options signals serialization chrono unit_test_framework regex random ) 
LINK_DIRECTORIES( ${Boost_LIBRARY_DIRS} )
include_directories( ${Boost_INCLUDE_DIR} )

include_directories( ${QT_QTCORE_INCLUDE_DIR} )
include_directories( ${QT_QTGUI_INCLUDE_DIR} )
include_directories( fc/include )
#include_directories( ../reflect/include )
include_directories( include )
include_directories( /usr/include )
include_directories( /opt/local/include/db53 )

link_directories( /opt/local/lib/db53 )
#set( BerkeleyDB_LIBRARY /opt/local/lib/db53/libdb_cxx.a )
set( BerkeleyDB_LIBRARY -ldb_cxx )

add_subdirectory(fc)
add_subdirectory(context)


SET( sources 
    src/main.cpp
    src/WTornetApplication.cpp
    src/node.cpp
    src/connection.cpp
    src/db/chunk.cpp
    src/db/peer.cpp
    src/db/publish.cpp
    src/db/name.cpp
    src/buffer.cpp
    src/channel.cpp
    src/kad.cpp
    src/kbucket.cpp
    src/chunk_service.cpp
    src/tornet_file.cpp
    src/name_service.cpp
    src/udt_test_service.cpp
    src/udt_channel.cpp
    src/miss_list.cpp
    src/chunk_search.cpp
    src/chunk_service_client.cpp
    src/chunk_service_messages.cpp
    src/download_status.cpp
    src/tornet_app.cpp
    src/chisq.c
    src/raw_rpc.cpp

 #   main.cpp
 #   gui/QBind.cpp
 #   net/channel.cpp
 #   net/node.cpp
 #   net/kad.cpp
 #   net/udt_channel.cpp
 #   net/detail/miss_list.cpp
 #   net/detail/connection.cpp
 #   net/detail/node_private.cpp
 #   rpc/connection.cpp
 #   rpc/service.cpp
 #   rpc/udp_connection.cpp
 #   rpc/udt_connection.cpp
 #   services/accounting.cpp
 #   services/chat.cpp
 #   services/calc.cpp
 #   services/chunk_service.cpp
 #   services/chunk_session.cpp
 #   services/chunk_search.cpp
 #   db/peer.cpp
 #   db/chunk.cpp 
 #   db/publish.cpp 
   )

SET( moc_headers 
    gui/QBind.hpp
   )          


#QT4_WRAP_UI( sources gui/main.ui )
#QT4_WRAP_CPP( sources ${moc_headers} )

SET( CXX_FLAGS -Wall )

SET( libraries 
    fc
    boost_context
    ${Wt_DEBUG_LIBRARIES}
    ${Wt_HTTP_LIBRARY}
    ${Wt_EXT_LIBRARY}
    ${BerkeleyDB_LIBRARY}
    ${Boost_THREAD_LIBRARY} 
    ${Boost_CHRONO_LIBRARY} 
    ${Boost_DATE_TIME_LIBRARY} 
    ${Boost_ASIO_LIBRARY} 
    ${Boost_SYSTEM_LIBRARY} 
    ${Boost_FILESYSTEM_LIBRARY} 
    ${Boost_PROGRAM_OPTIONS_LIBRARY} 
    ${Boost_SIGNALS_LIBRARY}
    ${Boost_REGEX_LIBRARY}
    ${QT_QTCORE_LIBRARY}
    ${QT_QTGUI_LIBRARY}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_CRYPTO_LIBRARIES}
    ${OPENSSL_SSL_LIBRARIES}
    z
    ${Boost_RANDOM_LIBRARY}
    ${rt_library}
    ${pthread_library}
    )

    SET( MACOSX_BUNDLE_INFO_STRING "TornetInfoString" )
    SET( MACOSX_BUNDLE_BUNDLE_NAME "TornetBundleName"  )
add_executable( tornet 
  MACOSX_BUNDLE 
  ${sources} )
target_link_libraries( tornet ${libraries} )

