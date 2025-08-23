#ifndef GOPHER_BRIDGE_H
#define GOPHER_BRIDGE_H

#include <stdint.h>  // Add this line for uint16_t

// Error codes
#define GOPHER_BRIDGE_SUCCESS 0
#define GOPHER_BRIDGE_ERROR -1

// Client handle type
typedef struct gopher_client* gopher_client_handle_t;

// Core client management
gopher_client_handle_t gopher_bridge_create(void);
void gopher_bridge_destroy(gopher_client_handle_t client);

// Client operations
int gopher_bridge_start_call(gopher_client_handle_t client, const char* ip, uint16_t port);
void gopher_bridge_end_call(gopher_client_handle_t client);
const char* gopher_bridge_get_ip(gopher_client_handle_t client);
uint16_t gopher_bridge_get_port(gopher_client_handle_t client);
void gopher_bridge_set_dev_mode(gopher_client_handle_t client, int enabled);

// Callback for incoming calls
typedef int (*incoming_call_callback_t)(const char* caller_name, const char* caller_ip, uint16_t caller_port);
void gopher_bridge_set_incoming_call_callback(gopher_client_handle_t client, incoming_call_callback_t callback);

#endif // GOPHER_BRIDGE_H
