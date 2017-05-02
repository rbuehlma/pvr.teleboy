project(pvr.teleboy)

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})
enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)
find_package(Yajl REQUIRED)
find_package(CURL REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${YAJL_INCLUDE_DIR}
                    ${CURL_INCLUDE_DIRS})

set(DEPLIBS ${kodiplatform_LIBRARIES} 
            ${p8-platform_LIBRARIES}
            ${YAJL_LIBRARIES}
            ${CURL_LIBRARIES}
)

set(TELEBOY_SOURCES
		src/Utils.cpp
		src/client.cpp
		src/Curl.cpp
		src/TeleBoy.cpp
		src/JsonParser.cpp
		 )

if(WIN32)
	list(APPEND DEPLIBS ws2_32)
endif()

build_addon(pvr.teleboy TELEBOY DEPLIBS)

include(CPack)


