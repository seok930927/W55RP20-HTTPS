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
} https_session_t;

/* 초기화 - 부팅 시 Flash에서 로드 */
void https_auth_init(void);

/* 계정 수 반환 (0이면 최초 설정 상태) */
int  https_auth_account_count(void);

/* 계정생성 패스워드 검증 (일치: 1, 불일치: 0) */
int  https_auth_verify_creation_pass(const char *pass);

/* 계정 생성 (성공: 0, 실패: 음수) */
int  https_auth_create_account(const char *user, const char *pass);

/* 계정 삭제 (성공: 0, 실패: 음수) */
int  https_auth_delete_account(const char *user);

/* 로그인 검증 후 세션 토큰 발급 (token_out: HTTPS_SESSION_TOKEN_HEX 크기 버퍼) */
/* 성공: 0, 실패: 음수 */
int  https_auth_login(const char *user, const char *pass, char *token_out);

/* 세션 토큰 검증 (유효: 1, 무효: 0) */
int  https_auth_verify_session(const char *token_hex);

/* 로그아웃 - 세션 무효화 */
void https_auth_logout(const char *token_hex);

/* 계정 목록 조회 (out: HTTPS_MAX_ACCOUNTS 크기 배열, count_out: 실제 개수) */
void https_auth_get_accounts(https_account_t *out, uint8_t *count_out);

#endif /* HTTPSAUTH_H_ */
