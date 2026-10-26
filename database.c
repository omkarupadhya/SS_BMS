
#define _GNU_SOURCE
#include "database.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define DATA_FILE "bank_data.txt"
#define LOCK_FILE "bank.lock"
#define LINE_SIZE 512
#define MAX_LINES 2000

static int lock_db(void) {
    int fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;

    if (flock(fd, LOCK_EX) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void unlock_db(int fd) {
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static int load_lines(char lines[MAX_LINES][LINE_SIZE], int *count) {
    FILE *fp = fopen(DATA_FILE, "r");
    if (!fp) return -1;

    *count = 0;

    while (*count < MAX_LINES &&
           fgets(lines[*count], LINE_SIZE, fp)) {
        /* Reject lines that exceed the buffer size. */
        size_t len = strlen(lines[*count]);

        if (len > 0 && lines[*count][len - 1] != '\n' &&
            !feof(fp)) {
            fclose(fp);
            return -1;
        }

        lines[*count][strcspn(lines[*count], "\r\n")] = '\0';
        (*count)++;
    }

    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }

    if (*count == MAX_LINES) {
        int ch = fgetc(fp);
        if (ch != EOF) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int save_lines(char lines[MAX_LINES][LINE_SIZE], int count) {
    const char *tmp = "bank_data.tmp";

    int fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return -1;

    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmp);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (fprintf(fp, "%s\n", lines[i]) < 0) {
            fclose(fp);
            unlink(tmp);
            return -1;
        }
    }

    if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }

    if (fclose(fp) != 0) {
        unlink(tmp);
        return -1;
    }

    if (rename(tmp, DATA_FILE) != 0) {
        unlink(tmp);
        return -1;
    }

    int dirfd = open(".", O_RDONLY | O_DIRECTORY);
    if (dirfd >= 0) {
        fsync(dirfd);
        close(dirfd);
    }

    return 0;
}

static int split(char *line, char *fields[], int max_fields) {
    int n = 0;
    char *save = NULL;

    for (char *p = strtok_r(line, "|", &save);
         p && n < max_fields;
         p = strtok_r(NULL, "|", &save)) {
        fields[n++] = p;
    }

    return n;
}

int db_login(const char *username, const char *password,
             int requested_role, Session *session) {
    if (!username || !password || !session) return -1;

    int lockfd = lock_db();
    if (lockfd < 0) return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int result = 0;

    for (int i = 0; i < count; i++) {
        char copy[LINE_SIZE];
        char *f[8];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 8);

        if (n != 7 || strcmp(f[0], "USER") != 0) continue;

        if (strcmp(f[2], username) != 0 ||
            strcmp(f[3], password) != 0) {
            continue;
        }

        int id = atoi(f[1]);
        int role = atoi(f[4]);

        if (role != requested_role) {
            result = -2;
        } else if (atoi(f[5]) != 1) {
            result = -3;
        } else {
            char *end = NULL;
            errno = 0;

            long old_pid = strtol(f[6], &end, 10);

            int pid_valid =
                f[6][0] != '\0' &&
                end != f[6] &&
                *end == '\0' &&
                errno != ERANGE &&
                old_pid > 1 &&
                old_pid <= INT_MAX;

            int process_alive = 0;

            if (pid_valid) {
                errno = 0;

                if (kill((pid_t)old_pid, 0) == 0 ||
                    errno == EPERM) {
                    process_alive = 1;
                }
            }

            if (process_alive) {
                result = -4;
            } else {
                char updated[LINE_SIZE];

                snprintf(updated, sizeof(updated),
                         "USER|%s|%s|%s|%s|%s|%ld",
                         f[1], f[2], f[3], f[4], f[5],
                         (long)getpid());

                snprintf(lines[i], LINE_SIZE, "%s", updated);

                if (save_lines(lines, count) == 0) {
                    session->id = id;
                    session->role = role;
                    result = 1;
                } else {
                    result = -1;
                }
            }
        }

        break;
    }

    unlock_db(lockfd);
    return result;
}

void db_logout(int user_id) {
    int lockfd = lock_db();
    if (lockfd < 0) return;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) == 0) {
        for (int i = 0; i < count; i++) {
            char copy[LINE_SIZE];
            char *f[8];

            snprintf(copy, sizeof(copy), "%s", lines[i]);
            int n = split(copy, f, 8);

            if (n == 7 && strcmp(f[0], "USER") == 0 &&
                atoi(f[1]) == user_id) {
                snprintf(lines[i], LINE_SIZE,
                         "USER|%s|%s|%s|%s|%s|0",
                         f[1], f[2], f[3], f[4], f[5]);
                break;
            }
        }

        save_lines(lines, count);
    }

    unlock_db(lockfd);
}

