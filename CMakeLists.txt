cmake_minimum_required(VERSION 3.5)
project(pvr.teleboy)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(PkgConfig)
find_package(Kodi REQUIRED)
find_package(RapidJSON REQUIRED)

include_directories(${KODI_INCLUDE_DIR}
                    ${RAPIDJSON_INCLUDE_DIRS}
)

set(TELEBOY_SOURCES
		src/Utils.cpp
		src/Curl.cpp
		src/md5.cpp
		src/Cache.cpp
		src/TeleBoy.cpp
		src/UpdateThread.cpp
		src/categories.cpp
)

set(TELEBOY_HEADERS
		src/Curl.h
		src/md5.h
		src/Cache.h
		src/UpdateThread.h
		src/TeleBoy.h
		src/to_string.h
		src/Utils.h
		src/windows.h
		src/categories.h
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
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DTELEBOY_VERSION=${TELEBOY_VERSION} -DKODI_VERSION=${kodiplatform_VERSION}")


build_addon(pvr.teleboy TELEBOY DEPLIBS)

include(CPack)
