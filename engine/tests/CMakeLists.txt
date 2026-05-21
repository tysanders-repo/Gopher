add_executable(gopher_self_loopback_test self_loopback_main.cpp)
target_link_libraries(gopher_self_loopback_test PRIVATE gopher_core)

add_executable(gopher_dual_peer_test dual_peer_main.cpp)
target_link_libraries(gopher_dual_peer_test PRIVATE gopher_core)

# Headless unit tests — registered with CTest so CI can run them.
add_executable(gopher_unit_tests unit_tests.cpp)
target_link_libraries(gopher_unit_tests PRIVATE gopher_core)

add_test(NAME unit_tests COMMAND gopher_unit_tests)