int db_balance(int user_id, long long *balance) {
    if (!balance) return -1;

    int lockfd = lock_db();
    if (lockfd < 0) return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;
    int result = -1;

    if (load_lines(lines, &count) == 0) {
        for (int i = 0; i < count; i++) {
            char copy[LINE_SIZE];
            char *f[6];

            snprintf(copy, sizeof(copy), "%s", lines[i]);
            int n = split(copy, f, 6);

            if (n == 4 && strcmp(f[0], "ACCOUNT") == 0 &&
                atoi(f[2]) == user_id) {
                *balance = atoll(f[3]);
                result = 0;
                break;
            }
        }
    }

    unlock_db(lockfd);
    return result;
}



static int change_balance(int user_id, long long amount, int deposit) {
    if (amount <= 0) return -1;

    int lockfd = lock_db();
    if (lockfd < 0) return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int result = -1;

    for (int i = 0; i < count; i++) {
        char copy[LINE_SIZE];
        char *f[6];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 6);

        if (n != 4 || strcmp(f[0], "ACCOUNT") != 0 ||
            atoi(f[2]) != user_id) {
            continue;
        }

        long long old_balance = atoll(f[3]);

        if (!deposit && old_balance < amount) {
            result = -2;
            break;
        }

        if (deposit &&
            old_balance > 9000000000000000000LL - amount) {
            result = -1;
            break;
        }

        /*
         * We need one additional line for the transaction record.
         */
        if (count >= MAX_LINES) {
            result = -1;
            break;
        }

        long long new_balance = deposit
            ? old_balance + amount
            : old_balance - amount;

        /*
         * Find the largest transaction ID already in the database.
         * The database lock prevents concurrent updates from choosing
         * the same next ID.
         */
        long long max_txn_id = 0;

        for (int j = 0; j < count; j++) {
            char txn_copy[LINE_SIZE];
            char *tf[8];

            snprintf(txn_copy, sizeof(txn_copy), "%s", lines[j]);
            int tn = split(txn_copy, tf, 8);

            if (tn != 8 || strcmp(tf[0], "TXN") != 0) {
                continue;
            }

            char *end = NULL;
            errno = 0;
            long long txn_id = strtoll(tf[1], &end, 10);

            if (errno == 0 && end != tf[1] &&
                *end == '\0' && txn_id > max_txn_id) {
                max_txn_id = txn_id;
            }
        }

        if (max_txn_id == LLONG_MAX) {
            result = -1;
            break;
        }

        long long next_txn_id = max_txn_id + 1;

        /*
         * Generate a timestamp for the transaction.
         */
        time_t now = time(NULL);
        struct tm time_info;
        char timestamp[32];

        if (now == (time_t)-1 ||
            localtime_r(&now, &time_info) == NULL ||
            strftime(timestamp, sizeof(timestamp),
                     "%Y-%m-%dT%H:%M:%S", &time_info) == 0) {
            result = -1;
            break;
        }

        /*
         * Update the account in memory.
         * No database change is saved yet.
         */
        snprintf(lines[i], LINE_SIZE, "ACCOUNT|%s|%s|%lld",
                 f[1], f[2], new_balance);

        /*
         * Format:
         * TXN|transaction_id|account_id|user_id|type|
         * amount_paise|balance_after_paise|timestamp
         */
        int written = snprintf(
            lines[count], LINE_SIZE,
            "TXN|%lld|%s|%d|%s|%lld|%lld|%s",
            next_txn_id, f[1], user_id,
            deposit ? "DEPOSIT" : "WITHDRAW",
            amount, new_balance, timestamp
        );

        if (written < 0 || written >= LINE_SIZE) {
            result = -1;
            break;
        }

        /*
         * Save both changes together while still holding the lock.
         */
        result = save_lines(lines, count + 1) == 0 ? 0 : -1;
        break;
    }

    unlock_db(lockfd);
    return result;
}

