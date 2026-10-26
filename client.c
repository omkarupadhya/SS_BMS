#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ctype.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

static int send_command(int sock, const char *command,
                        char *response, size_t response_size) {
    size_t len = strlen(command);
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(sock, command + sent, len - sent, 0);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            perror("send");
            return -1;
        }

        sent += (size_t)n;
    }

    size_t used = 0;

    while (used < response_size - 1) {
        char ch;
        ssize_t n = recv(sock, &ch, 1, 0);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            if (n < 0)
                perror("recv");
            else
                printf("Server disconnected.\n");

            return -1;
        }

        if (ch == '\n')
            break;

        response[used++] = ch;
    }

    response[used] = '\0';
    return 0;
}

static void discard_line(void) {
    int ch;

    while ((ch = getchar()) != '\n' && ch != EOF) {}
}

static int read_amount_paise(long long *paise) {
    double rupees;

    printf("Enter amount in rupees: ");

    if (scanf("%lf", &rupees) != 1 ||
        rupees <= 0 || rupees > 90000000000000.0) {
        discard_line();
        printf("Invalid amount.\n");
        return -1;
    }

    discard_line();
    *paise = (long long)(rupees * 100.0 + 0.5);

    if (*paise <= 0) {
        printf("Invalid amount.\n");
        return -1;
    }

    return 0;
}

