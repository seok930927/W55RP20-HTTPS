#include <stdio.h>
#include <string.h>

#include "common.h"
#include "port_common.h"
#include "httpHandler.h"
#include "Web_page.h"
#include "socket.h"
#include "netHandler.h"
#include "SSLInterface.h"
#include "deviceHandler.h"
#include "snmpBuffer.h"
#include "ConfigData.h"
#include "snmpHandler.h"

#define HTTPS_SERVER_PORT 443
#define HTTPS_RX_BUF_SIZE 1024
#define HTTPS_TX_CHUNK_SIZE 512

extern xSemaphoreHandle net_http_webserver_sem;

static const uint8_t https_server_socks[MAX_HTTPSOCK] = {
    SOCK_HTTPSERVER_1,
    SOCK_HTTPSERVER_2,
    SOCK_HTTPSERVER_3
};
static wiz_tls_context https_tls_ctx[MAX_HTTPSOCK];
static uint8_t https_tls_active[MAX_HTTPSOCK] = { FALSE, };
static uint8_t https_response_sent[MAX_HTTPSOCK] = { FALSE, };
static unsigned char https_rx_buf[MAX_HTTPSOCK][HTTPS_RX_BUF_SIZE];
static uint8_t https_last_sock_state[MAX_HTTPSOCK];
static uint32_t https_response_sent_ms[MAX_HTTPSOCK] = { 0, };

static int https_write_all(wiz_tls_context *tls_ctx, const unsigned char *buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        device_wdt_reset();
        int ret = mbedtls_ssl_write(tls_ctx->ssl, buf + sent, len - sent);
        if (ret > 0) {
            sent += (size_t)ret;
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        PRT_SSL("HTTPS write failed ret=-0x%x sent=%u remaining=%u\r\n",
                -ret, (unsigned int)sent, (unsigned int)(len - sent));
        return -1;
    }

    return 0;
}

static int https_send_page(wiz_tls_context *tls_ctx) {
    char header[192];
    size_t body_len = sizeof(_acWeb_page) - 1;
    size_t body_sent = 0;
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=UTF-8\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              (unsigned int)body_len);

    if (header_len <= 0 || header_len >= (int)sizeof(header)) {
        return -1;
    }

    PRT_SSL("HTTPS send header_len=%d body_len=%u\r\n", header_len, (unsigned int)body_len);

    if (https_write_all(tls_ctx, (const unsigned char *)header, (size_t)header_len) < 0) {
        PRT_SSL("HTTPS header send failed\r\n");
        return -1;
    }

    while (body_sent < body_len) {
        size_t chunk_len = body_len - body_sent;
        if (chunk_len > HTTPS_TX_CHUNK_SIZE) {
            chunk_len = HTTPS_TX_CHUNK_SIZE;
        }

        if (https_write_all(tls_ctx, _acWeb_page + body_sent, chunk_len) < 0) {
            PRT_SSL("HTTPS body send failed at offset=%u chunk=%u\r\n",
                    (unsigned int)body_sent, (unsigned int)chunk_len);
            return -1;
        }

        body_sent += chunk_len;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    PRT_SSL("HTTPS page send complete\r\n");
    return 0;
}

static int https_send_sensor_json(wiz_tls_context *tls_ctx) {
    static char json_body[800];
    char header[128];
    int n = 0;

    n += snprintf(json_body + n, sizeof(json_body) - n,
                  "{\"temp\":[%d,%d,%d,%d,%d,%d,%d,%d]",
                  g_snmp_sensor.temperature[0], g_snmp_sensor.temperature[1],
                  g_snmp_sensor.temperature[2], g_snmp_sensor.temperature[3],
                  g_snmp_sensor.temperature[4], g_snmp_sensor.temperature[5],
                  g_snmp_sensor.temperature[6], g_snmp_sensor.temperature[7]);
    n += snprintf(json_body + n, sizeof(json_body) - n,
                  ",\"humid\":[%u,%u,%u,%u,%u,%u,%u,%u]",
                  g_snmp_sensor.humidity[0], g_snmp_sensor.humidity[1],
                  g_snmp_sensor.humidity[2], g_snmp_sensor.humidity[3],
                  g_snmp_sensor.humidity[4], g_snmp_sensor.humidity[5],
                  g_snmp_sensor.humidity[6], g_snmp_sensor.humidity[7]);
    n += snprintf(json_body + n, sizeof(json_body) - n,
                  ",\"alarm\":[%u,%u,%u,%u,%u,%u,%u,%u]",
                  g_snmp_sensor.alarm[0], g_snmp_sensor.alarm[1],
                  g_snmp_sensor.alarm[2], g_snmp_sensor.alarm[3],
                  g_snmp_sensor.alarm[4], g_snmp_sensor.alarm[5],
                  g_snmp_sensor.alarm[6], g_snmp_sensor.alarm[7]);
    n += snprintf(json_body + n, sizeof(json_body) - n,
                  ",\"sensor\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
                  g_snmp_sensor.sensor_status[0],  g_snmp_sensor.sensor_status[1],
                  g_snmp_sensor.sensor_status[2],  g_snmp_sensor.sensor_status[3],
                  g_snmp_sensor.sensor_status[4],  g_snmp_sensor.sensor_status[5],
                  g_snmp_sensor.sensor_status[6],  g_snmp_sensor.sensor_status[7],
                  g_snmp_sensor.sensor_status[8],  g_snmp_sensor.sensor_status[9],
                  g_snmp_sensor.sensor_status[10], g_snmp_sensor.sensor_status[11]);
    n += snprintf(json_body + n, sizeof(json_body) - n,
                  ",\"comm\":{\"status\":%u,\"recv_cs\":%u,\"calc_cs\":%u,\"check\":%u,\"flag\":%u}}",
                  g_snmp_sensor.comm_status,    g_snmp_sensor.recv_checksum,
                  g_snmp_sensor.calc_checksum,  g_snmp_sensor.comm_check,
                  g_snmp_sensor.comm_flag);

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n"
                              "\r\n", n);

    if (https_write_all(tls_ctx, (const unsigned char *)header, (size_t)header_len) < 0) {
        return -1;
    }
    return https_write_all(tls_ctx, (const unsigned char *)json_body, (size_t)n);
}