int db_history(int user_id, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return -1;

    buffer[0] = '\0';

    int lockfd = lock_db();
    if (lockfd < 0) return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    size_t used = 0;
    int found = 0;
    int result = 0;

    /*
     * Transaction records are appended to the file.
     * Scanning backwards gives us the newest records first.
     */
    for (int i = count - 1; i >= 0 && found < 5; i--) {
        char copy[LINE_SIZE];
        char *f[8];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 8);

        if (n != 8 || strcmp(f[0], "TXN") != 0 ||
            atoi(f[3]) != user_id) {
            continue;
        }

        long long amount = atoll(f[5]);
        long long balance = atoll(f[6]);

        int written = snprintf(
            buffer + used, buffer_size - used,
            "%s#%s %s amt=%lld.%02lld bal=%lld.%02lld @%s",
            found ? " | " : "",
            f[1], f[4],
            amount / 100, amount % 100,
            balance / 100, balance % 100,
            f[7]
        );

        if (written < 0 ||
            (size_t)written >= buffer_size - used) {
            if (found == 0) {
                result = -1;
            }
            break;
        }

        used += (size_t)written;
        found++;
    }

    if (result == 0 && found == 0) {
        snprintf(buffer, buffer_size, "No transactions yet");
    }

    unlock_db(lockfd);
    return result;
}

int db_deposit(int user_id, long long amount) {
    return change_balance(user_id, amount, 1);
}

int db_withdraw(int user_id, long long amount) {
    return change_balance(user_id, amount, 0);
}

/* ---------- New customer operations ---------- */

/* Find the largest ID for records of a given type. */
static long long next_record_id(char lines[MAX_LINES][LINE_SIZE],
                                int count, const char *type) {
    long long max_id = 0;

    for (int i = 0; i < count; i++) {
        char copy[LINE_SIZE];
        char *f[8];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 8);

        if (n < 2 || strcmp(f[0], type) != 0)
            continue;

        char *end = NULL;
        errno = 0;
        long long id = strtoll(f[1], &end, 10);

        if (errno == 0 && end != f[1] && *end == '\0' &&
            id > max_id)
            max_id = id;
    }

    return max_id == LLONG_MAX ? -1 : max_id + 1;
}

static int make_timestamp(char *out, size_t size) {
    time_t now = time(NULL);
    struct tm tm_now;

    if (now == (time_t)-1 ||
        localtime_r(&now, &tm_now) == NULL ||
        strftime(out, size, "%Y-%m-%dT%H:%M:%S", &tm_now) == 0)
        return -1;

    return 0;
}

static int valid_text(const char *s, size_t max_len) {
    if (!s || !*s || strlen(s) > max_len)
        return 0;

    /* Pipe is the database field separator. */
    return strchr(s, '|') == NULL &&
           strchr(s, '\n') == NULL &&
           strchr(s, '\r') == NULL;
}

/*
 * Transfer from the sender's account to the account whose ID
 * was supplied. Both account changes and both TXN records are
 * saved together while the database lock is held.
 *
 * Return: 0 success, -1 failure, -2 insufficient funds,
 *         -3 destination not found, -4 same account.
 */
