#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "port_common.h"
#include "httpHandler.h"
#include "httpsAuth.h"
#include "Web_page.h"
#include "socket.h"
#include "netHandler.h"
#include "SSLInterface.h"
#include "deviceHandler.h"
#include "snmpBuffer.h"
#include "sensor.h"
#include "ConfigData.h"
#include "snmpHandler.h"

#define HTTPS_SERVER_PORT   443
#define HTTPS_RX_BUF_SIZE   1024
#define HTTPS_TX_CHUNK_SIZE 512
#define HTTPS_HDR_BUF_SIZE  512

extern xSemaphoreHandle net_http_webserver_sem;

static const uint8_t https_server_socks[MAX_HTTPSOCK] = {
    SOCK_HTTPSERVER_1,
    SOCK_HTTPSERVER_2,
    SOCK_HTTPSERVER_3
};
static wiz_tls_context https_tls_ctx[MAX_HTTPSOCK];
static uint8_t https_tls_active[MAX_HTTPSOCK]      = { FALSE, };
static uint8_t https_response_sent[MAX_HTTPSOCK]   = { FALSE, };
static unsigned char https_rx_buf[MAX_HTTPSOCK][HTTPS_RX_BUF_SIZE];
static uint8_t https_last_sock_state[MAX_HTTPSOCK];
static uint32_t https_response_sent_ms[MAX_HTTPSOCK] = { 0, };

/*  -----------------------------------------------------------------------
    HTML page templates (login / setup / account management)
    --------------------------------------------------------------------- */
static const char PAGE_LOGIN[] =
    "<!DOCTYPE html><html><head><meta charset=UTF-8><title>Login</title>"
    "<style>body{font-family:sans-serif;max-width:360px;margin:60px auto}"
    "input{width:100%;padding:8px;margin:4px 0;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#0055aa;color:#fff;border:none;cursor:pointer}"
    ".e{color:red;font-size:.9em}</style></head><body>"
    "<h2>Login</h2>"
    "<form method=post action=/login>"
    "ID: <input name=user autocomplete=username><br>"
    "PW: <input type=password name=pass autocomplete=current-password><br>"
    "<button>Login</button>"
    "</form>"
    "<p class=e>%s</p>"
    "<hr><a href=/setup>\xea\xb3\x84\xec\xa0\x95 \xec\x83\x9d\xec\x84\xb1</a>"
    "</body></html>";

static const char PAGE_SETUP[] =
    "<!DOCTYPE html><html><head><meta charset=UTF-8><title>\xea\xb3\x84\xec\xa0\x95 \xec\x83\x9d\xec\x84\xb1</title>"
    "<style>body{font-family:sans-serif;max-width:360px;margin:60px auto}"
    "input{width:100%;padding:8px;margin:4px 0;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#0055aa;color:#fff;border:none;cursor:pointer}"
    ".e{color:red;font-size:.9em}</style></head><body>"
    "<h2>\xea\xb3\x84\xec\xa0\x95 \xec\x83\x9d\xec\x84\xb1</h2>"
    "<form method=post action=/setup>"
    "\xec\x83\x9d\xec\x84\xb1 PW: <input type=password name=cpass><br>"
    "ID: <input name=user autocomplete=username><br>"
    "PW: <input type=password name=pass autocomplete=new-password><br>"
    "<button>\xec\x83\x9d\xec\x84\xb1</button>"
    "</form>"
    "<p class=e>%s</p>"
    "<a href=/login>\xeb\xa1\x9c\xea\xb7\xb8\xec\x9d\xb8\xec\x9c\xbc\xeb\xa1\x9c \xeb\x8f\x8c\xec\x95\x84\xea\xb0\x80\xea\xb8\xb0</a>"
    "</body></html>";