static int read_text(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);

    if (!fgets(buffer, (int)size, stdin))
        return -1;

    size_t len = strlen(buffer);

    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        discard_line();
        printf("Input is too long.\n");
        return -1;
    }

    if (buffer[0] == '\0') {
        printf("Input cannot be empty.\n");
        return -1;
    }

    if (strchr(buffer, '|')) {
        printf("The character '|' is not allowed.\n");
        return -1;
    }

    return 0;
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server.sin_addr) != 1) {
        fprintf(stderr, "Invalid server address.\n");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected to Banking Management System.\n");
    printf("Roles: 1=Customer, 2=Employee, 3=Manager, 4=Admin\n");

    int role;
    char username[64], password[64];
    char command[BUFFER_SIZE], response[BUFFER_SIZE];

    printf("Enter role: ");

    if (scanf("%d", &role) != 1 || role < 1 || role > 4) {
        printf("Invalid role.\n");
        close(sock);
        return 1;
    }

    printf("Username: ");

    if (scanf("%63s", username) != 1) {
        close(sock);
        return 1;
    }

    printf("Password: ");

    if (scanf("%63s", password) != 1) {
        close(sock);
        return 1;
    }

    discard_line();

    snprintf(command, sizeof(command), "AUTH %d %s %s\n",
             role, username, password);

    if (send_command(sock, command, response, sizeof(response)) < 0) {
        close(sock);
        return 1;
    }

    printf("%s\n", response);

    if (strncmp(response, "OK", 2) != 0) {
        close(sock);
        return 0;
    }

    while (1) {
        int choice;
        long long paise;
        char account_id[64];
        char purpose[181];
        char message[301];
        char old_password[64], new_password[64];

        if (role == 2) {
            printf("\n--- Employee Menu ---\n");
            printf("1. View assigned loans\n");
            printf("2. Approve/reject an assigned loan\n");
            printf("3. View customer passbook\n");
            printf("4. Change password\n");
            printf("5. Logout\n");
            printf("6. Exit\n");
            printf("Choose an option: ");

            if (scanf("%d", &choice) != 1) {
                discard_line();
                printf("Invalid input.\n");
                continue;
            }
            discard_line();

            if (choice == 1) {
                snprintf(command, sizeof(command), "VIEWLOANS\n");

            } else if (choice == 2) {
                char loan_id[32];
                char decision[16];

                if (read_text("Loan ID: ", loan_id, sizeof(loan_id)) != 0)
                    continue;
                if (read_text("Decision (APPROVE/REJECT): ",
                              decision, sizeof(decision)) != 0)
                    continue;

                for (char *p = decision; *p; p++)
                    *p = (char)toupper((unsigned char)*p);

                if (strcmp(decision, "APPROVE") != 0 &&
                    strcmp(decision, "REJECT") != 0) {
                    printf("Decision must be APPROVE or REJECT.\n");
                    continue;
                }

                snprintf(command, sizeof(command),
                         "PROCESSLOAN %s %s\n", loan_id, decision);

            } else if (choice == 3) {
                char customer_id[32];

                if (read_text("Customer user ID: ",
                              customer_id, sizeof(customer_id)) != 0)
                    continue;

                snprintf(command, sizeof(command),
                         "PASSBOOK %s\n", customer_id);

            } else if (choice == 4) {
                printf("Enter current password: ");
                if (scanf("%63s", old_password) != 1) {
                    discard_line();
                    continue;
                }

                printf("Enter new password: ");
                if (scanf("%63s", new_password) != 1) {
                    discard_line();
                    continue;
                }
                discard_line();

                if (strcmp(old_password, new_password) == 0) {
                    printf("New password must differ from the old password.\n");
                    continue;
                }

                snprintf(command, sizeof(command), "CHANGEPASS %s %s\n",
                         old_password, new_password);

            } else if (choice == 5) {
                snprintf(command, sizeof(command), "LOGOUT\n");

            } else if (choice == 6) {
                snprintf(command, sizeof(command), "QUIT\n");

            } else {
                printf("Choose a number from 1 to 6.\n");
                continue;
            }

            if (send_command(sock, command, response, sizeof(response)) < 0)
                break;

            printf("Server: %s\n", response);

            if (choice == 5 || choice == 6)
                break;

            continue;
        }

        if (role == 3) {
            printf("\n--- Manager Menu ---\n1. List unassigned pending loans\n2. Assign loan to employee\n3. Activate/deactivate customer\n4. Review feedback\n5. Change password\n6. Logout\n7. Exit\nChoose: ");
            if (scanf("%d", &choice) != 1) { discard_line(); puts("Invalid input."); continue; }
            discard_line();
            if (choice == 1) snprintf(command,sizeof command,"PENDINGLOANS\n");
            else if (choice == 2) { char loan[32], emp[32]; if(read_text("Loan ID: ",loan,sizeof loan)||read_text("Employee user ID: ",emp,sizeof emp))continue; snprintf(command,sizeof command,"ASSIGNLOAN %s %s\n",loan,emp); }
            else if (choice == 3) { char uid[32], active[8]; if(read_text("Customer user ID: ",uid,sizeof uid)||read_text("Active? enter 1 or 0: ",active,sizeof active))continue; if(strcmp(active,"0")&&strcmp(active,"1")){puts("Enter 1 or 0.");continue;} snprintf(command,sizeof command,"SETACTIVE %s %s\n",uid,active); }
            else if (choice == 4) snprintf(command,sizeof command,"FEEDBACKLIST\n");
            else if (choice == 5) { if(read_text("Current password: ",old_password,sizeof old_password)||read_text("New password: ",new_password,sizeof new_password))continue; snprintf(command,sizeof command,"CHANGEPASS %s %s\n",old_password,new_password); }
            else if (choice == 6) snprintf(command,sizeof command,"LOGOUT\n");
            else if (choice == 7) snprintf(command,sizeof command,"QUIT\n");
            else { puts("Choose 1-7."); continue; }
            if (send_command(sock, command, response, sizeof response) < 0)
                break;
            printf("Server: %s\n", response);
            if (choice == 6 || choice == 7)
                break;
            continue;
        }

        if (role == 4) {
            printf("\n--- Admin Menu ---\n1. Create employee\n2. Edit user details/role/status\n3. Change password\n4. Logout\n5. Exit\nChoose: ");
            if (scanf("%d", &choice) != 1) { discard_line(); puts("Invalid input."); continue; }
            discard_line();
            if (choice == 1) { char uname[64], pass[64]; if(read_text("New employee username (no spaces): ",uname,sizeof uname)||read_text("New employee password (no spaces): ",pass,sizeof pass))continue; snprintf(command,sizeof command,"ADDEMP %s %s\n",uname,pass); }
            else if (choice == 2) { char uid[32], uname[64], pass[64], r[8], a[8]; if(read_text("User ID: ",uid,sizeof uid)||read_text("New username (no spaces): ",uname,sizeof uname)||read_text("New password (no spaces): ",pass,sizeof pass)||read_text("Role 1=customer 2=employee 3=manager 4=admin: ",r,sizeof r)||read_text("Active 1=yes 0=no: ",a,sizeof a))continue; int rr=atoi(r); if(rr<1||rr>4||(strcmp(a,"0")&&strcmp(a,"1"))){puts("Invalid role/status.");continue;} snprintf(command,sizeof command,"EDITUSER %s %s %s %s %s\n",uid,uname,pass,r,a); }
            else if (choice == 3) { if(read_text("Current password: ",old_password,sizeof old_password)||read_text("New password: ",new_password,sizeof new_password))continue; snprintf(command,sizeof command,"CHANGEPASS %s %s\n",old_password,new_password); }
            else if (choice == 4) snprintf(command,sizeof command,"LOGOUT\n");
            else if (choice == 5) snprintf(command,sizeof command,"QUIT\n");
            else { puts("Choose 1-5."); continue; }
            if (send_command(sock, command, response, sizeof response) < 0)
                break;
            printf("Server: %s\n", response);
            if (choice == 4 || choice == 5)
                break;
            continue;
        }

        printf("\n--- Customer Menu ---\n");
        printf("1. Check balance\n");
        printf("2. Deposit money\n");
        printf("3. Withdraw money\n");
        printf("4. Transaction history\n");
        printf("5. Transfer to another account\n");
        printf("6. Apply for a loan\n");
        printf("7. Change password\n");
        printf("8. Submit feedback\n");
        printf("9. Logout\n");
        printf("10. Exit\n");
        printf("Choose an option: ");

        if (scanf("%d", &choice) != 1) {
            discard_line();
            printf("Invalid input.\n");
            continue;
        }

        discard_line();

        if (choice == 1) {
            snprintf(command, sizeof(command), "BALANCE\n");

        } else if (choice == 2 || choice == 3) {
            if (read_amount_paise(&paise) != 0)
                continue;

            snprintf(command, sizeof(command), "%s %lld\n",
                     choice == 2 ? "DEPOSIT" : "WITHDRAW", paise);

        } else if (choice == 4) {
            snprintf(command, sizeof(command), "HISTORY\n");

        } else if (choice == 5) {
            printf("Enter destination account ID: ");

            if (scanf("%63s", account_id) != 1) {
                discard_line();
                printf("Invalid account ID.\n");
                continue;
            }

            discard_line();

            if (read_amount_paise(&paise) != 0)
                continue;

            snprintf(command, sizeof(command), "TRANSFER %s %lld\n",
                     account_id, paise);

        } else if (choice == 6) {
            if (read_amount_paise(&paise) != 0)
                continue;

            if (read_text("Loan purpose: ", purpose, sizeof(purpose)) != 0)
                continue;

            snprintf(command, sizeof(command), "APPLYLOAN %lld %s\n",
                     paise, purpose);

        } else if (choice == 7) {
            printf("Enter current password: ");

            if (scanf("%63s", old_password) != 1) {
                discard_line();
                continue;
            }

            printf("Enter new password: ");

            if (scanf("%63s", new_password) != 1) {
                discard_line();
                continue;
            }

            discard_line();

            if (strcmp(old_password, new_password) == 0) {
                printf("New password must differ from the old password.\n");
                continue;
            }

            snprintf(command, sizeof(command), "CHANGEPASS %s %s\n",
                     old_password, new_password);

        } else if (choice == 8) {
            if (read_text("Your feedback: ", message, sizeof(message)) != 0)
                continue;

            snprintf(command, sizeof(command), "FEEDBACK %s\n", message);

        } else if (choice == 9) {
            snprintf(command, sizeof(command), "LOGOUT\n");

        } else if (choice == 10) {
            snprintf(command, sizeof(command), "QUIT\n");

        } else {
            printf("Choose a number from 1 to 10.\n");
            continue;
        }

        if (send_command(sock, command, response, sizeof(response)) < 0)
            break;

        printf("Server: %s\n", response);

        if (choice == 9 || choice == 10)
            break;
    }

    close(sock);
    printf("Disconnected.\n");
    return 0;
}
