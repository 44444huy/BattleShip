#include <ctype.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "account.h"
#define HASH_ITERATIONS 210000
#define SALT_SIZE 16
#define HASH_SIZE 32

int valid_username(const char *text)
{
    if (!text || !*text || strlen(text) >= USERNAME_SIZE) return 0;
    for (; *text; ++text)
        if (!((*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
              (*text >= '0' && *text <= '9') || *text == '_')) return 0;
    return 1;
}
int valid_password(const char *text)
{
    if (!text || !*text || strlen(text) >= PASSWORD_SIZE) return 0;
    for (; *text; ++text) if (isspace((unsigned char)*text)) return 0;
    return 1;
}
static int hash_password(const char *password, const unsigned char *salt,
                         int iterations, unsigned char *hash)
{
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_SIZE,
                            iterations, EVP_sha256(), HASH_SIZE, hash);
}
int register_account(sqlite3 *db, const char *username, const char *password)
{
    if (!valid_username(username) || !valid_password(password)) return 0;
    unsigned char salt[SALT_SIZE], hash[HASH_SIZE];
    if (RAND_bytes(salt, sizeof(salt)) != 1 ||
        !hash_password(password, salt, HASH_ITERATIONS, hash)) return -1;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "INSERT INTO users(username,salt,password_hash,iterations) VALUES(?,?,?,?)", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, salt, sizeof(salt), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, hash, sizeof(hash), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, HASH_ITERATIONS);
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    OPENSSL_cleanse(hash, sizeof(hash));
    return rc == SQLITE_DONE ? 1 : rc == SQLITE_CONSTRAINT ? 0 : -1;
}
int authenticate_account(sqlite3 *db, const char *username, const char *password,
                         struct Account *account)
{
    if (!valid_username(username) || !valid_password(password)) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id,salt,password_hash,iterations,rating FROM users WHERE username=?", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt), result = 0;
    if (rc == SQLITE_ROW) {
        unsigned char hash[HASH_SIZE];
        int iterations = sqlite3_column_int(stmt, 3);
        if (sqlite3_column_bytes(stmt, 1) != SALT_SIZE || sqlite3_column_bytes(stmt, 2) != HASH_SIZE || iterations < 10000 || iterations > 2000000) result = -1;
        else if (!hash_password(password, sqlite3_column_blob(stmt, 1), iterations, hash)) result = -1;
        else if (CRYPTO_memcmp(hash, sqlite3_column_blob(stmt, 2), sizeof(hash)) == 0) {
            account->id = sqlite3_column_int64(stmt, 0);
            strcpy(account->username, username);
            account->rating = sqlite3_column_int(stmt, 4);
            result = 1;
        }
        OPENSSL_cleanse(hash, sizeof(hash));
    } else if (rc != SQLITE_DONE) result = -1;
    sqlite3_finalize(stmt);
    return result;
}