static const char PAGE_ACCOUNT[] =
    "<!DOCTYPE html><html><head><meta charset=UTF-8><title>\xea\xb3\x84\xec\xa0\x95 \xea\xb4\x80\xeb\xa6\xac</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:60px auto}"
    "input{width:100%;padding:8px;margin:4px 0;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#0055aa;color:#fff;border:none;cursor:pointer}"
    ".del{background:#cc2200}.e{color:red;font-size:.9em}"
    "ul{padding:0}li{list-style:none;padding:4px 0;border-bottom:1px solid #eee}"
    "</style></head><body>"
    "<h2>\xea\xb3\x84\xec\xa0\x95 \xea\xb4\x80\xeb\xa6\xac</h2>"
    "<h3>\xed\x98\x84\xec\x9e\xac \xea\xb3\x84\xec\xa0\x95 (%d/%d)</h3><ul>%s</ul>"
    "<h3>\xea\xb3\x84\xec\xa0\x95 \xec\xb6\x94\xea\xb0\x80</h3>"
    "<form method=post action=/account/add>"
    "\xec\x83\x9d\xec\x84\xb1 PW: <input type=password name=cpass><br>"
    "ID: <input name=user><br>"
    "PW: <input type=password name=pass><br>"
    "<button>\xec\xb6\x94\xea\xb0\x80</button>"
    "</form>"
    "<h3>\xea\xb3\x84\xec\xa0\x95 \xec\x82\xad\xec\xa0\x9c</h3>"
    "<form method=post action=/account/del>"
    "ID: <input name=user><br>"
    "<button class=del>\xec\x82\xad\xec\xa0\x9c</button>"
    "</form>"
    "<p class=e>%s</p>"
    "<hr><a href=/>\xed\x99\x88</a> | <a href=/logout>\xeb\xa1\x9c\xea\xb7\xb8\xec\x95\x84\xec\x9b\x83</a>"
    "</body></html>";

/*  -----------------------------------------------------------------------
    Low-level TLS write (retries WANT_READ/WANT_WRITE)
    --------------------------------------------------------------------- */
static int https_write_all(wiz_tls_context *ctx, const unsigned char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        device_wdt_reset();
        int ret = mbedtls_ssl_write(ctx->ssl, buf + sent, len - sent);
        if (ret > 0) {
            sent += (size_t)ret;
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        return -1;
    }
    return 0;
}

/*  -----------------------------------------------------------------------
    HTTP response helpers
    --------------------------------------------------------------------- */
static int send_html(wiz_tls_context *ctx, const char *body, size_t body_len) {
    char hdr[HTTPS_HDR_BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html; charset=UTF-8\r\n"
                           "Content-Length: %u\r\n"
                           "Connection: close\r\n\r\n",
                           (unsigned int)body_len);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return -1;
    }
    if (https_write_all(ctx, (const unsigned char *)hdr, (size_t)hdr_len) < 0) {
        return -1;
    }
    size_t sent = 0;
    while (sent < body_len) {
        size_t chunk = body_len - sent;
        if (chunk > HTTPS_TX_CHUNK_SIZE) {
            chunk = HTTPS_TX_CHUNK_SIZE;
        }
        if (https_write_all(ctx, (const unsigned char *)body + sent, chunk) < 0) {
            return -1;
        }
        sent += chunk;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0;
}

static int send_redirect(wiz_tls_context *ctx, const char *location) {
    char hdr[HTTPS_HDR_BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
                           "HTTP/1.1 302 Found\r\n"
                           "Location: %s\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n",
                           location);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return -1;
    }
    return https_write_all(ctx, (const unsigned char *)hdr, (size_t)hdr_len);
}

static int send_redirect_with_cookie(wiz_tls_context *ctx, const char *location,
                                     const char *token_hex) {
    char hdr[HTTPS_HDR_BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
                           "HTTP/1.1 302 Found\r\n"
                           "Location: %s\r\n"
                           "Set-Cookie: session=%s; HttpOnly; Secure; SameSite=Strict\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n",
                           location, token_hex);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return -1;
    }
    return https_write_all(ctx, (const unsigned char *)hdr, (size_t)hdr_len);
}

static int send_redirect_clear_cookie(wiz_tls_context *ctx, const char *location) {
    char hdr[HTTPS_HDR_BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
                           "HTTP/1.1 302 Found\r\n"
                           "Location: %s\r\n"
                           "Set-Cookie: session=; HttpOnly; Secure; SameSite=Strict; Max-Age=0\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n",
                           location);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return -1;
    }
    return https_write_all(ctx, (const unsigned char *)hdr, (size_t)hdr_len);
}

/*  -----------------------------------------------------------------------
    Request parsing helpers
    --------------------------------------------------------------------- */
