#include <string.h>
#include <stdio.h>

#include "httpsAuth.h"
#include "storageHandler.h"
#include "deviceHandler.h"
#include "mbedtls/sha256.h"
#include "psa/crypto.h"
#include "FreeRTOS.h"
#include "task.h"
#include "WIZ5XXSR-RP_Debug.h"

/*
    SHA-256("wiznet_w55rp20")
    = 121dda172818ad50bccb032d865a75531b524ad39dbd2ed9099e966bc418dd76
*/
static const uint8_t CREATION_PASS_HASH[HTTPS_HASH_LEN] = {
    0x12, 0x1d, 0xda, 0x17, 0x28, 0x18, 0xad, 0x50,
    0xbc, 0xcb, 0x03, 0x2d, 0x86, 0x5a, 0x75, 0x53,
    0x1b, 0x52, 0x4a, 0xd3, 0x9d, 0xbd, 0x2e, 0xd9,
    0x09, 0x9e, 0x96, 0x6b, 0xc4, 0x18, 0xdd, 0x76
};

static https_auth_store_t s_store;
static https_session_t    s_sessions[HTTPS_MAX_SESSIONS];

static void sha256_str(const char *input, uint8_t *hash_out) {
    mbedtls_sha256((const unsigned char *)input, strlen(input), hash_out, 0);
}

static void token_to_hex(const uint8_t *token, char *hex_out) {
    for (int i = 0; i < HTTPS_SESSION_TOKEN_LEN; i++) {
        snprintf(hex_out + i * 2, 3, "%02x", token[i]);
    }
}

static int hex_to_token(const char *hex, uint8_t *token_out) {
    if (strlen(hex) != HTTPS_SESSION_TOKEN_LEN * 2) {
        return 0;
    }
    for (int i = 0; i < HTTPS_SESSION_TOKEN_LEN; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) {
            return 0;
        }
        token_out[i] = (uint8_t)byte;
    }
    return 1;
}

static void store_load(void) {
    read_storage(STORAGE_AUTH, &s_store, sizeof(s_store));
    if (s_store.magic[0] != HTTPS_AUTH_MAGIC_0 ||
            s_store.magic[1] != HTTPS_AUTH_MAGIC_1 ||
            s_store.magic[2] != HTTPS_AUTH_MAGIC_2 ||
            s_store.magic[3] != HTTPS_AUTH_MAGIC_3) {
        /* 미초기화 상태 - 빈 store로 시작 */
        memset(&s_store, 0x00, sizeof(s_store));
        s_store.magic[0] = HTTPS_AUTH_MAGIC_0;
        s_store.magic[1] = HTTPS_AUTH_MAGIC_1;
        s_store.magic[2] = HTTPS_AUTH_MAGIC_2;
        s_store.magic[3] = HTTPS_AUTH_MAGIC_3;
        s_store.count    = 0;
        PRT_INFO("HTTPS Auth: no accounts found (first boot)\r\n");
    } else {
        PRT_INFO("HTTPS Auth: loaded %d account(s)\r\n", s_store.count);
    }
}

static void store_save(void) {
    write_storage(STORAGE_AUTH, 0, &s_store, sizeof(s_store));
}

void https_auth_init(void) {
    memset(s_sessions, 0x00, sizeof(s_sessions));
    store_load();
}

int https_auth_account_count(void) {
    return (int)s_store.count;
}

int https_auth_verify_creation_pass(const char *pass) {
    uint8_t hash[HTTPS_HASH_LEN];
    sha256_str(pass, hash);
    return (memcmp(hash, CREATION_PASS_HASH, HTTPS_HASH_LEN) == 0) ? 1 : 0;
}

