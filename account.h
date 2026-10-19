#ifndef ACCOUNT_H
#define ACCOUNT_H
#include <sqlite3.h>
#define USERNAME_SIZE 32
#define PASSWORD_SIZE 64
struct Account { sqlite3_int64 id; char username[USERNAME_SIZE]; int rating; };
int valid_username(const char *text);
int valid_password(const char *text);
/* 1 success, 0 duplicate/invalid credentials, -1 storage/crypto failure. */
int register_account(sqlite3 *db, const char *username, const char *password);
int authenticate_account(sqlite3 *db, const char *username, const char *password,
                         struct Account *account);
#endif
