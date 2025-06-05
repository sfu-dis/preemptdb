cmake_minimum_required(VERSION 3.1)
project(preemptdb)

option(USE_LIBUINTRDRIV
  "whether to use the driver version of the user interrupt support (for unpatched kernels)"
  OFF
)


# No in-source build
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "In-source builds are not allowed.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

find_package(PkgConfig REQUIRED)

if(${CMAKE_C_COMPILER} MATCHES "clang+")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fcoroutines-ts -stdlib=libc++ -Woverloaded-virtual -Werror=reorder -Werror=unused-result -Wno-address-of-packed-member")

  include_directories(/opt/dis/llvm-libc++/include/c++/v1)
  link_directories(/opt/dis/llvm-libc++/lib)

  # Pre-build glog and gflags with libc++
  link_directories(${CMAKE_CURRENT_SOURCE_DIR}/third-party/glog/lib)
  link_directories(${CMAKE_CURRENT_SOURCE_DIR}/third-party/gflags/lib)

  # The required headers for libc++.
  include_directories(third-party/glog/include)
  include_directories(third-party/gflags/include)
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -fcoroutines -Woverloaded-virtual -Werror=reorder -Wno-address-of-packed-member -Wno-format-truncation -Wno-unused-result -Wno-stringop-overflow")

  # link glog for gcc.
  link_directories(/usr/local/lib)
endif()

if (USE_LIBUINTRDRIV)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_LIBUINTRDRIV")
endif()

# Store shared libraries under /lib
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Use masstree for index
if(NOT DEFINED MASSTREE)
  set(MASSTREE 1)
endif()
add_definitions(-DMASSTREE=${MASSTREE})

# Disable prefetch in masstree
#add_definitions(-DNOPREFETCH)

# Assume 64-byte cache line
if(NOT DEFINED CACHELINE_SIZE)
  set(CACHELINE_SIZE 64)
endif()
add_definitions(-DCACHELINE_SIZE=${CACHELINE_SIZE})

# Page size for O_DIRECT
if(NOT DEFINED PAGE_SIZE)
  set(PAGE_SIZE 2048)
endif()
add_definitions(-DPAGE_SIZE=${PAGE_SIZE})

if(NOT DEFINED DEBUG_UINTR)
  set(DEBUG_UINTR 0)
endif()
add_definitions(-DDEBUG_UINTR=${DEBUG_UINTR})

if(NOT DEFINED YIELD_ALL)
  set(YIELD_ALL 1)
endif()
add_definitions(-DYIELD_ALL=${YIELD_ALL})

if(NOT DEFINED DISABLE_TIMER)
  set(DISABLE_TIMER 0)
endif()
add_definitions(-DDISABLE_TIMER=${DISABLE_TIMER})

set(MASSTREE_CONFIG "--enable-max-key-len=1024 --with-malloc=malloc")

# Enable AddressSanitizer for supported compilers
# set(ASAN_FLAGS "-fsanitize=address")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS} -O1 -g -fno-omit-frame-pointer")
# set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${ASAN_FLAGS}")

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -include ${CMAKE_CURRENT_SOURCE_DIR}/masstree/config-debug.h")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -include ${CMAKE_CURRENT_SOURCE_DIR}/masstree/config-release.h")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -include ${CMAKE_CURRENT_SOURCE_DIR}/masstree/config-release.h")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

add_compile_options(-Wno-frame-address -muintr -save-temps -fverbose-asm -mfsgsbase -mxsaveopt)
link_libraries(m)

find_library(LIBCPPABI c++abi)
if (LIBCPPABI-NOTFOUND)
  message(FATAL_ERROR "library libc++abi is not found")
endif()
link_libraries(${LIBCPPABI})

pkg_check_modules(LIBNUMA REQUIRED numa)
add_compile_options(${LIBNUMA_CFLAGS_OTHER})
include_directories(${LIBNUMA_INCLUDE_DIRS})
link_libraries(${LIBNUMA_LDFLAGS})

find_package(Threads)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

find_library(LIBRT rt)
if (LIBRT-NOTFOUND)
  message(FATAL_ERROR "library librt is not found")
endif()
link_libraries(${LIBRT})

find_package(glog REQUIRED)
link_libraries(glog::glog)

find_package(gflags REQUIRED COMPONENTS shared)
link_libraries(gflags_shared)

if (USE_LIBUINTRDRIV)
find_library(LIBUINTRDRIV uintrdriv)
if (LIBUINTRDRIV-NOTFOUND)
  message(FATAL_ERROR "libuintrdriv not found, please download and install from https://github.com/UB-ADBLAB/uintr-driver")
endif()
link_libraries(${LIBUINTRDRIV})
endif()

pkg_check_modules(LIBURING REQUIRED liburing)
add_compile_options(${LIBURING_CFLAGS_OTHER})
include_directories(${LIBURING_INCLUDE_DIRS})
link_libraries(${LIBURING_LDFLAGS})

#set(LINK_FLAGS "-lc++abi -lnuma -lpthread -pthread -lrt -lglog -lgflags -luring")
#link_libraries(${LINK_FLAGS})

include_directories(dbcore)
include_directories(masstree)
include_directories(third-party/sparsehash/src)

add_subdirectory(benchmarks)
add_subdirectory(benchmarks/tpcc)
add_subdirectory(dbcore)
add_subdirectory(masstree)

set_property(GLOBAL APPEND PROPERTY ALL_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/engine.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/tuple.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/txn.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/txn-ssn.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/txn-ssi.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/txn-mvocc.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/uintr.cc
)

# Optimization will change the asm implementation in uintr
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/uintr.cc PROPERTIES COMPILE_FLAGS "-O0")

get_property(LIB_SRC GLOBAL PROPERTY ALL_SRC)

# Thread pool source
set_property(GLOBAL APPEND PROPERTY THREAD_POOL_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-thread.cpp
)
get_property(THREAD_POOL_SRC GLOBAL PROPERTY THREAD_POOL_SRC)

# Thread pool library
add_library(thread_pool SHARED ${THREAD_POOL_SRC})

# SI
add_library(preemptdb_si SHARED ${LIB_SRC})
set_target_properties(preemptdb_si PROPERTIES COMPILE_FLAGS "")
