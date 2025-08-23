#ifndef GOPHER_BRIDGE_H
#define GOPHER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
typedef enum {
    GOPHER_SUCCESS = 0,
    GOPHER_ERROR_INITIALIZATION_FAILED = -1,
    GOPHER_ERROR_NETWORK_FAILED = -2,
    GOPHER_ERROR_MEDIA_FAILED = -3,
    GOPHER_ERROR_INVALID_PARAMETER = -4
} gopher_error_t;

// Client handle
typedef struct gopher_client* gopher_client_t;

// Initialize Gopher client
gopher_error_t gopher_client_create(const char* username, int port, gopher_client_t* client);

// Destroy Gopher client
void gopher_client_destroy(gopher_client_t client);

// Start a call
gopher_error_t gopher_client_start_call(gopher_client_t client, const char* ip, int port);

// End current call
gopher_error_t gopher_client_end_call(gopher_client_t client);

// Get client IP
const char* gopher_client_get_ip(gopher_client_t client);

// Get client port
int gopher_client_get_port(gopher_client_t client);

// Enable/disable dev mode
void gopher_client_set_dev_mode(gopher_client_t client, int enabled);

// Set incoming call callback
typedef void (*gopher_incoming_call_callback_t)(const char* caller_ip, int caller_port, const char* caller_name);
void gopher_client_set_incoming_call_callback(gopher_client_t client, gopher_incoming_call_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // GOPHER_BRIDGE_H
