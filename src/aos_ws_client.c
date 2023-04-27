/**
 * @file aos_ws_client.c
 * @author Michele Riva (michele.riva@protonmail.com)
 * @brief AsyncRTOS Websocket client implementation
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
#include <aos_ws_client.h>
#include <esp_transport.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <sdkconfig.h>
#if CONFIG_AOS_WS_CLIENT_LOG_NONE
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#elif CONFIG_AOS_WS_CLIENT_LOG_ERROR
#define LOG_LOCAL_LEVEL ESP_LOG_ERROR
#elif CONFIG_AOS_WS_CLIENT_LOG_WARN
#define LOG_LOCAL_LEVEL ESP_LOG_WARN
#elif CONFIG_AOS_WS_CLIENT_LOG_INFO
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#elif CONFIG_AOS_WS_CLIENT_LOG_DEBUG
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#elif CONFIG_AOS_WS_CLIENT_LOG_VERBOSE
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#endif
#include <esp_log.h>

typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
} _aos_ws_client_state_t;

typedef struct _aos_ws_client_ctx_t
{
    _aos_ws_client_state_t state;
    aos_ws_client_config_t config;
    char *buffer;
    esp_transport_handle_t parent_transport;
    esp_transport_handle_t transport;
    unsigned int connection_attempt;
    unsigned int reconnection_attempt;
    aos_future_t *connect_future;
    aos_task_loop_handle_t *poll_loop;
    aos_task_loop_handle_t *retry_loop;
} _aos_ws_client_ctx_t;

typedef enum
{
    AOS_WS_CLIENT_TASKEVT_CONNECT,
    AOS_WS_CLIENT_TASKEVT_DISCONNECT,
    AOS_WS_CLIENT_TASKEVT_SEND_TEXT,
    AOS_WS_CLIENT_TASKEVT_SEND_BINARY,
} _aos_ws_client_taskevt_t;

static void _aos_ws_client_disconnect(aos_task_t *task);
static void _aos_ws_client_onerror(aos_task_t *task);
static void _aos_ws_client_handler_connect(aos_task_t *task, aos_future_t *future);
static void _aos_ws_client_handler_disconnect(aos_task_t *task, aos_future_t *future);
static void _aos_ws_client_handler_send_text(aos_task_t *task, aos_future_t *future);
static void _aos_ws_client_handler_send_binary(aos_task_t *task, aos_future_t *future);
static void _aos_ws_client_retry_loop(aos_task_t *task);
static void _aos_ws_client_poll_loop(aos_task_t *task);

static const char *_tag = "AOS Websocket client";

aos_task_t *aos_ws_client_alloc(aos_ws_client_config_t *config)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = NULL;
    aos_task_t *task = NULL;
    esp_transport_handle_t parent_transport = NULL;
    esp_transport_handle_t transport = NULL;
    char *buffer = NULL;

    // Verify config
    if (!config->host || !config->event_handler || !config->on_data)
    {
        ESP_LOGE(_tag, "Incomplete configuration (host:%u event_handler:%u on_data:%u)", config->host != NULL, config->event_handler != NULL, config->on_data != NULL);
        goto aos_ws_client_alloc_err;
    }

    // Build complete config
    aos_ws_client_config_t complete_config = {
        .host = config->host,
        .event_handler = config->event_handler,
        .on_data = config->on_data,
        .path = config->path ? config->path : "/",
        .port = config->port ? config->port : 443,
        .mode = config->mode ? config->mode : AOS_WS_CLIENT_MODE_SECURE,
        .subprotocol = config->subprotocol ? config->subprotocol : NULL,
        .user_agent = config->user_agent ? config->user_agent : _tag,
        .headers = config->headers ? config->headers : NULL,
        .server_cert_chain_pem = config->server_cert_chain_pem ? config->server_cert_chain_pem : NULL,
        .client_cert_chain_pem = config->client_cert_chain_pem ? config->client_cert_chain_pem : NULL,
        .client_key_pem = config->client_key_pem ? config->client_key_pem : NULL,
        .connection_attempts = config->connection_attempts ? config->connection_attempts : CONFIG_AOS_WS_CLIENT_CONNECTIONATTEMPTS_DEFAULT,
        .reconnection_attempts = config->reconnection_attempts ? config->reconnection_attempts : CONFIG_AOS_WS_CLIENT_RECONNECTIONATTEMPTS_DEFAULT,
        .retry_interval_ms = config->retry_interval_ms ? config->retry_interval_ms : CONFIG_AOS_WS_CLIENT_RETRYINTERVALMS_DEFAULT,
        .send_timeout_ms = config->send_timeout_ms ? config->send_timeout_ms : CONFIG_AOS_WS_CLIENT_SENDTIMEOUTMS_DEFAULT,
        .poll_timeout_ms = config->poll_timeout_ms ? config->poll_timeout_ms : CONFIG_AOS_WS_CLIENT_POLLINGTIMEOUTMS_DEFAULT,
        .buffer_size = config->buffer_size ? config->buffer_size : CONFIG_AOS_WS_CLIENT_BUFFERSIZE_DEFAULT,
        .stacksize = config->stacksize ? config->stacksize : CONFIG_AOS_WS_CLIENT_TASK_STACKSIZE_DEFAULT,
        .queuesize = config->queuesize ? config->queuesize : CONFIG_AOS_WS_CLIENT_TASK_QUEUESIZE_DEFAULT,
        .priority = config->priority ? config->priority : CONFIG_AOS_WS_CLIENT_TASK_PRIORITY_DEFAULT,
        .name = config->name ? config->name : NULL,
    };

    // Allocate resources
    ctx = calloc(1, sizeof(_aos_ws_client_ctx_t));
    buffer = calloc(complete_config.buffer_size, sizeof(char));
    aos_task_config_t task_config = {
        .stacksize = complete_config.stacksize,
        .queuesize = complete_config.queuesize,
        .priority = complete_config.priority,
        .name = complete_config.name,
        .args = ctx};
    task = aos_task_alloc(&task_config);
    if (!ctx || !buffer || !task)
        goto aos_ws_client_alloc_err;

    // Configure transports
    switch (complete_config.mode)
    {
    case AOS_WS_CLIENT_MODE_SECURE:
    case AOS_WS_CLIENT_MODE_SECURE_TEST:
    {
        ESP_LOGD(_tag, "Setting up SSL transport (port:%u)", complete_config.port);
        parent_transport = esp_transport_ssl_init();
        if (!parent_transport)
            goto aos_ws_client_alloc_err;

        if (complete_config.server_cert_chain_pem)
        {
            esp_transport_ssl_set_cert_data(parent_transport, complete_config.server_cert_chain_pem, strlen(complete_config.server_cert_chain_pem));
        }
        else
        {
            esp_transport_ssl_enable_global_ca_store(parent_transport);
        }

        if (complete_config.client_cert_chain_pem && complete_config.client_key_pem)
        {
            esp_transport_ssl_set_client_cert_data(parent_transport, complete_config.client_cert_chain_pem, strlen(complete_config.client_cert_chain_pem));
            esp_transport_ssl_set_client_key_data(parent_transport, complete_config.client_key_pem, strlen(complete_config.client_key_pem));
        }

        if (complete_config.mode == AOS_WS_CLIENT_MODE_SECURE_TEST)
        {
            esp_transport_ssl_skip_common_name_check(parent_transport);
        }

        break;
    }
    case AOS_WS_CLIENT_MODE_INSECURE:
    {
        ESP_LOGD(_tag, "Setting up TCP transport (port:%u)", complete_config.port);
        parent_transport = esp_transport_tcp_init();
        if (!parent_transport)
            goto aos_ws_client_alloc_err;

        break;
    }
    default:
        goto aos_ws_client_alloc_err;
    }

    transport = esp_transport_ws_init(parent_transport);
    if (!transport)
        goto aos_ws_client_alloc_err;

    /**
     * In the following configuration, we set propagate_control_frames to TRUE
     * because while the esp_transport_ws implementation CAN handle
     * disconnections, close frames, and others, it CANNOT notify a handler of
     * such events, including DISCONNECTIONS!
     * Thus we need to handle that stuff on our own.
     */
    esp_transport_ws_config_t ws_config = {
        .ws_path = complete_config.path,
        .sub_protocol = complete_config.subprotocol,
        .user_agent = complete_config.user_agent,
        .headers = complete_config.headers,
        .propagate_control_frames = true};

    if (esp_transport_ws_set_config(transport, &ws_config) != ESP_OK)
        goto aos_ws_client_alloc_err;
    if (aos_task_handler_set(task, _aos_ws_client_handler_connect, AOS_WS_CLIENT_TASKEVT_CONNECT))
        goto aos_ws_client_alloc_err;
    if (aos_task_handler_set(task, _aos_ws_client_handler_disconnect, AOS_WS_CLIENT_TASKEVT_DISCONNECT))
        goto aos_ws_client_alloc_err;
    if (aos_task_handler_set(task, _aos_ws_client_handler_send_text, AOS_WS_CLIENT_TASKEVT_SEND_TEXT))
        goto aos_ws_client_alloc_err;
    if (aos_task_handler_set(task, _aos_ws_client_handler_send_binary, AOS_WS_CLIENT_TASKEVT_SEND_BINARY))
        goto aos_ws_client_alloc_err;

    // Build context
    ctx->parent_transport = parent_transport;
    ctx->transport = transport;
    ctx->config = complete_config;
    ctx->buffer = buffer;

    return task;