int db_transfer(int sender_user_id, const char *destination_account_id,
                long long amount) {
    if (amount <= 0 || !destination_account_id ||
        !*destination_account_id ||
        strchr(destination_account_id, '|'))
        return -1;

    int lockfd = lock_db();
    if (lockfd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int sender_idx = -1, receiver_idx = -1;
    char sender_account[64], receiver_account[64];
    int receiver_user_id = -1;
    long long sender_balance = 0, receiver_balance = 0;

    for (int i = 0; i < count; i++) {
        char copy[LINE_SIZE];
        char *f[6];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 6);

        if (n != 4 || strcmp(f[0], "ACCOUNT") != 0)
            continue;

        if (sender_idx == -1 && atoi(f[2]) == sender_user_id) {
            sender_idx = i;
            snprintf(sender_account, sizeof(sender_account), "%s", f[1]);
            sender_balance = atoll(f[3]);
        }

        if (strcmp(f[1], destination_account_id) == 0) {
            receiver_idx = i;
            snprintf(receiver_account, sizeof(receiver_account), "%s", f[1]);
            receiver_user_id = atoi(f[2]);
            receiver_balance = atoll(f[3]);
        }
    }

    if (sender_idx < 0 || receiver_idx < 0) {
        unlock_db(lockfd);
        return receiver_idx < 0 ? -3 : -1;
    }

    if (sender_idx == receiver_idx) {
        unlock_db(lockfd);
        return -4;
    }

    if (sender_balance < amount) {
        unlock_db(lockfd);
        return -2;
    }

    if (receiver_balance > LLONG_MAX - amount ||
        count > MAX_LINES - 2) {
        unlock_db(lockfd);
        return -1;
    }

    long long txn1 = next_record_id(lines, count, "TXN");
    if (txn1 < 1 || txn1 == LLONG_MAX) {
        unlock_db(lockfd);
        return -1;
    }

    long long txn2 = txn1 + 1;
    long long sender_new = sender_balance - amount;
    long long receiver_new = receiver_balance + amount;

    char timestamp[32];
    if (make_timestamp(timestamp, sizeof(timestamp)) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int n1 = snprintf(lines[sender_idx], LINE_SIZE,
                      "ACCOUNT|%s|%d|%lld",
                      sender_account, sender_user_id, sender_new);

    int n2 = snprintf(lines[receiver_idx], LINE_SIZE,
                      "ACCOUNT|%s|%d|%lld",
                      receiver_account, receiver_user_id, receiver_new);

    int n3 = snprintf(lines[count], LINE_SIZE,
                      "TXN|%lld|%s|%d|TRANSFER_OUT|%lld|%lld|%s",
                      txn1, sender_account, sender_user_id,
                      amount, sender_new, timestamp);

    int n4 = snprintf(lines[count + 1], LINE_SIZE,
                      "TXN|%lld|%s|%d|TRANSFER_IN|%lld|%lld|%s",
                      txn2, receiver_account, receiver_user_id,
                      amount, receiver_new, timestamp);

    if (n1 < 0 || n1 >= LINE_SIZE ||
        n2 < 0 || n2 >= LINE_SIZE ||
        n3 < 0 || n3 >= LINE_SIZE ||
        n4 < 0 || n4 >= LINE_SIZE) {
        unlock_db(lockfd);
        return -1;
    }

    int result = save_lines(lines, count + 2) == 0 ? 0 : -1;
    unlock_db(lockfd);
    return result;
}

/* Loan format:
 * LOAN|loan_id|user_id|amount_paise|purpose|PENDING|timestamp
 */
int db_apply_loan(int user_id, long long amount, const char *purpose) {
    if (amount <= 0 || !valid_text(purpose, 180))
        return -1;

    int lockfd = lock_db();
    if (lockfd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0 || count >= MAX_LINES) {
        unlock_db(lockfd);
        return -1;
    }

    long long id = next_record_id(lines, count, "LOAN");
    char timestamp[32];

    if (id < 1 || make_timestamp(timestamp, sizeof(timestamp)) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int n = snprintf(lines[count], LINE_SIZE,
                     "LOAN|%lld|%d|%lld|%s|PENDING|%s",
                     id, user_id, amount, purpose, timestamp);

    int result = -1;
    if (n >= 0 && n < LINE_SIZE)
        result = save_lines(lines, count + 1) == 0 ? 0 : -1;

    unlock_db(lockfd);
    return result;
}

/* Return 0 on success, -2 for incorrect old password. */
int db_change_password(int user_id, const char *old_password,
                       const char *new_password) {
    if (!old_password || !new_password ||
        !*new_password || strlen(new_password) > 100 ||
        strchr(new_password, '|') ||
        strchr(new_password, '\n') ||
        strchr(new_password, '\r'))
        return -1;

    int lockfd = lock_db();
    if (lockfd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int result = -2;

    for (int i = 0; i < count; i++) {
        char copy[LINE_SIZE];
        char *f[8];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, f, 8);

        if (n != 7 || strcmp(f[0], "USER") != 0 ||
            atoi(f[1]) != user_id)
            continue;

        if (strcmp(f[3], old_password) != 0)
            break;

        int written = snprintf(lines[i], LINE_SIZE,
                               "USER|%s|%s|%s|%s|%s|%s",
                               f[1], f[2], new_password,
                               f[4], f[5], f[6]);

        if (written >= 0 && written < LINE_SIZE)
            result = save_lines(lines, count) == 0 ? 0 : -1;
        else
            result = -1;

        break;
    }

    unlock_db(lockfd);
    return result;
}

/* Feedback format:
 * FEEDBACK|feedback_id|user_id|timestamp|message
 */
int db_submit_feedback(int user_id, const char *message) {
    if (!valid_text(message, 300))
        return -1;

    int lockfd = lock_db();
    if (lockfd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count;

    if (load_lines(lines, &count) != 0 || count >= MAX_LINES) {
        unlock_db(lockfd);
        return -1;
    }

    long long id = next_record_id(lines, count, "FEEDBACK");
    char timestamp[32];

    if (id < 1 || make_timestamp(timestamp, sizeof(timestamp)) != 0) {
        unlock_db(lockfd);
        return -1;
    }

    int n = snprintf(lines[count], LINE_SIZE,
                     "FEEDBACK|%lld|%d|%s|%s",
                     id, user_id, timestamp, message);

    int result = -1;
    if (n >= 0 && n < LINE_SIZE)
        result = save_lines(lines, count + 1) == 0 ? 0 : -1;

    unlock_db(lockfd);
    return result;
}
int db_list_assigned_loans(int employee_id,
                           char *buffer, size_t buffer_size)
{
    if (employee_id <= 0 || buffer == NULL || buffer_size == 0)
        return -1;

    int fd = lock_db();
    if (fd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count = 0;

    if (load_lines(lines, &count) != 0) {
        unlock_db(fd);
        return -1;
    }

    buffer[0] = '\0';
    size_t used = 0;
    int found = 0;

    for (int i = 0; i < count; i++) {
        if (strncmp(lines[i], "LOAN|", 5) != 0)
            continue;

        char copy[LINE_SIZE];
        char *fields[10];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, fields, 10);

        /* New format:
           LOAN|id|user_id|amount|purpose|status|employee_id|timestamp
        */
        if (n != 8)
            continue;

        char *end = NULL;
        long assigned_employee = strtol(fields[6], &end, 10);

        if (end == fields[6] || *end != '\0' ||
            assigned_employee != employee_id)
            continue;

        int written = snprintf(
            buffer + used, buffer_size - used,
            "Loan ID: %s | Customer: %s | Amount (paise): %s"
            " | Purpose: %s | Status: %s\n",
            fields[1], fields[2], fields[3], fields[4], fields[5]);

        if (written < 0 || (size_t)written >= buffer_size - used) {
            unlock_db(fd);
            return -1;
        }

        used += (size_t)written;
        found++;
    }

    unlock_db(fd);

    if (!found)
        snprintf(buffer, buffer_size, "No loans assigned to you.\n");

    return 0;
}

int db_process_loan(int employee_id, long long loan_id,
                    const char *decision)
{
    if (employee_id <= 0 || loan_id <= 0 || decision == NULL)
        return -1;

    int approve = strcmp(decision, "APPROVE") == 0;
    int reject = strcmp(decision, "REJECT") == 0;

    if (!approve && !reject)
        return -1;

    int fd = lock_db();
    if (fd < 0)
        return -1;

    char lines[MAX_LINES][LINE_SIZE];
    int count = 0;

    if (load_lines(lines, &count) != 0) {
        unlock_db(fd);
        return -1;
    }

    int updated = 0;

    for (int i = 0; i < count; i++) {
        if (strncmp(lines[i], "LOAN|", 5) != 0)
            continue;

        char copy[LINE_SIZE];
        char *fields[10];

        snprintf(copy, sizeof(copy), "%s", lines[i]);
        int n = split(copy, fields, 10);

        /* Only process assigned loans in the new 8-field format. */
        if (n != 8)
            continue;

        char *id_end = NULL;
        char *employee_end = NULL;

        long long current_id = strtoll(fields[1], &id_end, 10);
        long assigned_employee = strtol(fields[6], &employee_end, 10);

        if (id_end == fields[1] || *id_end != '\0' ||
            employee_end == fields[6] || *employee_end != '\0')
            continue;

        if (current_id != loan_id || assigned_employee != employee_id)
            continue;

        if (strcmp(fields[5], "PENDING") != 0) {
            unlock_db(fd);
            return -2;
        }

        char timestamp[64];
        if (make_timestamp(timestamp, sizeof(timestamp)) != 0) {
            unlock_db(fd);
            return -1;
        }

        int written = snprintf(
            lines[i], LINE_SIZE,
            "LOAN|%s|%s|%s|%s|%s|%s|%s",
            fields[1], fields[2], fields[3], fields[4],
            approve ? "APPROVED" : "REJECTED",
            fields[6], timestamp);

        if (written < 0 || written >= LINE_SIZE) {
            unlock_db(fd);
            return -1;
        }

        updated = 1;
        break;
    }

    if (!updated) {
        unlock_db(fd);
        return -3;
    }

    int result = save_lines(lines, count);
    unlock_db(fd);

    return result == 0 ? 0 : -1;
}
