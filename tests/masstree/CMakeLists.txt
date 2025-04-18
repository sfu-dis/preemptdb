set(ERMIA_INCLUDES
  ${CMAKE_SOURCE_DIR}
)

set(MASSTREE_SRCS
  ${CMAKE_SOURCE_DIR}/masstree/compiler.cc
  ${CMAKE_SOURCE_DIR}/masstree/straccum.cc
  ${CMAKE_SOURCE_DIR}/masstree/str.cc
  ${CMAKE_SOURCE_DIR}/masstree/string.cc
)

set(DBCORE_SRCS
  ${CMAKE_SOURCE_DIR}/dbcore/adler.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/burt-hash.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/dynarray.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/epoch.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/mcs_lock.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/serial.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/size-encode.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-alloc.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-chkpt.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-common.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-config.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-coroutine.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-exceptions.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-table.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-alloc.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-file.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-offset.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-oid-replay-impl.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-recover.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-log-recover-impl.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-object.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-oid.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/sm-tx-log.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/window-buffer.cpp
  ${CMAKE_SOURCE_DIR}/dbcore/xid.cpp
)

set(MASSTREE_TEST_SRCS
    ${DBCORE_SRCS}
    ${MASSTREE_SRCS}
    single_threaded.cpp
    concurrent.cpp
    record.h
    record.cpp
    test_main.cpp
)

add_executable(test_masstree ${MASSTREE_TEST_SRCS})
target_include_directories(test_masstree PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(test_masstree gtest_main thread_pool)

add_executable(test_masstree_coro ${MASSTREE_TEST_SRCS})
set_target_properties(test_masstree_coro PROPERTIES COMPILE_FLAGS "-DNESTED_COROUTINE")
target_include_directories(test_masstree_coro PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(test_masstree_coro gtest_main thread_pool)


set(SS_PERF_SRCS
    ${DBCORE_SRCS}
    ${MASSTREE_SRCS}
    record.h
    record.cpp
    gbench_main.cpp
    perf_single_threaded.cpp
)

add_executable(perf_single_thread_masstree ${SS_PERF_SRCS})
target_include_directories(perf_single_thread_masstree PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(perf_single_thread_masstree thread_pool benchmark_main)

add_executable(perf_single_thread_masstree_coro ${SS_PERF_SRCS})
set_target_properties(perf_single_thread_masstree_coro PROPERTIES COMPILE_FLAGS "-DNESTED_COROUTINE")
target_include_directories(perf_single_thread_masstree_coro PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(perf_single_thread_masstree_coro thread_pool benchmark_main)

set(CONCURRENT_PERF_SRCS
    ${DBCORE_SRCS}
    ${MASSTREE_SRCS}
    record.h
    record.cpp
    perf_concurrent.cpp
)

add_executable(perf_concurrent_masstree ${CONCURRENT_PERF_SRCS})
target_include_directories(perf_concurrent_masstree PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(perf_concurrent_masstree thread_pool)

add_executable(perf_concurrent_masstree_coro ${CONCURRENT_PERF_SRCS})
set_target_properties(perf_concurrent_masstree_coro PROPERTIES COMPILE_FLAGS "-DNESTED_COROUTINE")
target_include_directories(perf_concurrent_masstree_coro PRIVATE ${ERMIA_INCLUDES})
target_link_libraries(perf_concurrent_masstree_coro thread_pool)
