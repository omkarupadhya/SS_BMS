#define _POSIX_C_SOURCE 200809L
#include "database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#define PORT 8080
#define LINE_SIZE 512

static int read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;

    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);

        if (r <= 0)
            return (int)r;

        if (c == '\n')
            break;

        if (c != '\r')
            buf[n++] = c;
    }

    buf[n] = '\0';
    return (int)n;
}

static int send_line(int fd, const char *s) {
    char buf[LINE_SIZE];
    int n = snprintf(buf, sizeof(buf), "%s\n", s);

    if (n < 0 || n >= (int)sizeof(buf))
        return -1;

    int sent = 0;

    while (sent < n) {
        ssize_t r = send(fd, buf + sent, (size_t)(n - sent), 0);

        if (r < 0 && errno == EINTR)
            continue;

        if (r <= 0)
            return -1;

        sent += (int)r;
    }

    return 0;
}

static int parse_positive_amount(const char *text, long long *amount) {
    char *end = NULL;
    errno = 0;

    long long value = strtoll(text, &end, 10);

    if (!text || !*text || end == text || *end != '\0' ||
        errno == ERANGE || value <= 0)
        return -1;

    *amount = value;
    return 0;
}

static void child_session(int fd) {
    char line[LINE_SIZE];
    Session session = { .id = -1, .role = 0 };
    int logged_in = 0;

    while (read_line(fd, line, sizeof(line)) > 0) {
        char command[32] = "";
        sscanf(line, "%31s", command);

        char response[LINE_SIZE] = "ERR invalid request";

        if (strcmp(command, "AUTH") == 0) {
            char username[128], password[128];
            int role;

            if (sscanf(line, "AUTH %d %127s %127s",
                       &role, username, password) != 3) {
                snprintf(response, sizeof(response),
                         "ERR usage: AUTH role username password");
            } else if (logged_in) {
                snprintf(response, sizeof(response),
                         "ERR already logged in");
            } else {
                int rc = db_login(username, password, role, &session);

                if (rc == 1) {
                    logged_in = 1;
                    snprintf(response, sizeof(response),
                             "OK logged in user=%d role=%d",
                             session.id, session.role);
                } else if (rc == -2) {
                    snprintf(response, sizeof(response), "ERR role mismatch");
                } else if (rc == -3) {
                    snprintf(response, sizeof(response), "ERR account inactive");
                } else if (rc == -4) {
                    snprintf(response, sizeof(response),
                             "ERR user already logged in");
                } else {
                    snprintf(response, sizeof(response), "ERR login failed");
                }
            }

        } else if (strcmp(command, "QUIT") == 0) {
            if (logged_in)
                db_logout(session.id);

            send_line(fd, "OK bye");
            break;

        } else if (!logged_in) {
            snprintf(response, sizeof(response), "ERR login required");

        } else if (strcmp(command, "VIEWLOANS") == 0) {
            if (session.role != 2) {
                snprintf(response, sizeof(response), "ERR employee access only");
            } else if (mgmt_list_assigned_loans(session.id, response,
                                              sizeof(response)) != 0) {
                snprintf(response, sizeof(response), "ERR could not load assigned loans");
            }

        } else if (strcmp(command, "PROCESSLOAN") == 0) {
            long long loan_id = 0;
            char decision[16] = "";

            if (session.role != 2) {
                snprintf(response, sizeof(response), "ERR employee access only");
            } else if (sscanf(line, "PROCESSLOAN %lld %15s",
                              &loan_id, decision) != 2 || loan_id <= 0) {
                snprintf(response, sizeof(response),
                         "ERR usage: PROCESSLOAN loan_id APPROVE|REJECT");
            } else {
                for (size_t j = 0; decision[j] != '\0'; j++)
                    decision[j] = (char)toupper((unsigned char)decision[j]);

                int rc = mgmt_process_loan(session.id, loan_id, decision);
                if (rc == 0)
                    snprintf(response, sizeof(response), "OK loan status updated");
                else if (rc == -2)
                    snprintf(response, sizeof(response), "ERR loan is not pending");
                else if (rc == -3)
                    snprintf(response, sizeof(response),
                             "ERR loan not found or not assigned to you");
                else
                    snprintf(response, sizeof(response), "ERR loan processing failed");
            }

        } else if (strcmp(command, "PASSBOOK") == 0) {
            int customer_id = 0;
            char history[400] = "";

            if (session.role != 2) {
                snprintf(response, sizeof(response), "ERR employee access only");
            } else if (sscanf(line, "PASSBOOK %d", &customer_id) != 1 ||
                       customer_id <= 0) {
                snprintf(response, sizeof(response),
                         "ERR usage: PASSBOOK customer_user_id");
            } else if (db_history(customer_id, history, sizeof(history)) == 0) {
                snprintf(response, sizeof(response), "OK PASSBOOK %.430s", history);
            } else {
                snprintf(response, sizeof(response), "ERR passbook unavailable");
            }

        } else if (strcmp(command, "PENDINGLOANS") == 0) {
            if (session.role != 3) {
                snprintf(response, sizeof(response), "ERR manager access only");
            } else if (db_list_pending_loans(response, sizeof(response)) != 0) {
                snprintf(response, sizeof(response), "ERR could not list pending loans");
            }

        } else if (strcmp(command, "ASSIGNLOAN") == 0) {
            long long loan_id = 0;
            int employee_id = 0;
            if (session.role != 3) {
                snprintf(response, sizeof(response), "ERR manager access only");
            } else if (sscanf(line, "ASSIGNLOAN %lld %d", &loan_id, &employee_id) != 2 || loan_id <= 0 || employee_id <= 0) {
                snprintf(response, sizeof(response), "ERR usage: ASSIGNLOAN loan_id employee_user_id");
            } else {
                int rc = db_assign_loan(session.id, loan_id, employee_id);
                snprintf(response, sizeof(response), rc == 0 ? "OK loan assigned" : rc == -2 ? "ERR employee not found or inactive" : rc == -3 ? "ERR loan not pending or already assigned" : "ERR assignment failed");
            }

        } else if (strcmp(command, "SETACTIVE") == 0) {
            int user_id = 0, active = -1;
            if (session.role != 3) {
                snprintf(response, sizeof(response), "ERR manager access only");
            } else if (sscanf(line, "SETACTIVE %d %d", &user_id, &active) != 2 || user_id <= 0 || (active != 0 && active != 1)) {
                snprintf(response, sizeof(response), "ERR usage: SETACTIVE customer_user_id 0|1");
            } else {
                int rc = db_manager_set_customer_active(session.id, user_id, active);
                snprintf(response, sizeof(response), rc == 0 ? "OK customer status updated" : rc == -2 ? "ERR customer not found" : "ERR manager can only change customer accounts");
            }

        } else if (strcmp(command, "FEEDBACKLIST") == 0) {
            if (session.role != 3) {
                snprintf(response, sizeof(response), "ERR manager access only");
            } else if (db_list_feedback(response, sizeof(response)) != 0) {
                snprintf(response, sizeof(response), "ERR could not list feedback");
            }

        } else if (strcmp(command, "ADDEMP") == 0) {
            char username[64], password[64];
            int new_id = 0;
            if (session.role != 4) {
                snprintf(response, sizeof(response), "ERR admin access only");
            } else if (sscanf(line, "ADDEMP %63s %63s", username, password) != 2) {
                snprintf(response, sizeof(response), "ERR usage: ADDEMP username password");
            } else {
                int rc = db_admin_create_employee(session.id, username, password, &new_id);
                snprintf(response, sizeof(response), rc == 0 ? "OK employee created user_id=%d" : rc == -2 ? "ERR username already exists" : "ERR employee creation failed", new_id);
            }

        } else if (strcmp(command, "EDITUSER") == 0) {
            int user_id = 0, role = 0, active = -1;
            char username[64], password[64];
            if (session.role != 4) {
                snprintf(response, sizeof(response), "ERR admin access only");
            } else if (sscanf(line, "EDITUSER %d %63s %63s %d %d", &user_id, username, password, &role, &active) != 5 || user_id <= 0 || role < 1 || role > 4 || (active != 0 && active != 1)) {
                snprintf(response, sizeof(response), "ERR usage: EDITUSER user_id username password role(1-4) active(0|1)");
            } else {
                int rc = db_admin_edit_user(session.id, user_id, username, password, role, active);
                snprintf(response, sizeof(response), rc == 0 ? "OK user updated" : rc == -2 ? "ERR user not found" : rc == -3 ? "ERR username already exists" : "ERR cannot modify this user or invalid details");
            }

        } else if (session.role != 1 &&
                   (strcmp(command, "BALANCE") == 0 ||
                    strcmp(command, "HISTORY") == 0 ||
                    strcmp(command, "DEPOSIT") == 0 ||
                    strcmp(command, "WITHDRAW") == 0 ||
                    strcmp(command, "TRANSFER") == 0 ||
                    strcmp(command, "APPLYLOAN") == 0 ||
                    strcmp(command, "FEEDBACK") == 0)) {
            snprintf(response, sizeof(response), "ERR customer access only");

        } else if (strcmp(command, "BALANCE") == 0) {
            long long balance;

            if (db_balance(session.id, &balance) == 0) {
                snprintf(response, sizeof(response),
                         "OK balance_paise=%lld", balance);
            } else {
                snprintf(response, sizeof(response), "ERR account not found");
            }

        } else if (strcmp(command, "HISTORY") == 0) {
            char history[450];

            if (db_history(session.id, history, sizeof(history)) == 0) {
                snprintf(response, sizeof(response), "OK HISTORY %s", history);
            } else {
                snprintf(response, sizeof(response), "ERR history unavailable");
            }

        } else if (strcmp(command, "DEPOSIT") == 0 ||
                   strcmp(command, "WITHDRAW") == 0) {
            char amount_text[128];
            long long amount;

            if (sscanf(line, "%*s %127s", amount_text) != 1 ||
                parse_positive_amount(amount_text, &amount) != 0) {
                snprintf(response, sizeof(response),
                         "ERR amount must be positive paise");
            } else if (strcmp(command, "DEPOSIT") == 0) {
                int rc = db_deposit(session.id, amount);
                snprintf(response, sizeof(response),
                         rc == 0 ? "OK deposit complete" : "ERR deposit failed");
            } else {
                int rc = db_withdraw(session.id, amount);
                snprintf(response, sizeof(response),
                         rc == 0 ? "OK withdrawal complete" :
                         rc == -2 ? "ERR insufficient funds" :
                                    "ERR withdrawal failed");
            }

        } else if (strcmp(command, "TRANSFER") == 0) {
            char destination[128], amount_text[128];
            long long amount;

            if (sscanf(line, "TRANSFER %127s %127s",
                       destination, amount_text) != 2 ||
                parse_positive_amount(amount_text, &amount) != 0) {
                snprintf(response, sizeof(response),
                         "ERR usage: TRANSFER destination_account_id amount_paise");
            } else {
                int rc = db_transfer(session.id, destination, amount);

                snprintf(response, sizeof(response),
                         rc == 0 ? "OK transfer complete" :
                         rc == -2 ? "ERR insufficient funds" :
                         rc == -3 ? "ERR destination account not found" :
                         rc == -4 ? "ERR cannot transfer to your own account" :
                                    "ERR transfer failed");
            }

        } else if (strcmp(command, "APPLYLOAN") == 0) {
            long long amount;
            char purpose[256] = "";

            if (sscanf(line, "APPLYLOAN %lld %255[^\n]",
                       &amount, purpose) != 2 ||
                amount <= 0 || strlen(purpose) > 180) {
                snprintf(response, sizeof(response),
                         "ERR usage: APPLYLOAN amount_paise purpose");
            } else {
                int rc = db_apply_loan(session.id, amount, purpose);

                snprintf(response, sizeof(response),
                         rc == 0 ? "OK loan application submitted" :
                                   "ERR loan application failed; check purpose text");
            }

        } else if (strcmp(command, "CHANGEPASS") == 0) {
            char old_password[128], new_password[128];

            if (sscanf(line, "CHANGEPASS %127s %127s",
                       old_password, new_password) != 2) {
                snprintf(response, sizeof(response),
                         "ERR usage: CHANGEPASS old_password new_password");
            } else {
                int rc = db_change_password(session.id,
                                            old_password, new_password);

                snprintf(response, sizeof(response),
                         rc == 0 ? "OK password changed" :
                         rc == -2 ? "ERR old password incorrect" :
                                    "ERR password change failed");
            }

        } else if (strcmp(command, "FEEDBACK") == 0) {
            char message[301] = "";

            if (sscanf(line, "FEEDBACK %300[^\n]", message) != 1) {
                snprintf(response, sizeof(response),
                         "ERR usage: FEEDBACK your_message");
            } else {
                int rc = db_submit_feedback(session.id, message);

                snprintf(response, sizeof(response),
                         rc == 0 ? "OK feedback submitted" :
                                   "ERR feedback submission failed; avoid | characters");
            }

        } else if (strcmp(command, "LOGOUT") == 0) {
            db_logout(session.id);
            logged_in = 0;
            session.id = -1;
            session.role = 0;

            snprintf(response, sizeof(response), "OK logged out");

        }



         else {
            snprintf(response, sizeof(response),
                     "ERR commands: BALANCE, HISTORY, DEPOSIT, WITHDRAW, "
                     "TRANSFER, APPLYLOAN, CHANGEPASS, FEEDBACK, VIEWLOANS, "
                      "PROCESSLOAN, PASSBOOK, PENDINGLOANS, ASSIGNLOAN, SETACTIVE, "
                     "FEEDBACKLIST, ADDEMP, EDITUSER, LOGOUT, QUIT");
        }



        if (send_line(fd, response) != 0)
            break;
    }

    if (logged_in)
        db_logout(session.id);

    close(fd);
    _exit(0);
}

static void reap_children(int sig) {
    (void)sig;

    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main(void) {
    signal(SIGCHLD, reap_children);
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 10) < 0) {
        perror("bind/listen");
        close(server_fd);
        return 1;
    }

    printf("Bank server listening on port %d\n", PORT);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
            close(server_fd);
            child_session(client_fd);
        }

        close(client_fd);

        if (pid < 0)
            perror("fork");
    }
}