aos_ws_client_alloc_err:
    esp_transport_destroy(transport);
    esp_transport_destroy(parent_transport);
    free(ctx);
    free(buffer);
    aos_task_free(task);
    return NULL;
}

void aos_ws_client_free(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);
    esp_transport_destroy(ctx->transport);
    esp_transport_destroy(ctx->parent_transport);
    free(ctx->buffer);
    free(ctx);
    aos_task_free(task);
}

AOS_DEFINE(aos_ws_client_send_text, char *, uint8_t)
aos_future_t *aos_ws_client_send_text(aos_task_t *client, aos_future_t *future)
{
    return aos_task_send(client, AOS_WS_CLIENT_TASKEVT_SEND_TEXT, future);
}
static void _aos_ws_client_handler_send_text(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    AOS_ARGS_T(aos_ws_client_send_text) *args = aos_args_get(future);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case CONNECTED:
    {
        if (esp_transport_ws_send_raw(ctx->transport, WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN, args->in_data, strlen(args->in_data), ctx->config.send_timeout_ms) < 0)
        {
            ESP_LOGW(_tag, "Could not send text data (errno:%d)", esp_transport_get_errno(ctx->transport));
            _aos_ws_client_onerror(task);
            args->out_err = 1;
            aos_resolve(future);
            break;
        }
        args->out_err = 0;
        aos_resolve(future);
        break;
    }
    case DISCONNECTED:
    case CONNECTING:
    case RECONNECTING:
    {
        args->out_err = 1;
        aos_resolve(future);
        break;
    }
    }
}

