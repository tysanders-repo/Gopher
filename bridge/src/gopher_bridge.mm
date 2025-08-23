#include "gopher_bridge/gopher_bridge.h"
#include "gopher/gopher_client_lib.hpp"
#include <string>
#include <memory>

// Bridge implementation
struct gopher_client {
    std::unique_ptr<GopherClient> client;
    std::string ip;
    int port;
    gopher_incoming_call_callback_t incoming_callback;
};

extern "C" {

gopher_error_t gopher_client_create(const char* username, int port, gopher_client_t* client) {
    if (!username || !client) {
        return GOPHER_ERROR_INVALID_PARAMETER;
    }
    
    try {
        auto* new_client = new gopher_client();
        new_client->client = std::make_unique<GopherClient>();
        new_client->port = port;
        
        // Initialize the C++ client
        if (!new_client->client->initialize(username, port)) {
            delete new_client;
            return GOPHER_ERROR_INITIALIZATION_FAILED;
        }
        
        // Get IP and port
        new_client->ip = new_client->client->get_ip();
        new_client->port = new_client->client->get_port();
        
        *client = new_client;
        return GOPHER_SUCCESS;
    } catch (...) {
        return GOPHER_ERROR_INITIALIZATION_FAILED;
    }
}

void gopher_client_destroy(gopher_client_t client) {
    if (client) {
        delete client;
    }
}

gopher_error_t gopher_client_start_call(gopher_client_t client, const char* ip, int port) {
    if (!client || !ip) {
        return GOPHER_ERROR_INVALID_PARAMETER;
    }
    
    try {
        if (client->client->start_call(ip, port)) {
            return GOPHER_SUCCESS;
        } else {
            return GOPHER_ERROR_NETWORK_FAILED;
        }
    } catch (...) {
        return GOPHER_ERROR_NETWORK_FAILED;
    }
}

gopher_error_t gopher_client_end_call(gopher_client_t client) {
    if (!client) {
        return GOPHER_ERROR_INVALID_PARAMETER;
    }
    
    try {
        client->client->end_call();
        return GOPHER_SUCCESS;
    } catch (...) {
        return GOPHER_ERROR_NETWORK_FAILED;
    }
}

const char* gopher_client_get_ip(gopher_client_t client) {
    if (!client) {
        return nullptr;
    }
    return client->ip.c_str();
}

int gopher_client_get_port(gopher_client_t client) {
    if (!client) {
        return -1;
    }
    return client->port;
}

void gopher_client_set_dev_mode(gopher_client_t client, int enabled) {
    if (client) {
        client->client->enable_dev_mode(enabled);
    }
}

void gopher_client_set_incoming_call_callback(gopher_client_t client, gopher_incoming_call_callback_t callback) {
    if (client) {
        client->incoming_callback = callback;
        // TODO: Connect this to the C++ client's callback system
    }
}

} // extern "C"
