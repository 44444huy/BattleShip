#include <stdio.h>
#include "account.h"

static int failed_tests = 0;

static void check(int condition, const char *test_name)
{
    if (condition)
    {
        printf("PASS: %s\n", test_name);
    }
    else
    {
        printf("FAIL: %s\n", test_name);
        failed_tests++;
    }
}

int main(void)
{
    const char *file_name = "test_accounts.tmp";
    struct AccountStore accounts;
    struct AccountStore loaded_accounts;

    remove(file_name);
    initialize_account_store(&accounts);

    check(accounts.count == 0,
          "Account store starts empty");
    check(valid_username("player_1"),
          "Accept a valid username");
    check(!valid_username("player one"),
          "Reject spaces in username");
    check(valid_password("secret123"),
          "Accept a valid password");
    check(!valid_password("secret 123"),
          "Reject spaces in password");

    check(register_account(&accounts, file_name,
                           "player_1", "secret123") ==
              REGISTER_ACCOUNT_OK,
          "Register a new account");
    check(register_account(&accounts, file_name,
                           "player_1", "another") ==
              REGISTER_ACCOUNT_EXISTS,
          "Reject a duplicate username");
    check(authenticate_account(&accounts,
                               "player_1", "secret123"),
          "Login with the correct password");
    check(!authenticate_account(&accounts,
                                "player_1", "wrong"),
          "Reject an incorrect password");

    check(load_accounts(&loaded_accounts, file_name),
          "Load accounts from file");
    check(loaded_accounts.count == 1 &&
              authenticate_account(&loaded_accounts,
                                   "player_1", "secret123"),
          "Loaded account can log in");

    remove(file_name);

    return failed_tests == 0 ? 0 : 1;
}