static void parse_cookie(const char *req, char *token_out, size_t token_len) {
    token_out[0] = '\0';
    const char *p = strstr(req, "Cookie:");
    if (!p) {
        return;
    }
    p = strstr(p, "session=");
    if (!p) {
        return;
    }
    p += 8;
    size_t i = 0;
    while (*p && *p != ';' && *p != '\r' && *p != '\n' && i < token_len - 1) {
        token_out[i++] = *p++;
    }
    token_out[i] = '\0';
}

static const char *find_body(const char *req) {
    const char *p = strstr(req, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

static void url_decode(const char *src, char *dst, size_t dst_len) {
    size_t i = 0;
    while (*src && i < dst_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            unsigned int val;
            if (sscanf(src + 1, "%02x", &val) == 1) {
                dst[i++] = (char)val;
                src += 3;
            } else {
                dst[i++] = *src++;
            }
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void get_form_field(const char *body, const char *name,
                           char *out, size_t out_len) {
    out[0] = '\0';
    if (!body) {
        return;
    }
    size_t nlen = strlen(name);
    const char *p = body;
    while (*p) {
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            char raw[256] = {0};
            size_t i = 0;
            while (*p && *p != '&' && i < sizeof(raw) - 1) {
                raw[i++] = *p++;
            }
            raw[i] = '\0';
            url_decode(raw, out, out_len);
            return;
        }
        while (*p && *p != '&') {
            p++;
        }
        if (*p == '&') {
            p++;
        }
    }
}

/*  -----------------------------------------------------------------------
    JSON API handlers (sensor data + SNMP config)
    --------------------------------------------------------------------- */
/*
    Send a single HTTP chunked-encoding chunk:
      <hex-length>\r\n
      <data>
      \r\n
*/
static int https_send_http_chunk(wiz_tls_context *ctx, const char *data, size_t len) {
    char prefix[16];
    int plen = snprintf(prefix, sizeof(prefix), "%X\r\n", (unsigned)len);
    if (https_write_all(ctx, (const unsigned char *)prefix, (size_t)plen) < 0) {
        return -1;
    }
    if (len > 0) {
        if (https_write_all(ctx, (const unsigned char *)data, len) < 0) {
            return -1;
        }
    }
    return https_write_all(ctx, (const unsigned char *)"\r\n", 2);
}

static int https_send_sensor_json(wiz_tls_context *tls_ctx) {
    /*
        Index-based JSON streamed via HTTP chunked transfer-encoding.

        We never build the full body in RAM — each enabled sensor produces
        a small (~150 B) chunk that is sent immediately. The browser
        re-assembles the chunks natively. No Content-Length header.

        Decoded wire shape:
        {
          "sensors":[
            {"index":0,"name":"...","type_id":1,"type":"Temperature",
             "unit":"°C","scale":-1,"value":256,"label":null},
            ...
          ],
          "comm":{"status":..,"recv_cs":..,"calc_cs":..,"check":..,"flag":..}
        }
    */
    char chunk[512];
    int n;
    int first = 1;

    /* Response header — chunked, no Content-Length */
    static const char header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n";
    if (https_write_all(tls_ctx, (const unsigned char *)header,
                        sizeof(header) - 1) < 0) {
        return -1;
    }

    /* Open the JSON object + sensors array */
    if (https_send_http_chunk(tls_ctx, "{\"sensors\":[", 12) < 0) {
        return -1;
    }

    /* One chunk per enabled sensor */
    for (int i = 0; i < SENSOR_MAX; i++) {
        const Sensor *sn = sensor_get((uint8_t)i);
        if (sn == NULL || !sn->enabled) {
            continue;
        }
        const SensorType *t = sensorType_get(sn->type_id);
        if (t == NULL) {
            continue;
        }

        int scale = sensor_effectiveScale((uint8_t)i);
        const char *label = sensor_valueLabel((uint8_t)i);

        if (label) {
            n = snprintf(chunk, sizeof(chunk),
                         "%s{\"index\":%d,\"name\":\"%s\",\"type_id\":%u,"
                         "\"type\":\"%s\",\"unit\":\"%s\",\"scale\":%d,"
                         "\"value\":%ld,\"label\":\"%s\"}",
                         first ? "" : ",",
                         i, sn->name, (unsigned)sn->type_id,
                         t->name, t->unit, scale,
                         (long)sn->value, label);
        } else {
            n = snprintf(chunk, sizeof(chunk),
                         "%s{\"index\":%d,\"name\":\"%s\",\"type_id\":%u,"
                         "\"type\":\"%s\",\"unit\":\"%s\",\"scale\":%d,"
                         "\"value\":%ld,\"label\":null}",
                         first ? "" : ",",
                         i, sn->name, (unsigned)sn->type_id,
                         t->name, t->unit, scale,
                         (long)sn->value);
        }
        if (n <= 0 || n >= (int)sizeof(chunk)) {
            continue;   /* overflow — skip this entry */
        }
        if (https_send_http_chunk(tls_ctx, chunk, (size_t)n) < 0) {
            return -1;
        }
        first = 0;
    }

    /* Close sensors array + comm object */
    uint32_t cs_status, cs_recv, cs_calc, cs_check, cs_flag;
    snmpBuffer_getCommFields(&cs_status, &cs_recv, &cs_calc, &cs_check, &cs_flag);
    n = snprintf(chunk, sizeof(chunk),
                 "],\"comm\":{\"status\":%lu,\"recv_cs\":%lu,\"calc_cs\":%lu,"
                 "\"check\":%lu,\"flag\":%lu}}",
                 (unsigned long)cs_status, (unsigned long)cs_recv,
                 (unsigned long)cs_calc, (unsigned long)cs_check,
                 (unsigned long)cs_flag);
    if (https_send_http_chunk(tls_ctx, chunk, (size_t)n) < 0) {
        return -1;
    }

    /* Terminating zero-length chunk */
    return https_write_all(tls_ctx, (const unsigned char *)"0\r\n\r\n", 5);
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
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Content-Length: %d\r\nConnection: close\r\n\r\n", n);
    if (https_write_all(tls_ctx, (const unsigned char *)header, (size_t)hlen) < 0) {
        return -1;
    }
    return https_write_all(tls_ctx, (const unsigned char *)body, (size_t)n);
}

static int https_handle_config_post(wiz_tls_context *tls_ctx, const char *body) {
    DevConfig *conf = get_DevConfig_pointer();
    static unsigned char post_extra_buf[256];
    const char *actual_body = body;

    if (!actual_body || *actual_body == '\0') {
        int r = mbedtls_ssl_read(tls_ctx->ssl, post_extra_buf, sizeof(post_extra_buf) - 1);
        if (r > 0) {
            post_extra_buf[r] = '\0';
            actual_body = (const char *)post_extra_buf;
        }
    }

    if (actual_body && *actual_body != '\0') {
        static const char *keys[4] = {
            "\"allowed_ip0\":\"", "\"allowed_ip1\":\"",
            "\"trap_ip0\":\"",    "\"trap_ip1\":\""
        };
        static const size_t klens[4] = { 15, 15, 12, 12 };
        uint8_t changed = 0;
        for (int k = 0; k < 4; k++) {
            const char *p = strstr(actual_body, keys[k]);
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

/*  -----------------------------------------------------------------------
    Route handlers
    --------------------------------------------------------------------- */
static void handle_get_root(wiz_tls_context *ctx, const char *session) {
    if (https_auth_account_count() == 0) {
        send_redirect(ctx, "/setup");
        return;
    }
    if (!https_auth_verify_session(session)) {
        send_redirect(ctx, "/login");
        return;
    }
    send_html(ctx, (const char *)_acWeb_page, sizeof(_acWeb_page) - 1);
}

static void handle_get_login(wiz_tls_context *ctx, const char *query) {
    char body[sizeof(PAGE_LOGIN) + 128];
    const char *err = "";
    if (query && strstr(query, "err=1")) {
        err = "\xec\x95\x84\xec\x9d\xb4\xeb\x94\x94 \xeb\x98\x90\xeb\x8a\x94 \xed\x8c\xa8\xec\x8a\xa4\xec\x9b\x8c\xeb\x93\x9c\xea\xb0\x80 \xed\x8b\x80\xeb\xa0\xb8\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.";
    }
    snprintf(body, sizeof(body), PAGE_LOGIN, err);
    send_html(ctx, body, strlen(body));
}

static void handle_post_login(wiz_tls_context *ctx, const char *body_str) {
    char user[HTTPS_USER_LEN] = {0};
    char pass[64]             = {0};
    get_form_field(body_str, "user", user, sizeof(user));
    get_form_field(body_str, "pass", pass, sizeof(pass));

    char token[HTTPS_SESSION_TOKEN_HEX] = {0};
    if (https_auth_login(user, pass, token) == 0) {
        send_redirect_with_cookie(ctx, "/", token);
    } else {
        send_redirect(ctx, "/login?err=1");
    }
}

static void handle_get_setup(wiz_tls_context *ctx, const char *query) {
    char body[sizeof(PAGE_SETUP) + 128];
    const char *err = "";
    if (query) {
        if (strstr(query, "err=1")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x83\x9d\xec\x84\xb1 \xed\x8c\xa8\xec\x8a\xa4\xec\x9b\x8c\xeb\x93\x9c\xea\xb0\x80 \xed\x8b\x80\xeb\xa0\xb8\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=2")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x9d\xb4 \xec\x9d\xb4\xeb\xaf\xb8 5\xea\xb0\x9c\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=3")) {
            err = "\xec\x9d\xb4\xeb\xaf\xb8 \xec\xa1\xb4\xec\x9e\xac\xed\x95\x98\xeb\x8a\x94 \xec\x95\x84\xec\x9d\xb4\xeb\x94\x94\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=4")) {
            err = "\xec\x95\x84\xec\x9d\xb4\xeb\x94\x94 \xeb\x98\x90\xeb\x8a\x94 \xed\x8c\xa8\xec\x8a\xa4\xec\x9b\x8c\xeb\x93\x9c\xeb\xa5\xbc \xec\x9e\x85\xeb\xa0\xa5\xed\x95\x98\xec\x84\xb8\xec\x9a\x94.";
        }
    }
    snprintf(body, sizeof(body), PAGE_SETUP, err);
    send_html(ctx, body, strlen(body));
}

static void handle_post_setup(wiz_tls_context *ctx, const char *body_str) {
    char cpass[64]            = {0};
    char user[HTTPS_USER_LEN] = {0};
    char pass[64]             = {0};
    get_form_field(body_str, "cpass", cpass, sizeof(cpass));
    get_form_field(body_str, "user",  user,  sizeof(user));
    get_form_field(body_str, "pass",  pass,  sizeof(pass));

    if (!https_auth_verify_creation_pass(cpass)) {
        send_redirect(ctx, "/setup?err=1");
        return;
    }
    if (strlen(user) == 0 || strlen(pass) == 0) {
        send_redirect(ctx, "/setup?err=4");
        return;
    }

    int ret = https_auth_create_account(user, pass);
    if (ret == -1) {
        send_redirect(ctx, "/setup?err=2");
    } else if (ret == -4) {
        send_redirect(ctx, "/setup?err=3");
    } else if (ret < 0) {
        send_redirect(ctx, "/setup?err=4");
    } else {
        send_redirect(ctx, "/login");
    }
}

static void handle_get_account(wiz_tls_context *ctx, const char *session, const char *query) {
    if (!https_auth_verify_session(session)) {
        send_redirect(ctx, "/login");
        return;
    }

    https_account_t accs[HTTPS_MAX_ACCOUNTS];
    uint8_t count = 0;
    https_auth_get_accounts(accs, &count);

    char list_buf[256] = {0};
    for (int i = 0; i < count; i++) {
        char item[64];
        snprintf(item, sizeof(item), "<li>%s</li>", accs[i].user);
        strncat(list_buf, item, sizeof(list_buf) - strlen(list_buf) - 1);
    }

    const char *err = "";
    if (query) {
        if (strstr(query, "err=1")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x83\x9d\xec\x84\xb1 \xed\x8c\xa8\xec\x8a\xa4\xec\x9b\x8c\xeb\x93\x9c\xea\xb0\x80 \xed\x8b\x80\xeb\xa0\xb8\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=2")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x9d\xb4 \xec\x9d\xb4\xeb\xaf\xb8 5\xea\xb0\x9c\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=3")) {
            err = "\xec\x9d\xb4\xeb\xaf\xb8 \xec\xa1\xb4\xec\x9e\xac\xed\x95\x98\xeb\x8a\x94 \xec\x95\x84\xec\x9d\xb4\xeb\x94\x94\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "err=4")) {
            err = "\xec\x95\x84\xec\x9d\xb4\xeb\x94\x94 \xeb\x98\x90\xeb\x8a\x94 \xed\x8c\xa8\xec\x8a\xa4\xec\x9b\x8c\xeb\x93\x9c\xeb\xa5\xbc \xec\x9e\x85\xeb\xa0\xa5\xed\x95\x98\xec\x84\xb8\xec\x9a\x94.";
        } else if (strstr(query, "err=5")) {
            err = "\xec\xa1\xb4\xec\x9e\xac\xed\x95\x98\xec\xa7\x80 \xec\x95\x8a\xeb\x8a\x94 \xec\x95\x84\xec\x9d\xb4\xeb\x94\x94\xec\x9e\x85\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "ok=1")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x9d\xb4 \xec\xb6\x94\xea\xb0\x80\xeb\x90\x90\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.";
        } else if (strstr(query, "ok=2")) {
            err = "\xea\xb3\x84\xec\xa0\x95\xec\x9d\xb4 \xec\x82\xad\xec\xa0\x9c\xeb\x90\x90\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4.";
        }
    }

    char body[sizeof(PAGE_ACCOUNT) + 512];
    snprintf(body, sizeof(body), PAGE_ACCOUNT,
             (int)count, HTTPS_MAX_ACCOUNTS, list_buf, err);
    send_html(ctx, body, strlen(body));
}

static void handle_post_account_add(wiz_tls_context *ctx, const char *session,
                                    const char *body_str) {
    if (!https_auth_verify_session(session)) {
        send_redirect(ctx, "/login");
        return;
    }

    char cpass[64]            = {0};
    char user[HTTPS_USER_LEN] = {0};
    char pass[64]             = {0};
    get_form_field(body_str, "cpass", cpass, sizeof(cpass));
    get_form_field(body_str, "user",  user,  sizeof(user));
    get_form_field(body_str, "pass",  pass,  sizeof(pass));

    if (!https_auth_verify_creation_pass(cpass)) {
        send_redirect(ctx, "/account?err=1");
        return;
    }
    if (strlen(user) == 0 || strlen(pass) == 0) {
        send_redirect(ctx, "/account?err=4");
        return;
    }

    int ret = https_auth_create_account(user, pass);
    if (ret == -1) {
        send_redirect(ctx, "/account?err=2");
    } else if (ret == -4) {
        send_redirect(ctx, "/account?err=3");
    } else if (ret < 0) {
        send_redirect(ctx, "/account?err=4");
    } else {
        send_redirect(ctx, "/account?ok=1");
    }
}

static void handle_post_account_del(wiz_tls_context *ctx, const char *session,
                                    const char *body_str) {
    if (!https_auth_verify_session(session)) {
        send_redirect(ctx, "/login");
        return;
    }

    char user[HTTPS_USER_LEN] = {0};
    get_form_field(body_str, "user", user, sizeof(user));

    if (https_auth_delete_account(user) < 0) {
        send_redirect(ctx, "/account?err=5");
    } else {
        send_redirect(ctx, "/account?ok=2");
    }
}

static void handle_get_logout(wiz_tls_context *ctx, const char *session) {
    if (session[0]) {
        https_auth_logout(session);
    }
    send_redirect_clear_cookie(ctx, "/login");
}

/*  -----------------------------------------------------------------------
    JSON helpers for account/verify-pass API
    --------------------------------------------------------------------- */
static void https_send_json_result(wiz_tls_context *ctx, int ok, const char *msg) {
    char body[160];
    int  n    = snprintf(body, sizeof(body), "{\"ok\":%d,\"msg\":\"%s\"}", ok, msg ? msg : "");
    char hdr[256];
    int  hlen = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                         "Content-Length: %d\r\nConnection: close\r\n\r\n",
                         ok ? 200 : 400, ok ? "OK" : "Error", n);
    https_write_all(ctx, (const unsigned char *)hdr, (size_t)hlen);
    https_write_all(ctx, (const unsigned char *)body, (size_t)n);
}

static void json_get_field(const char *json, const char *key,
                           char *out, int outlen) {
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json ? json : "", needle);
    if (!p) {
        return;
    }
    p = strchr(p + strlen(needle), '"');
    if (!p) {
        return;
    }
    p++;
    const char *end = strchr(p, '"');
    if (!end) {
        return;
    }
    int len = (int)(end - p);
    if (len <= 0 || len >= outlen) {
        return;
    }
    strncpy(out, p, (size_t)len);
    out[len] = '\0';
}

static void handle_get_api_accounts(wiz_tls_context *ctx) {
    https_account_t accounts[HTTPS_MAX_ACCOUNTS];
    uint8_t count = 0;
    https_auth_get_accounts(accounts, &count);

    char body[512];
    int  n     = snprintf(body, sizeof(body),
                          "{\"count\":%d,\"max\":%d,\"list\":[", count, HTTPS_MAX_ACCOUNTS);
    int  first = 1;
    for (int i = 0; i < HTTPS_MAX_ACCOUNTS; i++) {
        if (!accounts[i].valid) {
            continue;
        }
        n    += snprintf(body + n, sizeof(body) - (size_t)n,
                         "%s\"%s\"", first ? "" : ",", accounts[i].user);
        first = 0;
    }
    n += snprintf(body + n, sizeof(body) - (size_t)n, "]}");

    char hdr[256];
    int  hlen = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                         "Content-Length: %d\r\nConnection: close\r\n\r\n", n);
    https_write_all(ctx, (const unsigned char *)hdr, (size_t)hlen);
    https_write_all(ctx, (const unsigned char *)body, (size_t)n);
}

