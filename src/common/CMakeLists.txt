
#
# Create svnversion.h
#
message( STATUS "Creating svnversion.h" )
if( SVNVERSION )
	file( WRITE ${CMAKE_CURRENT_BINARY_DIR}/svnversion.h
		"#ifndef SVNVERSION\n#define SVNVERSION ${SVNVERSION}\n#endif\n" )
else()
	file( WRITE ${CMAKE_CURRENT_BINARY_DIR}/svnversion.h "" )
endif()
set( GLOBAL_INCLUDE_DIRS ${GLOBAL_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "" )
set( SVNVERSION ${SVNVERSION}
	CACHE STRING "SVN version of the source code" )
if( INSTALL_COMPONENT_DEVELOPMENT )
	install( FILES ${CMAKE_CURRENT_BINARY_DIR}/svnversion.h
		DESTINATION "src/common"
		COMPONENT Development_base )
endif( INSTALL_COMPONENT_DEVELOPMENT )
message( STATUS "Creating svnversion.h - done" )


#####################################################################
# setup
#
set( COMMON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
	CACHE PATH "common source directory" )
mark_as_advanced( COMMON_SOURCE_DIR )

set( COMMON_ALL_HEADERS
	"${CMAKE_CURRENT_BINARY_DIR}/svnversion.h"
	"${COMMON_SOURCE_DIR}/cbasetypes.h"
	"${COMMON_SOURCE_DIR}/mmo.h"
	"${COMMON_SOURCE_DIR}/plugin.h"
	"${COMMON_SOURCE_DIR}/version.h"
	)


#
# common_base
#
if( WITH_ZLIB )
message( STATUS "Creating target common_base" )
set( COMMON_BASE_HEADERS
	${COMMON_ALL_HEADERS}
	"${COMMON_SOURCE_DIR}/core.h"
	"${COMMON_SOURCE_DIR}/db.h"
	"${COMMON_SOURCE_DIR}/des.h"
	"${COMMON_SOURCE_DIR}/ers.h"
	"${COMMON_SOURCE_DIR}/grfio.h"
	"${COMMON_SOURCE_DIR}/malloc.h"
	"${COMMON_SOURCE_DIR}/mapindex.h"
	"${COMMON_SOURCE_DIR}/md5calc.h"
	"${COMMON_SOURCE_DIR}/nullpo.h"
	"${COMMON_SOURCE_DIR}/plugins.h"
	"${COMMON_SOURCE_DIR}/random.h"
	"${COMMON_SOURCE_DIR}/showmsg.h"
	"${COMMON_SOURCE_DIR}/socket.h"
	"${COMMON_SOURCE_DIR}/strlib.h"
	"${COMMON_SOURCE_DIR}/timer.h"
	"${COMMON_SOURCE_DIR}/utils.h"
	"${COMMON_SOURCE_DIR}/msg_conf.h"
	CACHE INTERNAL "common_base headers" )
set( COMMON_BASE_SOURCES
	"${COMMON_SOURCE_DIR}/core.c"
	"${COMMON_SOURCE_DIR}/db.c"
	"${COMMON_SOURCE_DIR}/des.c"
	"${COMMON_SOURCE_DIR}/ers.c"
	"${COMMON_SOURCE_DIR}/grfio.c"
	"${COMMON_SOURCE_DIR}/lock.c"
	"${COMMON_SOURCE_DIR}/malloc.c"
	"${COMMON_SOURCE_DIR}/mapindex.c"
	"${COMMON_SOURCE_DIR}/md5calc.c"
	"${COMMON_SOURCE_DIR}/nullpo.c"
	"${COMMON_SOURCE_DIR}/plugins.c"
	"${COMMON_SOURCE_DIR}/random.c"
	"${COMMON_SOURCE_DIR}/showmsg.c"
	"${COMMON_SOURCE_DIR}/socket.c"
	"${COMMON_SOURCE_DIR}/strlib.c"
	"${COMMON_SOURCE_DIR}/timer.c"
	"${COMMON_SOURCE_DIR}/utils.c"
	"${COMMON_SOURCE_DIR}/msg_conf.c"
	CACHE INTERNAL "common_base sources" )
set( DEPENDENCIES ${ZLIB_DEPENDENCIES} )
set( LIBRARIES ${GLOBAL_LIBRARIES} ${ZLIB_LIBRARIES} )
set( INCLUDE_DIRS ${GLOBAL_INCLUDE_DIRS} ${MT19937AR_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} )
set( DEFINITIONS "${GLOBAL_DEFINITIONS} ${ZLIB_DEFINITIONS}" )
set( SOURCE_FILES ${MT19937AR_HEADERS} ${MT19937AR_SOURCES} ${COMMON_BASE_HEADERS} ${COMMON_BASE_SOURCES} )
source_group( mt19937ar FILES ${MT19937AR_HEADERS} ${MT19937AR_SOURCES} )
source_group( common FILES ${COMMON_BASE_HEADERS} ${COMMON_BASE_SOURCES} )
add_library( common_base ${SOURCE_FILES} )
if( DEPENDENCIES )
	add_dependencies( common_base ${DEPENDENCIES} )
endif()
target_link_libraries( common_base ${LIBRARIES} )
set_target_properties( common_base PROPERTIES COMPILE_FLAGS "${DEFINITIONS}" )
include_directories( ${INCLUDE_DIRS} )
set( HAVE_common_base ON  CACHE INTERNAL "" )
set( TARGET_LIST ${TARGET_LIST} common_base  CACHE INTERNAL "" )
message( STATUS "Creating target common_base - done" )
else()
message( STATUS "Skipping target common_base (requires ZLIB)" )
unset( HAVE_common_base CACHE )
endif()


#
# common_sql
#
if( HAVE_common_base AND WITH_MYSQL )
message( STATUS "Creating target common_sql" )
set( COMMON_SQL_HEADERS
	${COMMON_ALL_HEADERS}
	"${CMAKE_CURRENT_SOURCE_DIR}/sql.h"
	CACHE INTERNAL "common_sql headers" )
set( COMMON_SQL_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/sql.c"
	CACHE INTERNAL "common_sql sources" )
set( DEPENDENCIES common_base ${MYSQL_DEPENDENCIES} )
set( LIBRARIES ${GLOBAL_LIBRARIES} common_base ${MYSQL_LIBRARIES} )
set( INCLUDE_DIRS ${GLOBAL_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIRS} )
set( DEFINITIONS "${GLOBAL_DEFINITIONS} ${MYSQL_DEFINITIONS}" )
set( SOURCE_FILES ${COMMON_SQL_HEADERS} ${COMMON_SQL_SOURCES} )
source_group( common FILES ${COMMON_SQL_HEADERS} ${COMMON_SQL_SOURCES} )
add_library( common_sql ${SOURCE_FILES} )
if( DEPENDENCIES )
	add_dependencies( common_sql ${DEPENDENCIES} )
endif()
target_link_libraries( common_sql ${LIBRARIES} )
set_target_properties( common_sql PROPERTIES COMPILE_FLAGS "${DEFINITIONS}" )
include_directories( ${INCLUDE_DIRS} )
set( HAVE_common_sql ON  CACHE INTERNAL "" )
set( TARGET_LIST ${TARGET_LIST} common_sql  CACHE INTERNAL "" )
message( STATUS "Creating target common_sql - done" )
else()
message( STATUS "Skipping target common_sql (requires common_base and MYSQL)" )
unset( HAVE_common_sql CACHE )
endif()
