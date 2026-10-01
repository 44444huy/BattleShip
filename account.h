#ifndef ACCOUNT_H
#define ACCOUNT_H

#define MAX_ACCOUNTS 100
#define USERNAME_SIZE 32
#define PASSWORD_SIZE 64

#define REGISTER_ACCOUNT_OK 1
#define REGISTER_ACCOUNT_EXISTS 0
#define REGISTER_ACCOUNT_FULL -1
#define REGISTER_ACCOUNT_FILE_ERROR -2

struct Account
{
    char username[USERNAME_SIZE];
    char password[PASSWORD_SIZE];
};

struct AccountStore
{
    struct Account accounts[MAX_ACCOUNTS];
    int count;
};

void initialize_account_store(struct AccountStore *store);
int load_accounts(struct AccountStore *store, const char *file_name);
int register_account(struct AccountStore *store,
                     const char *file_name,
                     const char *username,
                     const char *password);
int authenticate_account(const struct AccountStore *store,
                         const char *username,
                         const char *password);
int valid_username(const char *username);
int valid_password(const char *password);

#endif
