cmake_minimum_required(VERSION 3.5)
project(pvr.teleboy)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(PkgConfig)
find_package(Kodi REQUIRED)
find_package(RapidJSON REQUIRED)

include_directories(${KODI_INCLUDE_DIR}
                    ${RAPIDJSON_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib
)

add_subdirectory(lib/sqlite)

set(DEPLIBS sqlite
)

set(TELEBOY_SOURCES
		src/Utils.cpp
		src/md5.cpp
		src/Session.cpp
		src/TeleBoy.cpp
		src/UpdateThread.cpp
		src/categories.cpp
		src/sql/SQLConnection.cpp
		src/sql/ParameterDB.cpp	
		src/http/Curl.cpp
		src/http/Cache.cpp
		src/http/HttpClient.cpp
)

set(TELEBOY_HEADERS
		src/md5.h
		src/UpdateThread.h
		src/Session.h
		src/TeleBoy.h
		src/to_string.h
		src/Utils.h
		src/windows.h
		src/categories.h
		src/sql/SQLConnection.h
		src/sql/ParameterDB.h
		src/http/Curl.h
		src/http/Cache.h
		src/http/HttpClient.h
		src/http/HttpStatusCodeHandler.h
)

if(WIN32)
	list(APPEND DEPLIBS ws2_32)
endif()

if ( CMAKE_COMPILER_IS_GNUCC )
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall")
endif ( CMAKE_COMPILER_IS_GNUCC )
if ( MSVC )
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /W4")
endif ( MSVC )

addon_version(pvr.teleboy TELEBOY)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DTELEBOY_VERSION=${TELEBOY_VERSION} -DKODI_VERSION=${APP_VERSION_CODE}")


build_addon(pvr.teleboy TELEBOY DEPLIBS)

include(CPack)
