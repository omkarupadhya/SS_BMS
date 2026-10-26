#ifndef DATABASE_H
#define DATABASE_H
#include <stddef.h>
typedef struct { int id; int role; } Session;
int db_login(const char *username, const char *password, int requested_role, Session *session);
void db_logout(int user_id);
int db_balance(int user_id, long long *balance);
int db_deposit(int user_id, long long amount);
int db_withdraw(int user_id, long long amount);
int db_history(int user_id, char *buffer, size_t buffer_size);
int db_transfer(int sender_user_id, const char *destination_account_id, long long amount);
int db_apply_loan(int user_id, long long amount, const char *purpose);
int db_change_password(int user_id, const char *old_password, const char *new_password);
int db_submit_feedback(int user_id, const char *message);
int mgmt_list_assigned_loans(int employee_id, char *buffer, size_t buffer_size);
int mgmt_process_loan(int employee_id, long long loan_id, const char *decision);
int db_list_pending_loans(char *buffer, size_t buffer_size);
int db_assign_loan(int manager_id, long long loan_id, int employee_id);
int db_manager_set_customer_active(int manager_id, int customer_id, int active);
int db_list_feedback(char *buffer, size_t buffer_size);
int db_admin_create_employee(int admin_id, const char *username, const char *password, int *new_user_id);
int db_admin_edit_user(int admin_id, int user_id, const char *username, const char *password, int role, int active);
#endif
