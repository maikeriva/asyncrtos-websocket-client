#include <aos_ws_client.h>
#include <aos_wifi_client.h>
#include <test_macros.h>
#include <unity.h>
#include <unity_test_runner.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_netif.h>
#include <esp_tls.h>

static bool _isinit = false;
static const char *_test_ssid = "MY_SSID";
static const char *_test_password = "MY_PASSWORD";
static const char *_test_host = "ws.postman-echo.com";
extern const uint8_t server_root_cert_pem_start[] asm("_binary_postman_echo_com_pem_start");
extern const uint8_t server_root_cert_pem_end[] asm("_binary_postman_echo_com_pem_end");

static void test_ws_ondata(const void *data, size_t data_len)
{
    printf("Received data: %.*s\n", data_len, (char *)data);
}

static void test_ws_eventhandler(aos_ws_client_event_t event, void *args)
{
    switch (event)
    {
    case AOS_WS_CLIENT_EVENT_DISCONNECTED:
    {
        printf("Websocket client disconnected\n");
        break;
    }
    case AOS_WS_CLIENT_EVENT_RECONNECTING:
    {
        printf("Websocket client reconnecting\n");
        break;
    }
    case AOS_WS_CLIENT_EVENT_RECONNECTED:
    {
        printf("Websocket client reconnected\n");
        break;
    }
    }
}

static void test_wifi_handler(aos_wifi_client_event_t event, void *args)
{
    switch (event)
    {
    case AOS_WIFI_CLIENT_EVENT_DISCONNECTED:
    {
        printf("WiFi client disconnected\n");
        break;
    }
    case AOS_WIFI_CLIENT_EVENT_RECONNECTING:
    {
        printf("WiFi client reconnecting\n");
        break;
    }
    case AOS_WIFI_CLIENT_EVENT_RECONNECTED:
    {
        printf("WiFi client reconnected\n");
        break;
    }
    }
}

static void test_init()
{
    if (!_isinit)
    {
        ESP_ERROR_CHECK(esp_netif_init());

        ESP_ERROR_CHECK(esp_tls_set_global_ca_store(server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start));
    }
    _isinit = true;

    aos_wifi_client_config_t config = {
        .connection_attempts = 3,
        .reconnection_attempts = UINT8_MAX,
        .event_handler = test_wifi_handler};
    aos_wifi_client_init(&config);

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_connect(connect))));
    AOS_ARGS_T(aos_wifi_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);
}

TEST_CASE("Alloc/dealloc", "[wsclient]")
{
    TEST_HEAP_START
    test_init();

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .host = _test_host};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);
    aos_ws_client_free(client);

    TEST_HEAP_STOP
}

TEST_CASE("Start/stop", "[wsclient]")
{
    test_init();
    TEST_HEAP_START

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .host = _test_host};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);

    aos_future_t *start = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_start(client, start))));
    aos_awaitable_free(start);

    aos_future_t *stop = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_stop(client, stop))));
    aos_awaitable_free(stop);

    aos_ws_client_free(client);

    TEST_HEAP_STOP
}

TEST_CASE("Connect/disconnect", "[wsclient]")
{
    test_init();

    TEST_HEAP_START

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .mode = AOS_WS_CLIENT_MODE_SECURE_TEST,
        .host = _test_host,
        .path = "/raw"};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);

    aos_future_t *start = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_start(client, start))));
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_ws_client_connect)(0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_connect(client, connect))));
    AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);

    aos_future_t *stop = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_stop(client, stop))));
    aos_awaitable_free(stop);

    aos_ws_client_free(client);

    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_HEAP_STOP
}

TEST_CASE("Connect/sendtext/disconnect", "[wsclient]")
{
    test_init();

    TEST_HEAP_START

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .mode = AOS_WS_CLIENT_MODE_SECURE_TEST,
        .host = _test_host,
        .path = "/raw"};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);

    aos_future_t *start = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_start(client, start))));
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_ws_client_connect)(0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_connect(client, connect))));
    AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);

    char *data = strdup("Hello world");
    TEST_ASSERT_NOT_NULL(data);
    aos_future_t *send = AOS_AWAITABLE_ALLOC_T(aos_ws_client_send_text)(data, 0);
    TEST_ASSERT_NOT_NULL(send);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_send_text(client, send))));
    AOS_ARGS_T(aos_ws_client_send_text) *send_args = aos_args_get(send);
    TEST_ASSERT_EQUAL(0, send_args->out_err);
    aos_awaitable_free(send);
    free(data);

    // Wait for response
    vTaskDelay(pdMS_TO_TICKS(300));

    aos_future_t *stop = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_stop(client, stop))));
    aos_awaitable_free(stop);

    aos_ws_client_free(client);

    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_HEAP_STOP
}

TEST_CASE("Connect/sendraw/disconnect", "[wsclient]")
{
    test_init();

    TEST_HEAP_START

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .mode = AOS_WS_CLIENT_MODE_SECURE_TEST,
        .host = _test_host,
        .path = "/raw"};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);

    aos_future_t *start = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_start(client, start))));
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_ws_client_connect)(0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_connect(client, connect))));
    AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);

    char *data = strdup("Hello world");
    TEST_ASSERT_NOT_NULL(data);
    aos_future_t *send = AOS_AWAITABLE_ALLOC_T(aos_ws_client_send_binary)(data, strlen(data) + 1, 0);
    TEST_ASSERT_NOT_NULL(send);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_send_binary(client, send))));
    AOS_ARGS_T(aos_ws_client_send_binary) *send_args = aos_args_get(send);
    TEST_ASSERT_EQUAL(0, send_args->out_err);
    aos_awaitable_free(send);
    free(data);

    // Wait for response
    vTaskDelay(pdMS_TO_TICKS(300));

    aos_future_t *stop = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_stop(client, stop))));
    aos_awaitable_free(stop);

    aos_ws_client_free(client);

    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_HEAP_STOP
}

TEST_CASE("Connect / wait for press / disconnect", "[wsclient]")
{
    test_init();

    TEST_HEAP_START

    aos_ws_client_config_t config = {
        .on_data = test_ws_ondata,
        .event_handler = test_ws_eventhandler,
        .mode = AOS_WS_CLIENT_MODE_SECURE_TEST,
        .host = _test_host,
        .path = "/raw"};
    aos_task_t *client = aos_ws_client_alloc(&config);
    TEST_ASSERT_NOT_NULL(client);

    aos_future_t *start = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_start(client, start))));
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_ws_client_connect)(0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_ws_client_connect(client, connect))));
    AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);

    printf("Press q when satisfied\n");
    while (fgetc(stdin) != 'q')
        ;

    aos_future_t *stop = aos_awaitable_alloc(0);
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_task_stop(client, stop))));
    aos_awaitable_free(stop);

    aos_ws_client_free(client);

    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_HEAP_STOP
}
