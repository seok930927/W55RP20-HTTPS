#ifndef HTTPSAUTH_H_
#define HTTPSAUTH_H_

#include <stdint.h>

#define HTTPS_MAX_ACCOUNTS        5
#define HTTPS_USER_LEN            32
#define HTTPS_HASH_LEN            32   /* SHA-256 */

#define HTTPS_MAX_SESSIONS        5
#define HTTPS_SESSION_TOKEN_LEN   16   /* raw bytes */
#define HTTPS_SESSION_TOKEN_HEX   33   /* hex string + null */
#define HTTPS_SESSION_TIMEOUT_MS  (30 * 60 * 1000U)

#define HTTPS_AUTH_MAGIC_0  0xAA
#define HTTPS_AUTH_MAGIC_1  0xBB
#define HTTPS_AUTH_MAGIC_2  0xCC
#define HTTPS_AUTH_MAGIC_3  0xDD

typedef struct {
    char    user[HTTPS_USER_LEN];
    uint8_t pass_hash[HTTPS_HASH_LEN];
    uint8_t valid;
} https_account_t;

typedef struct {
    uint8_t         magic[4];
    uint8_t         count;
    https_account_t accounts[HTTPS_MAX_ACCOUNTS];
} https_auth_store_t;

typedef struct {
    uint8_t  token[HTTPS_SESSION_TOKEN_LEN];
    uint32_t created_at;
    uint8_t  valid;
    char     user[HTTPS_USER_LEN];
} https_session_t;

void https_auth_init(void);
int  https_auth_account_count(void);
int  https_auth_verify_creation_pass(const char *pass);
int  https_auth_create_account(const char *user, const char *pass);
int  https_auth_delete_account(const char *user);
int  https_auth_login(const char *user, const char *pass, char *token_out);
int  https_auth_verify_session(const char *token_hex);
void https_auth_logout(const char *token_hex);
void https_auth_get_accounts(https_account_t *out, uint8_t *count_out);
int  https_auth_verify_pass_for_session(const char *token_hex, const char *pass);

#endif /* HTTPSAUTH_H_ */