static void handle_post_api_verify_pass(wiz_tls_context *ctx,
                                        const char *session, const char *body_str) {
    if (!https_auth_verify_session(session)) {
        https_send_json_result(ctx, 0, "no session");
        return;
    }
    char pass[64] = {0};
    json_get_field(body_str, "pass", pass, sizeof(pass));
    int ok = https_auth_verify_pass_for_session(session, pass);
    https_send_json_result(ctx, ok, ok ? "ok" : "wrong password");
}

static void handle_post_api_account_add(wiz_tls_context *ctx,
                                        const char *session, const char *body_str) {
    if (!https_auth_verify_session(session)) {
        https_send_json_result(ctx, 0, "no session");
        return;
    }
    char cpass[64]             = {0};
    char user[HTTPS_USER_LEN]  = {0};
    char pass[64]              = {0};
    json_get_field(body_str, "cpass", cpass, sizeof(cpass));
    json_get_field(body_str, "user",  user,  sizeof(user));
    json_get_field(body_str, "pass",  pass,  sizeof(pass));

    if (!https_auth_verify_creation_pass(cpass)) {
        https_send_json_result(ctx, 0, "wrong creation password");
        return;
    }
    if (strlen(user) == 0 || strlen(pass) == 0) {
        https_send_json_result(ctx, 0, "empty field");
        return;
    }
    int ret = https_auth_create_account(user, pass);
    if (ret ==  0) {
        https_send_json_result(ctx, 1, "created");
    } else if (ret == -1) {
        https_send_json_result(ctx, 0, "max accounts");
    } else if (ret == -4) {
        https_send_json_result(ctx, 0, "user exists");
    } else {
        https_send_json_result(ctx, 0, "error");
    }
}