AOS_DEFINE(aos_ws_client_send_binary, void *, size_t, uint8_t)
aos_future_t *aos_ws_client_send_binary(aos_task_t *client, aos_future_t *future)
{
    return aos_task_send(client, AOS_WS_CLIENT_TASKEVT_SEND_BINARY, future);
}
static void _aos_ws_client_handler_send_binary(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    AOS_ARGS_T(aos_ws_client_send_binary) *args = aos_args_get(future);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case CONNECTED:
    {
        if (esp_transport_ws_send_raw(ctx->transport, WS_TRANSPORT_OPCODES_BINARY | WS_TRANSPORT_OPCODES_FIN, args->in_data, args->in_data_len, ctx->config.send_timeout_ms) < 0)
        {
            ESP_LOGW(_tag, "Could not send binary data (errno:%d)", esp_transport_get_errno(ctx->transport));
            _aos_ws_client_onerror(task);
            args->out_err = 1;
            aos_resolve(future);
            break;
        }
        args->out_err = 0;
        aos_resolve(future);
        break;
    }
    case DISCONNECTED:
    case CONNECTING:
    case RECONNECTING:
    {
        args->out_err = 1;
        aos_resolve(future);
        break;
    }
    }
}

AOS_DEFINE(aos_ws_client_connect, uint8_t)
aos_future_t *aos_ws_client_connect(aos_task_t *client, aos_future_t *future)
{
    return aos_task_send(client, AOS_WS_CLIENT_TASKEVT_CONNECT, future);
}
static void _aos_ws_client_handler_connect(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    AOS_ARGS_T(aos_ws_client_connect) *args = aos_args_get(future);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case DISCONNECTED:
    case CONNECTING:
    case RECONNECTING:
    {
        // Clean slate
        _aos_ws_client_disconnect(task);

        // Resolve current connect future if any
        if (ctx->connect_future)
        {
            ESP_LOGD(_tag, "Resolving connect_future");
            AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(ctx->connect_future);
            connect_args->out_err = 1;
            aos_resolve(ctx->connect_future);
            ctx->connect_future = NULL;
        }

        // Perform connection attempt
        ctx->connect_future = future;
        ctx->connection_attempt = 0;
        ctx->reconnection_attempt = 0;
        if (esp_transport_connect(
                ctx->transport,
                ctx->config.host,
                ctx->config.port,
                ctx->config.send_timeout_ms) < 0)
        {
            ESP_LOGW(_tag, "Could not connect (errno:%d)", esp_transport_get_errno(ctx->transport));
            _aos_ws_client_onerror(task);
            break;
        }

        // Connected! Set receive loops
        ESP_LOGI(_tag, "Connected");
        ctx->connect_future = NULL;
        ctx->state = CONNECTED;
        args->out_err = 0;
        aos_resolve(future);

        ctx->poll_loop = aos_task_loop_set(task, _aos_ws_client_poll_loop, 1);
        _aos_ws_client_poll_loop(task);

        break;
    }
    case CONNECTED:
    {
        ctx->connection_attempt = 0;
        ctx->reconnection_attempt = 0;
        args->out_err = 0;
        aos_resolve(future);
        break;
    }
    }
}

