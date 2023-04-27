/**
 * @file aos_ws_client.h
 * @author Michele Riva (michele.riva@protonmail.com)
 * @brief AsyncRTOS Websocket client API
 * @version 0.9.0
 * @date 2023-04-23
 *
 * @copyright Copyright (c) 2023
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless futureuired by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#pragma once
#include <aos.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Websocket client unexpected events
     */
    typedef enum
    {
        AOS_WS_CLIENT_EVENT_DISCONNECTED, // Client disconnected unexpectedly
        AOS_WS_CLIENT_EVENT_RECONNECTING, // Client is recovering connection
        AOS_WS_CLIENT_EVENT_RECONNECTED,  // Client has recovered connection
    } aos_ws_client_event_t;

    /**
     * @brief Websocket client connection modes
     */
    typedef enum
    {
        AOS_WS_CLIENT_MODE_SECURE,      // Use TLS as transport layer and verify server certificates
        AOS_WS_CLIENT_MODE_SECURE_TEST, // Use TLS as transport layer and verify server certificates (but not the CN field)
        AOS_WS_CLIENT_MODE_INSECURE,    // Use TCP as transport layer
    } aos_ws_client_mode_t;

    /**
     * @brief Websocket client configuration
     */
    typedef struct aos_ws_client_config_t
    {
        void (*on_data)(const void *data, size_t data_len);             // Handler for data events (required)
        void (*event_handler)(aos_ws_client_event_t event, void *args); // Unexpected events handler (required)
        const char *host;                                               // Host to connect to (required)
        const char *path;                                               // Server path (defaults to "/")
        aos_ws_client_mode_t mode;                                      // Connection mode (defaults to AOS_WS_CLIENT_MODE_SECURE)
        uint16_t port;                                                  // Server port (defaults to 443)
        const char *subprotocol;                                        // Subprotocol (defaults to NULL)
        const char *user_agent;                                         // User agent (defaults to "AOS Websocket Client")
        const char *headers;                                            // Handshake headers (defaults to NULL)
        const char *server_cert_chain_pem;                              // Server certificate chain in PEM format (defaults to NULL)
        const char *client_cert_chain_pem;                              // Client certificate chain in PEM format (defaults to NULL)
        const char *client_key_pem;                                     // Client key in PEM format (defaults to NULL)
        uint32_t connection_attempts;                                   // Number of connection attempts before giving up (defaults to 3)
        uint32_t reconnection_attempts;                                 // Number of recovery attempts before giving up (defaults to UINT32_MAX)
        uint32_t retry_interval_ms;                                     // Interval in ms between connection/recovery attempts (defaults to 3000)
        uint32_t send_timeout_ms;                                       // Timeout in ms before failing sends (defaults to 3000)
        uint32_t poll_timeout_ms;                                       // Timeout in ms before giving up polling (defaults to 100)
        size_t buffer_size;                                             // Incoming data buffer size (defaults to 1024)
        uint32_t stacksize;                                             // Task stack size (defaults to 3072)
        uint32_t queuesize;                                             // Task queue size (defaults to 3)
        uint32_t priority;                                              // Task priority (defaults to 1)
        const char *name;                                               // Task name (defaults to NULL)
    } aos_ws_client_config_t;

    /**
     * @brief Allocate a new Websocket client
     *
     * @param config Configuration
     * @return aos_task_t* Websocket client task
     */
    aos_task_t *aos_ws_client_alloc(aos_ws_client_config_t *config);

    /**
     * @brief Free Websocket client
     *
     * @param task Websocket client task
     */
    void aos_ws_client_free(aos_task_t *task);

    AOS_DECLARE(aos_ws_client_connect, uint8_t out_err)
    /**
     * @brief Connect
     *
     * @param client Websocket client task instance
     * @param future Future
     * @param out_err (future args) 0 if success, fail otherwise
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_ws_client_connect(aos_task_t *client, aos_future_t *future);

    AOS_DECLARE(aos_ws_client_disconnect)
    /**
     * @brief Disconnect
     *
     * @param client Websocket client task instance
     * @param future Future
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_ws_client_disconnect(aos_task_t *client, aos_future_t *future);

    AOS_DECLARE(aos_ws_client_send_text, char *in_data, uint8_t out_err)
    /**
     * @brief Send text data
     *
     * @warning in_data will be temporarily and non-permanently manipulated before being sent.
     * Ensure it stays accessible from the websocket task until the future is resolved (allocate dynamically when in doubt).
     *
     * @param client Websocket client instance
     * @param future Future
     * @param in_data (future args) Dynamically allocated text to be sent
     * @param out_err (future args) 0 on success, other on fail
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_ws_client_send_text(aos_task_t *client, aos_future_t *future);

    AOS_DECLARE(aos_ws_client_send_binary, void *in_data, size_t in_data_len, uint8_t out_err)
    /**
     * @brief Send binary data
     *
     * @warning in_data will be temporarily and non-permanently manipulated before being sent.
     * Ensure it stays accessible from the websocket task until the future is resolved (allocate dynamically when in doubt).
     *
     * @param client Websocket client instance
     * @param future Future
     * @param in_data (future args) Dynamically allocated text to be sent
     * @param out_err (future args) 0 on success, other on fail
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_ws_client_send_binary(aos_task_t *client, aos_future_t *future);

#ifdef __cplusplus
}
#endif