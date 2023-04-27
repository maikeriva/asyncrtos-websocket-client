/**
 * @file main.c
 * @author Michele Riva (micheleriva@protonmail.com)
 * @brief AsyncRTOS Websocket client example 0
 * @version 0.9.0
 * @date 2023-04-17
 *
 * @copyright Copyright (c) 2023
 *
 * This example shows basic usage of the websocket client
 */
#include <stdio.h>
#include <aos.h>
#include <aos_wifi_client.h>
#include <aos_ws_client.h>
#include <esp_netif.h>
#include <esp_tls.h>

static const char *_ssid = "MY_SSID";
static const char *_password = "MY_PASSWORD";
static const char *_ws_host = "ws.postman-echo.com";
extern const uint8_t server_root_cert_pem_start[] asm("_binary_postman_echo_com_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_postman_echo_com_pem_end");

static void wifi_event_handler(aos_wifi_client_event_t event, void *args)
{
    printf("Received WiFi event (%d)\n",event);
}

static void ws_event_handler(aos_ws_client_event_t event, void *args)
{
    printf("Received Websocket event (%d)\n",event);
}

static void ws_on_data(const void *data, size_t data_len)
{
    printf("Received Websocket data: %.*s\n", data_len, (char *)data);
}

void app_main(void)
{
    // Initialize ESP netif and global CA store
    esp_netif_init();
    esp_tls_set_global_ca_store(server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start);

    // Initialize AOS WiFi client
    // All fields are mandatory, we want to be explicit
    aos_wifi_client_config_t wifi_config = {
        .connection_attempts = UINT32_MAX,
        .reconnection_attempts = UINT32_MAX,
        .event_handler = wifi_event_handler};
    aos_wifi_client_init(&wifi_config);

    // Initialize AOS Websocket client
    aos_ws_client_config_t ws_config = {
        .connection_attempts = UINT32_MAX,
        .reconnection_attempts = UINT32_MAX,
        .event_handler = ws_event_handler,
        .on_data = ws_on_data,
        .host = _ws_host,
        .path = "/raw"};
    aos_task_t *ws_task = aos_ws_client_alloc(&ws_config);

    // Start the wifi client for example with an awaitable future
    aos_future_t *wifi_start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    aos_await(aos_wifi_client_start(wifi_start));
    aos_awaitable_free(wifi_start);

    // Start the websocket client for example with an awaitable future
    aos_future_t *ws_start = aos_awaitable_alloc(0);
    aos_await(aos_task_start(ws_task, ws_start));
    aos_awaitable_free(ws_start);

    // Connect to network
    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_ssid, _password, 0);
    aos_await(aos_wifi_client_connect(connect));
    aos_awaitable_free(connect);

    // Connect to websocket
    aos_future_t *ws_connect = AOS_AWAITABLE_ALLOC_T(aos_ws_client_connect)(0);
    aos_await(aos_ws_client_connect(ws_task, ws_connect));
    aos_awaitable_free(ws_connect);

    // Send some text through websocket
    char *text = strdup("Hello");
    aos_future_t *ws_send = AOS_AWAITABLE_ALLOC_T(aos_ws_client_send_text)(text,0);
    aos_await(aos_ws_client_send_text(ws_task, ws_send));
    aos_awaitable_free(ws_send);
    free(text);
}