static void handle_post_api_account_del(wiz_tls_context *ctx,
                                        const char *session, const char *body_str) {
    if (!https_auth_verify_session(session)) {
        https_send_json_result(ctx, 0, "no session");
        return;
    }
    char user[HTTPS_USER_LEN] = {0};
    json_get_field(body_str, "user", user, sizeof(user));
    if (strlen(user) == 0) {
        https_send_json_result(ctx, 0, "empty user");
        return;
    }
    int ret = https_auth_delete_account(user);
    https_send_json_result(ctx, ret == 0 ? 1 : 0, ret == 0 ? "deleted" : "not found");
}

/*  -----------------------------------------------------------------------
    Request dispatcher
    --------------------------------------------------------------------- */
static void dispatch_request(wiz_tls_context *ctx, const char *req) {
    char method[16]  = {0};
    char path[256]   = {0};
    char full_path[256] = {0};
    char session[HTTPS_SESSION_TOKEN_HEX] = {0};

    sscanf(req, "%15s %255s", method, full_path);

    strncpy(path, full_path, sizeof(path) - 1);
    char *q = strchr(path, '?');
    const char *query = NULL;
    if (q) {
        *q    = '\0';
        query = q + 1;
    }

    parse_cookie(req, session, sizeof(session));
    const char *body = find_body(req);

    PRT_SSL("HTTPS %s %s\r\n", method, full_path);

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0) {
            handle_get_root(ctx, session);
        } else if (strcmp(path, "/login") == 0) {
            handle_get_login(ctx, query);
        } else if (strcmp(path, "/setup") == 0) {
            handle_get_setup(ctx, query);
        } else if (strcmp(path, "/account") == 0) {
            handle_get_account(ctx, session, query);
        } else if (strcmp(path, "/logout") == 0) {
            handle_get_logout(ctx, session);
        } else if (strcmp(path, "/api/sensors") == 0) {
            if (!https_auth_verify_session(session)) {
                send_redirect(ctx, "/login");
            } else {
                https_send_sensor_json(ctx);
            }
        } else if (strcmp(path, "/api/config") == 0) {
            if (!https_auth_verify_session(session)) {
                send_redirect(ctx, "/login");
            } else {
                https_send_config_json(ctx);
            }
        } else if (strcmp(path, "/api/accounts") == 0) {
            if (!https_auth_verify_session(session)) {
                send_redirect(ctx, "/login");
            } else {
                handle_get_api_accounts(ctx);
            }
        } else {
            send_redirect(ctx, "/");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/login") == 0) {
            handle_post_login(ctx, body);
        } else if (strcmp(path, "/setup") == 0) {
            handle_post_setup(ctx, body);
        } else if (strcmp(path, "/account/add") == 0) {
            handle_post_account_add(ctx, session, body);
        } else if (strcmp(path, "/account/del") == 0) {
            handle_post_account_del(ctx, session, body);
        } else if (strcmp(path, "/api/verify-pass") == 0) {
            handle_post_api_verify_pass(ctx, session, body);
        } else if (strcmp(path, "/api/accounts/add") == 0) {
            handle_post_api_account_add(ctx, session, body);
        } else if (strcmp(path, "/api/accounts/del") == 0) {
            handle_post_api_account_del(ctx, session, body);
        } else if (strcmp(path, "/api/config") == 0) {
            if (!https_auth_verify_session(session)) {
                send_redirect(ctx, "/login");
            } else {
                https_handle_config_post(ctx, body);
            }
        } else {
            send_redirect(ctx, "/");
        }
    } else {
        send_redirect(ctx, "/");
    }
}