static int https_send_config_json(wiz_tls_context *tls_ctx) {
    DevConfig *conf = get_DevConfig_pointer();
    char body[192];
    char header[128];
    int n = snprintf(body, sizeof(body),
                     "{\"allowed_ip0\":\"%u.%u.%u.%u\","
                     "\"allowed_ip1\":\"%u.%u.%u.%u\","
                     "\"trap_ip0\":\"%u.%u.%u.%u\","
                     "\"trap_ip1\":\"%u.%u.%u.%u\"}",
                     conf->snmp_option.allowed_ip[0][0], conf->snmp_option.allowed_ip[0][1],
                     conf->snmp_option.allowed_ip[0][2], conf->snmp_option.allowed_ip[0][3],
                     conf->snmp_option.allowed_ip[1][0], conf->snmp_option.allowed_ip[1][1],
                     conf->snmp_option.allowed_ip[1][2], conf->snmp_option.allowed_ip[1][3],
                     conf->snmp_option.trap_ip[0][0], conf->snmp_option.trap_ip[0][1],
                     conf->snmp_option.trap_ip[0][2], conf->snmp_option.trap_ip[0][3],
                     conf->snmp_option.trap_ip[1][0], conf->snmp_option.trap_ip[1][1],
                     conf->snmp_option.trap_ip[1][2], conf->snmp_option.trap_ip[1][3]);
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n);
    if (https_write_all(tls_ctx, (const unsigned char *)header, (size_t)hlen) < 0) {
        return -1;
    }
    return https_write_all(tls_ctx, (const unsigned char *)body, (size_t)n);
}

