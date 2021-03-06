#-------------------------------------------------------------------------------
# marinatb-test build file
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# cmake ........................................................................
cmake_minimum_required(VERSION 3.0)

set( CMAKE_CXX_COMPILER /usr/local/bin/clang++ )
set( CMAKE_C_COMPILER /usr/local/bin/clang )

project(marinatb)

set(BUILD_SHARED_LIBS ON)

include_directories(
  /usr/local/include/c++/v1 
  /usr/local/include/bsoncxx/v_noabi
  /usr/local/include/mongocxx/v_noabi
)


set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${marinatb_SOURCE_DIR}/cmake"
  "/usr/local/lib/cmake")


# compiler settings ............................................................
set( CMAKE_C_FLAGS
  "${CMAKE_C_FLAGS} \
  -std=c11 \
  -Wall -Wextra -fPIC" )

set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} \
  -g -O0 -fno-optimize-sibling-calls -fno-omit-frame-pointer" )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
  -g -std=c++1z -stdlib=libc++ \
  -Wall -Wextra \
  -fdiagnostics-color=always -fPIC" )

set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} \
  -O3" )

set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} \
  -g -O0 -fno-optimize-sibling-calls -fno-omit-frame-pointer" )

include_directories(
  "${marinatb_SOURCE_DIR}"
  "/usr/local/include/c++/v1"
)

# build components .............................................................
if(EXISTS "${marinatb_SOURCE_DIR}/common")
  add_subdirectory(common)
endif(EXISTS "${marinatb_SOURCE_DIR}/common")

if(EXISTS "${marinatb_SOURCE_DIR}/test")
  add_subdirectory(test)
endif(EXISTS "${marinatb_SOURCE_DIR}/test")

if(EXISTS "${marinatb_SOURCE_DIR}/api")
  add_subdirectory(api)
endif(EXISTS "${marinatb_SOURCE_DIR}/api")

if(EXISTS "${marinatb_SOURCE_DIR}/core")
  add_subdirectory(core)
endif(EXISTS "${marinatb_SOURCE_DIR}/core")