AOS_DEFINE(aos_ws_client_disconnect)
aos_future_t *aos_ws_client_disconnect(aos_task_t *client, aos_future_t *future)
{
    return aos_task_send(client, AOS_WS_CLIENT_TASKEVT_DISCONNECT, future);
}
static void _aos_ws_client_handler_disconnect(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case CONNECTED:
    case CONNECTING:
    case RECONNECTING:
    {
        // Disconnect
        _aos_ws_client_disconnect(task);

        // Resolve current connect future if any
        if (ctx->connect_future)
        {
            ESP_LOGD(_tag, "Resolving connect_future");
            AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(ctx->connect_future);
            connect_args->out_err = 1;
            aos_resolve(ctx->connect_future);
            ctx->connect_future = NULL;
        }

        ESP_LOGI(_tag, "Disconnected");
        ctx->state = DISCONNECTED;
        aos_resolve(future);
        break;
    }
    case DISCONNECTED:
    {
        aos_resolve(future);
        break;
    }
    }
}

static void _aos_ws_client_poll_loop(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    uint32_t data_len = 0;
    int32_t len = 0;
    do
    {
        ESP_LOGD(_tag, "Reading transport");
        // NOTE: This blocks until config.poll_timeout_ms if no data is received, and the task will be unresponsive in the meantime. Use an appropriate timeout value.
        len = esp_transport_read(ctx->transport, ctx->buffer + data_len, ctx->config.buffer_size - data_len, ctx->config.poll_timeout_ms);
        /**
         * Websocket frame outline:
         * 0                   1                   2                   3
         * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-------+-+-------------+-------------------------------+
         * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
         * |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
         * |N|V|V|V|       |S|             |   (if payload len==126/127)   |
         * | |1|2|3|       |K|             |                               |
         * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
         * |     Extended payload length continued, if payload len == 127  |
         * + - - - - - - - - - - - - - - - +-------------------------------+
         * |                               |Masking-key, if MASK set to 1  |
         * +-------------------------------+-------------------------------+
         * | Masking-key (continued)       |          Payload Data         |
         * +-------------------------------- - - - - - - - - - - - - - - - +
         * :                     Payload Data continued ...                :
         * + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
         * |                     Payload Data continued ...                |
         * +---------------------------------------------------------------+
         */
        if (len < 0)
        {
            ESP_LOGW(_tag, "Error while reading transport (errno:%d)", esp_transport_get_errno(ctx->transport));
            _aos_ws_client_onerror(task);
            return; // Break out of the loop
        }
        data_len += len;
    } while (len && data_len < ctx->config.buffer_size && data_len < esp_transport_ws_get_read_payload_len(ctx->transport));

    ws_transport_opcodes_t opcode = esp_transport_ws_get_read_opcode(ctx->transport);
    switch (opcode)
    {
    case WS_TRANSPORT_OPCODES_CONT:
    case WS_TRANSPORT_OPCODES_TEXT:
    case WS_TRANSPORT_OPCODES_BINARY:
    {
        ctx->config.on_data(ctx->buffer, data_len);
        break;
    }
    case WS_TRANSPORT_OPCODES_PING:
    {
        // Reply with a PONG message. Note that when PING messages are longer than config.buffer_len the PONG response will be truncated as well.
        ESP_LOGD(_tag, "Received ping (%.*s)", ctx->config.buffer_size, ctx->buffer);
        if (esp_transport_ws_send_raw(ctx->transport, WS_TRANSPORT_OPCODES_PONG | WS_TRANSPORT_OPCODES_FIN, ctx->buffer, data_len, ctx->config.send_timeout_ms) < 0)
        {
            ESP_LOGW(_tag, "Error while replying to ping (errno:%d)", esp_transport_get_errno(ctx->transport));
            _aos_ws_client_onerror(task);
        }
        break;
    }
    case WS_TRANSPORT_OPCODES_PONG:
    {
        // We are a client, not a server, thus we ignore this.
        break;
    }
    case WS_TRANSPORT_OPCODES_CLOSE:
    {
        _aos_ws_client_disconnect(task);
        ctx->config.event_handler(AOS_WS_CLIENT_EVENT_DISCONNECTED, NULL);
        break;
    }
    case WS_TRANSPORT_OPCODES_NONE:
    {
        break;
    }
    default:
    {
        // According to RFC6455 we should FAIL the websocket connection in this case
        ESP_LOGW(_tag, "Unknown OPCODE (opcode:%d errno:%d)", opcode, esp_transport_get_errno(ctx->transport));
        _aos_ws_client_onerror(task);
    }
    }
}

