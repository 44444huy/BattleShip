#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "account.h"

void initialize_account_store(struct AccountStore *store)
{
    store->count = 0;
}

static int find_account(const struct AccountStore *store,
                        const char *username)
{
    for (int i = 0; i < store->count; i++)
    {
        if (strcmp(store->accounts[i].username, username) == 0)
        {
            return i;
        }
    }

    return -1;
}

int valid_username(const char *username)
{
    size_t length;

    if (username == NULL)
    {
        return 0;
    }

    length = strlen(username);

    if (length == 0 || length >= USERNAME_SIZE)
    {
        return 0;
    }

    for (size_t i = 0; i < length; i++)
    {
        unsigned char character = (unsigned char)username[i];

        if (!isalnum(character) && character != '_')
        {
            return 0;
        }
    }

    return 1;
}

int valid_password(const char *password)
{
    size_t length;

    if (password == NULL)
    {
        return 0;
    }

    length = strlen(password);

    if (length == 0 || length >= PASSWORD_SIZE)
    {
        return 0;
    }

    for (size_t i = 0; i < length; i++)
    {
        if (isspace((unsigned char)password[i]))
        {
            return 0;
        }
    }

    return 1;
}

int load_accounts(struct AccountStore *store, const char *file_name)
{
    FILE *file = fopen(file_name, "r");

    initialize_account_store(store);

    if (file == NULL)
    {
        return errno == ENOENT;
    }

    char line[USERNAME_SIZE + PASSWORD_SIZE + 4];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char username[USERNAME_SIZE];
        char password[PASSWORD_SIZE];
        char extra_character;
        int fields = sscanf(line, "%31s %63s %c",
                            username, password, &extra_character);

        if (fields != 2 ||
            !valid_username(username) ||
            !valid_password(password) ||
            find_account(store, username) != -1 ||
            store->count >= MAX_ACCOUNTS)
        {
            fclose(file);
            initialize_account_store(store);
            return 0;
        }

        strcpy(store->accounts[store->count].username, username);
        strcpy(store->accounts[store->count].password, password);
        store->count++;
    }

    if (ferror(file))
    {
        fclose(file);
        initialize_account_store(store);
        return 0;
    }

    fclose(file);
    return 1;
}

int register_account(struct AccountStore *store,
                     const char *file_name,
                     const char *username,
                     const char *password)
{
    if (find_account(store, username) != -1)
    {
        return REGISTER_ACCOUNT_EXISTS;
    }

    if (store->count >= MAX_ACCOUNTS)
    {
        return REGISTER_ACCOUNT_FULL;
    }

    FILE *file = fopen(file_name, "a");

    if (file == NULL)
    {
        return REGISTER_ACCOUNT_FILE_ERROR;
    }

    int write_failed = fprintf(file, "%s %s\n",
                               username, password) < 0;

    if (fclose(file) != 0)
    {
        write_failed = 1;
    }

    if (write_failed)
    {
        return REGISTER_ACCOUNT_FILE_ERROR;
    }

    strcpy(store->accounts[store->count].username, username);
    strcpy(store->accounts[store->count].password, password);
    store->count++;

    return REGISTER_ACCOUNT_OK;
}

int authenticate_account(const struct AccountStore *store,
                         const char *username,
                         const char *password)
{
    int account_index = find_account(store, username);

    if (account_index == -1)
    {
        return 0;
    }

    return strcmp(store->accounts[account_index].password,
                  password) == 0;
}
