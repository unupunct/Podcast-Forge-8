file(GLOB PF8_UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
file(GLOB PF8_LIVE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/live/*.cpp)
file(GLOB PF8_HARNESS    CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/harness/*.cpp)

add_executable(pf8_tests ${PF8_UNIT_TESTS} ${PF8_LIVE_TESTS} ${PF8_HARNESS} support/rt_new.cpp)
target_include_directories(pf8_tests PRIVATE support harness)
target_compile_definitions(pf8_tests PRIVATE
    PF8_TEST_SCRATCH="${CMAKE_BINARY_DIR}/test-scratch"
    PF8_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

set(PF8_TEST_LIBS pf8_core)
foreach(lib pf8_devices pf8_dsp pf8_routing pf8_record pf8_engine)
    if(TARGET ${lib})
        list(APPEND PF8_TEST_LIBS ${lib})
    endif()
endforeach()
target_link_libraries(pf8_tests PRIVATE ${PF8_TEST_LIBS} pf8_juce Catch2::Catch2WithMain)
pf8_set_warnings(pf8_tests)

include(${PROJECT_SOURCE_DIR}/third_party/Catch2/extras/Catch.cmake)
catch_discover_tests(pf8_tests TEST_SPEC "~[live]" DISCOVERY_MODE PRE_TEST)
catch_discover_tests(pf8_tests TEST_SPEC "[live]" TEST_PREFIX "live: " PROPERTIES LABELS live DISCOVERY_MODE PRE_TEST)