/*  -----------------------------------------------------------------------
    TLS session teardown
    --------------------------------------------------------------------- */
static void https_close_session(uint8_t sock, wiz_tls_context *ctx, uint8_t *tls_active) {
    if (*tls_active) {
        wiz_tls_close_notify(ctx);
        wiz_tls_deinit(ctx);
        *tls_active = FALSE;
    }
    if (getSn_SR(sock) != SOCK_CLOSED) {
        disconnect(sock);
        close(sock);
    }
}

/*  -----------------------------------------------------------------------
    HTTPS server task
    --------------------------------------------------------------------- */
void http_webserver_task(void *argument) {
    uint8_t i;
    (void)argument;

    https_auth_init();

    memset(https_tls_ctx, 0, sizeof(https_tls_ctx));
    memset(https_rx_buf,  0, sizeof(https_rx_buf));
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
            uint8_t sock       = https_server_socks[i];
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
                    if (getSn_RX_RSR(sock) == 0) {
                        break;
                    }
                    int socket_fd = sock;
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
                    https_tls_active[i]    = TRUE;
                    https_response_sent[i] = FALSE;
                    break;
                }

                if (https_response_sent[i]) {
                    if ((millis() - https_response_sent_ms[i]) >= 2000) {
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    }
                    break;
                }

                if (getSn_RX_RSR(sock) == 0 &&
                        mbedtls_ssl_check_pending(https_tls_ctx[i].ssl) == 0) {
                    break;
                }

                {
                    memset(https_rx_buf[i], 0, sizeof(https_rx_buf[i]));
                    int ret = mbedtls_ssl_read(https_tls_ctx[i].ssl,
                                               https_rx_buf[i],
                                               sizeof(https_rx_buf[i]) - 1);
                    if (ret > 0) {
                        dispatch_request(&https_tls_ctx[i], (const char *)https_rx_buf[i]);
                        https_response_sent[i]    = TRUE;
                        https_response_sent_ms[i] = millis();
                    } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    } else if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
                        https_close_session(sock, &https_tls_ctx[i], &https_tls_active[i]);
                        https_response_sent[i] = FALSE;
                    } else if (ret < 0 &&
                               ret != MBEDTLS_ERR_SSL_WANT_READ &&
                               ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                        PRT_SSL("HTTPS socket[%d] read err: -0x%x\r\n", sock, -ret);
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