static void _aos_ws_client_retry_loop(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    if (esp_transport_connect(
            ctx->transport,
            ctx->config.host,
            ctx->config.port,
            ctx->config.send_timeout_ms) < 0)
    {
        ESP_LOGW(_tag, "Could not connect (errno:%d)", esp_transport_get_errno(ctx->transport));
        _aos_ws_client_onerror(task);
        return;
    }

    ESP_LOGI(_tag, "Connected");
    aos_task_loop_unset(task, ctx->retry_loop);
    ctx->retry_loop = NULL;
    ctx->state = CONNECTED;
    if (ctx->reconnection_attempt)
    {
        ctx->reconnection_attempt = 0;
        ctx->config.event_handler(AOS_WS_CLIENT_EVENT_RECONNECTED, NULL);
    }

    if (ctx->connect_future)
    {
        ESP_LOGD(_tag, "Resolving connect_future");
        AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(ctx->connect_future);
        connect_args->out_err = 0;
        aos_resolve(ctx->connect_future);
        ctx->connect_future = NULL;
    }

    ctx->poll_loop = aos_task_loop_set(task, _aos_ws_client_poll_loop, 1);
    _aos_ws_client_poll_loop(task);
}

static void _aos_ws_client_disconnect(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);
    aos_task_loop_unset(task, ctx->poll_loop);
    ctx->poll_loop = NULL;
    aos_task_loop_unset(task, ctx->retry_loop);
    ctx->retry_loop = NULL;
    switch (ctx->state)
    {
    case DISCONNECTED:
    case CONNECTING:
    case RECONNECTING:
    {
        // Do nothing. Performing operations on a non-open transport is idempotent but takes several seconds to fail.
        break;
    }
    case CONNECTED:
    {
        esp_transport_ws_send_raw(ctx->transport, WS_TRANSPORT_OPCODES_CLOSE | WS_TRANSPORT_OPCODES_FIN, NULL, 0, ctx->config.send_timeout_ms);
        esp_transport_ws_poll_connection_closed(ctx->transport, ctx->config.send_timeout_ms);
        esp_transport_close(ctx->transport);
        break;
    }
    }
}

