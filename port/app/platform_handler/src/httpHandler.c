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

#define HTTPS_SERVER_PORT 443
#define HTTPS_RX_BUF_SIZE 1024
#define HTTPS_TX_CHUNK_SIZE 512

extern xSemaphoreHandle net_http_webserver_sem;

static const uint8_t https_server_socks[MAX_HTTPSOCK] = {
    SOCK_HTTPSERVER_1,
    SOCK_HTTPSERVER_2,
    SOCK_HTTPSERVER_3,
    SOCK_HTTPSERVER_4
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
                        if (https_send_page(&https_tls_ctx[i]) < 0) {
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