int https_auth_create_account(const char *user, const char *pass) {
    if (s_store.count >= HTTPS_MAX_ACCOUNTS) {
        return -1;
    }
    if (strlen(user) == 0 || strlen(user) >= HTTPS_USER_LEN) {
        return -2;
    }
    if (strlen(pass) == 0) {
        return -3;
    }

    /* 중복 검사 */
    for (int i = 0; i < s_store.count; i++) {
        if (s_store.accounts[i].valid &&
                strncmp(s_store.accounts[i].user, user, HTTPS_USER_LEN) == 0) {
            return -4;
        }
    }

    https_account_t *acc = &s_store.accounts[s_store.count];
    memset(acc, 0x00, sizeof(*acc));
    strncpy(acc->user, user, HTTPS_USER_LEN - 1);
    sha256_str(pass, acc->pass_hash);
    acc->valid = 1;
    s_store.count++;

    store_save();
    PRT_INFO("HTTPS Auth: account '%s' created\r\n", user);
    return 0;
}

int https_auth_delete_account(const char *user) {
    for (int i = 0; i < s_store.count; i++) {
        if (s_store.accounts[i].valid &&
                strncmp(s_store.accounts[i].user, user, HTTPS_USER_LEN) == 0) {
            /* 뒤 항목을 앞으로 당김 */
            for (int j = i; j < s_store.count - 1; j++) {
                s_store.accounts[j] = s_store.accounts[j + 1];
            }
            memset(&s_store.accounts[s_store.count - 1], 0x00, sizeof(https_account_t));
            s_store.count--;
            store_save();
            PRT_INFO("HTTPS Auth: account '%s' deleted\r\n", user);
            return 0;
        }
    }
    return -1;
}

int https_auth_login(const char *user, const char *pass, char *token_out) {
    uint8_t hash[HTTPS_HASH_LEN];
    sha256_str(pass, hash);

    for (int i = 0; i < s_store.count; i++) {
        if (!s_store.accounts[i].valid) {
            continue;
        }
        if (strncmp(s_store.accounts[i].user, user, HTTPS_USER_LEN) != 0) {
            continue;
        }
        if (memcmp(s_store.accounts[i].pass_hash, hash, HTTPS_HASH_LEN) != 0) {
            continue;
        }

        /* 빈 세션 슬롯 찾기 */
        uint32_t now = xTaskGetTickCount();
        for (int j = 0; j < HTTPS_MAX_SESSIONS; j++) {
            if (!s_sessions[j].valid ||
                    (now - s_sessions[j].created_at) >= HTTPS_SESSION_TIMEOUT_MS) {
                /* 랜덤 토큰 생성 */
                psa_generate_random(s_sessions[j].token, HTTPS_SESSION_TOKEN_LEN);
                s_sessions[j].created_at = now;
                s_sessions[j].valid      = 1;
                token_to_hex(s_sessions[j].token, token_out);
                PRT_INFO("HTTPS Auth: user '%s' logged in\r\n", user);
                return 0;
            }
        }
        return -2; /* 세션 슬롯 부족 */
    }
    return -1; /* 인증 실패 */
}

int https_auth_verify_session(const char *token_hex) {
    uint8_t token[HTTPS_SESSION_TOKEN_LEN];
    if (!hex_to_token(token_hex, token)) {
        return 0;
    }

    uint32_t now = xTaskGetTickCount();
    for (int i = 0; i < HTTPS_MAX_SESSIONS; i++) {
        if (!s_sessions[i].valid) {
            continue;
        }
        if ((now - s_sessions[i].created_at) >= HTTPS_SESSION_TIMEOUT_MS) {
            s_sessions[i].valid = 0;
            continue;
        }
        if (memcmp(s_sessions[i].token, token, HTTPS_SESSION_TOKEN_LEN) == 0) {
            return 1;
        }
    }
    return 0;
}

void https_auth_logout(const char *token_hex) {
    uint8_t token[HTTPS_SESSION_TOKEN_LEN];
    if (!hex_to_token(token_hex, token)) {
        return;
    }

    for (int i = 0; i < HTTPS_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid &&
                memcmp(s_sessions[i].token, token, HTTPS_SESSION_TOKEN_LEN) == 0) {
            s_sessions[i].valid = 0;
            PRT_INFO("HTTPS Auth: session logged out\r\n");
            return;
        }
    }
}

void https_auth_get_accounts(https_account_t *out, uint8_t *count_out) {
    memcpy(out, s_store.accounts, sizeof(https_account_t) * HTTPS_MAX_ACCOUNTS);
    *count_out = s_store.count;
}
