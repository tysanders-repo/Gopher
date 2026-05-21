#include "gopher/gopher_client_lib.hpp"
#include <cassert>
#include <iostream>

// Returns 0 on pass, 1 on fail.
// These tests exercise GopherClient state that doesn't require camera or network.

static int test_initial_state() {
    GopherClient client;
    assert(!client.is_in_call());
    assert(client.get_name().empty());
    assert(client.get_port() == 0);
    return 0;
}

static int test_initialize_binds_udp_port() {
    GopherClient client;
    // Port 0 lets the OS pick one; expect a non-zero port back.
    if (!client.initialize("unit-test", 0)) {
        std::cerr << "FAIL: initialize returned false\n";
        return 1;
    }
    if (client.get_port() == 0) {
        std::cerr << "FAIL: expected non-zero port after initialize\n";
        client.shutdown();
        return 1;
    }
    if (client.get_name() != "unit-test") {
        std::cerr << "FAIL: name mismatch\n";
        client.shutdown();
        return 1;
    }
    if (client.is_in_call()) {
        std::cerr << "FAIL: should not be in call after initialize\n";
        client.shutdown();
        return 1;
    }
    client.shutdown();
    return 0;
}

static int test_double_initialize_is_noop() {
    GopherClient client;
    if (!client.initialize("test", 0)) return 1;
    // Second initialize should return false (already initialized)
    if (client.initialize("test2", 0)) {
        std::cerr << "FAIL: double initialize should return false\n";
        client.shutdown();
        return 1;
    }
    client.shutdown();
    return 0;
}

int main() {
    int failures = 0;
    failures += test_initial_state();
    failures += test_initialize_binds_udp_port();
    failures += test_double_initialize_is_noop();

    if (failures == 0)
        std::cout << "All unit tests passed.\n";
    else
        std::cerr << failures << " unit test(s) FAILED.\n";

    return failures;
}