static void _aos_ws_client_onerror(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_ws_client_ctx_t *ctx = aos_task_args_get(task);

    // Set a clean slate first
    _aos_ws_client_disconnect(task);

    // Are we are attempting connection?
    if (ctx->connect_future)
    {
        // Yes, have we tried enough already?
        if (ctx->connection_attempt >= ctx->config.connection_attempts)
        {
            // Yes, do not try anymore, resolve connect future
            ESP_LOGE(_tag, "Maximum connection attempts reached, giving up (attempts:%u)", ctx->config.connection_attempts);
            _aos_ws_client_disconnect(task);
            ctx->state = DISCONNECTED;
            AOS_ARGS_T(aos_ws_client_connect) *connect_args = aos_args_get(ctx->connect_future);
            connect_args->out_err = 1;
            aos_resolve(ctx->connect_future);
            ctx->connect_future = NULL;
            return;
        }
        // No, try once more
        ctx->connection_attempt++;
        ESP_LOGI(_tag, "New connection attempt in %ums (attempt:%u)", ctx->config.retry_interval_ms, ctx->connection_attempt);
        ctx->retry_loop = aos_task_loop_set(task, _aos_ws_client_retry_loop, ctx->config.retry_interval_ms);
        ctx->state = CONNECTING;
        return;
    }

    // We should try to restore the connection
    ctx->state = RECONNECTING;
    if (!ctx->reconnection_attempt)
    {
        ctx->config.event_handler(AOS_WS_CLIENT_EVENT_RECONNECTING, NULL);
    }
    // Have we tried enough already?
    if (ctx->reconnection_attempt >= ctx->config.reconnection_attempts)
    {
        // Yes, do not try anymore and raise disconnected event
        ESP_LOGE(_tag, "Maximum reconnection attempts reached, giving up (attempts:%u)", ctx->config.reconnection_attempts);
        ctx->state = DISCONNECTED;
        ctx->config.event_handler(AOS_WS_CLIENT_EVENT_DISCONNECTED, NULL);
        return;
    }
    // No, try once more
    ctx->reconnection_attempt++;
    ESP_LOGI(_tag, "New reconnection attempt in %ums (attempt:%u)", ctx->config.retry_interval_ms, ctx->reconnection_attempt);
    ctx->retry_loop = aos_task_loop_set(task, _aos_ws_client_retry_loop, ctx->config.retry_interval_ms);
}
