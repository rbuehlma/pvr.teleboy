project(pvr.teleboy)

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})
set(PC_RapidJSON_INCLUDEDIR ${PROJECT_SOURCE_DIR}/depends/target/rapidjson/native/build/dummy/include)

enable_language(CXX)

execute_process(
      COMMAND make PREFIX=dummy
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/depends/target/rapidjson/
)


find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)
find_package(RapidJSON REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${RAPIDJSON_INCLUDE_DIRS}
)

set(DEPLIBS ${kodiplatform_LIBRARIES} 
            ${p8-platform_LIBRARIES}
)

set(TELEBOY_SOURCES
		src/Utils.cpp
		src/client.cpp
		src/Curl.cpp
		src/TeleBoy.cpp
		src/UpdateThread.cpp
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


build_addon(pvr.teleboy TELEBOY DEPLIBS)

include(CPack)