static int https_handle_config_post(wiz_tls_context *tls_ctx, const char *rx_buf) {
    DevConfig *conf = get_DevConfig_pointer();
    const char *body = strstr(rx_buf, "\r\n\r\n");
    static unsigned char post_extra_buf[256];
    if (body) {
        body += 4;
        if (*body == '\0') {
            int r = mbedtls_ssl_read(tls_ctx->ssl, post_extra_buf, sizeof(post_extra_buf) - 1);
            if (r > 0) {
                post_extra_buf[r] = '\0';
                body = (const char *)post_extra_buf;
            }
        }
        static const char *keys[4] = {
            "\"allowed_ip0\":\"", "\"allowed_ip1\":\"",
            "\"trap_ip0\":\"",    "\"trap_ip1\":\""
        };
        static const size_t klens[4] = { 15, 15, 12, 12 };
        uint8_t changed = 0;
        for (int k = 0; k < 4; k++) {
            const char *p = strstr(body, keys[k]);
            if (p) {
                p += klens[k];
                unsigned int a = 0, b = 0, c = 0, d = 0;
                if (sscanf(p, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                    uint8_t *ip = (k < 2)
                                  ? conf->snmp_option.allowed_ip[k]
                                  : conf->snmp_option.trap_ip[k - 2];
                    ip[0] = (uint8_t)a; ip[1] = (uint8_t)b;
                    ip[2] = (uint8_t)c; ip[3] = (uint8_t)d;
                    changed = 1;
                }
            }
        }
        if (changed) {
            save_DevConfig_to_storage();
            snmp_request_reinit();
        }
    }
    return https_send_config_json(tls_ctx);
}

static void https_close_session(uint8_t sock, wiz_tls_context *tls_ctx, uint8_t *tls_active) {
    if (*tls_active) {
        wiz_tls_close_notify(tls_ctx);
        wiz_tls_deinit(tls_ctx);
        *tls_active = FALSE;
    }

    if (getSn_SR(sock) != SOCK_CLOSED) {
        disconnect(sock);
        close(sock);
    }
}

void http_webserver_task(void *argument) {
    uint8_t i;

    (void)argument;
    memset(https_tls_ctx, 0, sizeof(https_tls_ctx));
    memset(https_rx_buf, 0, sizeof(https_rx_buf));
    for (i = 0; i < MAX_HTTPSOCK; i++) {
        https_last_sock_state[i] = 0xff;
    }

    while (1) {
        device_wdt_reset();

        if (get_net_status() == NET_LINK_DISCONNECTED) {
            for (i = 0; i < MAX_HTTPSOCK; i++) {
                https_close_session(https_server_socks[i], &https_tls_ctx[i], &https_tls_active[i]);
                https_response_sent[i] = FALSE;
            }
            xSemaphoreTake(net_http_webserver_sem, portMAX_DELAY);
        }

        for (i = 0; i < MAX_HTTPSOCK; i++) {
            uint8_t sock = https_server_socks[i];
            uint8_t sock_state = getSn_SR(sock);

            if (sock_state != https_last_sock_state[i]) {
                PRT_SSL("HTTPS socket[%d] state = 0x%02x\r\n", sock, sock_state);
                https_last_sock_state[i] = sock_state;
            }

            switch (sock_state) {
            case SOCK_CLOSED:
                if (socket(sock, Sn_MR_TCP, HTTPS_SERVER_PORT, 0x00) == sock) {
                    PRT_SSL("HTTPS socket[%d] opened on port %d\r\n", sock, HTTPS_SERVER_PORT);
                    listen(sock);
                }
                break;

            case SOCK_INIT:
                listen(sock);
                break;

            case SOCK_ESTABLISHED:
                if (getSn_IR(sock) & Sn_IR_CON) {
                    setSn_IR(sock, Sn_IR_CON);
                }

                if (!https_tls_active[i]) {
                    int socket_fd = sock;

                    if (getSn_RX_RSR(sock) == 0) {
                        break;
                    }

                    memset(&https_tls_ctx[i], 0, sizeof(https_tls_ctx[i]));
                    if (wiz_tls_server_init(&https_tls_ctx[i], &socket_fd) < 0) {
                        wiz_tls_deinit(&https_tls_ctx[i]);
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        break;
                    }

                    if (wiz_tls_server_handshake(&https_tls_ctx[i]) < 0) {
                        wiz_tls_deinit(&https_tls_ctx[i]);
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        break;
                    }

                    https_tls_active[i] = TRUE;
                    https_response_sent[i] = FALSE;
                    break;
                }

                {
                    int ret;

                    if (https_response_sent[i]) {
                        if ((millis() - https_response_sent_ms[i]) >= 2000) {
                            PRT_SSL("HTTPS socket[%d] response flush wait done, closing session\r\n", sock);
                            https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                            https_response_sent[i] = FALSE;
                        }
                        break;
                    }

                    if (getSn_RX_RSR(sock) == 0 &&
                            mbedtls_ssl_check_pending(https_tls_ctx[i].ssl) == 0) {
                        break;
                    }

                    memset(https_rx_buf[i], 0, sizeof(https_rx_buf[i]));
                    ret = mbedtls_ssl_read(https_tls_ctx[i].ssl, https_rx_buf[i], sizeof(https_rx_buf[i]) - 1);
                    if (ret > 0) {
                        const char *req = (const char *)https_rx_buf[i];
                        int send_ok;
                        if (strncmp(req, "GET /api/sensors", 16) == 0) {
                            send_ok = https_send_sensor_json(&https_tls_ctx[i]) >= 0;
                        } else if (strncmp(req, "GET /api/config", 15) == 0) {
                            send_ok = https_send_config_json(&https_tls_ctx[i]) >= 0;
                        } else if (strncmp(req, "POST /api/config", 16) == 0) {
                            send_ok = https_handle_config_post(&https_tls_ctx[i], req) >= 0;
                        } else {
                            send_ok = https_send_page(&https_tls_ctx[i]) >= 0;
                        }
                        if (!send_ok) {
                            PRT_SSL("HTTPS socket[%d] response send failed\r\n", sock);
                            https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        } else {
                            https_response_sent[i] = TRUE;
                            https_response_sent_ms[i] = millis();
                        }
                    } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    } else if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
                        PRT_SSL("HTTPS socket[%d] idle timeout, closing session\r\n", sock);
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    } else if (ret < 0 &&
                               ret != MBEDTLS_ERR_SSL_WANT_READ &&
                               ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                        PRT_SSL("HTTPS socket[%d] read failed: -0x%x\r\n", sock, -ret);
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    }
                }
                break;

            case SOCK_CLOSE_WAIT:
                https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                https_response_sent[i] = FALSE;
                break;

            default:
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
