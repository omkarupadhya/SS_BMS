# Banking Management System

A **client-server banking application written in C** that demonstrates socket programming, concurrent client handling, file-based persistence, inter-process coordination, and role-based access control on Linux.

The system provides separate interfaces for customers, employees, managers, and administrators, with banking operations and loan-management workflows.

## Features

### Customer

- View account balance and transaction history.
- Deposit and withdraw funds.
- Transfer funds between accounts.
- Apply for loans.
- Change password and submit feedback.

### Employee

- View loans assigned by a manager.
- Approve or reject assigned loan applications.
- View customer passbook information.
- Change password.

### Manager

- View pending loan applications awaiting assignment.
- Assign loan applications to employees.
- Activate or deactivate customer accounts.
- Review customer feedback.

### Administrator

- Create employee accounts.
- Edit user details, roles, and account status.
- Change password.

## Architecture

The application follows a client-server architecture:

- **Client (`client.c`):** Provides the terminal-based user interface and sends requests to the server.
- **Server (`server.c`):** Accepts TCP connections, authenticates users, enforces role-based permissions, and dispatches operations.
- **Database layer (`database.c`, `database.h`):** Implements banking operations and file-based persistence.
- **Management layer (`management.c`):** Implements loan assignment, loan processing, customer account management, and feedback review.
- **Build system (`Makefile`):** Compiles the client and server.

### Technologies and concepts

- C and POSIX system calls
- TCP sockets
- Process-based concurrency using `fork()`
- File locking using `flock()`
- File-based data persistence
- Atomic file replacement using temporary files and `rename()`
- Role-based access control
- Linux development environment and GCC

### Class diagram

The project includes a Mermaid class diagram describing the main entities and their relationships.

Source: BMS_CD.png

## Project Structure

```text
SS_BMS/
├── client.c
├── server.c
├── database.c
├── database.h
├── management.c
├── Makefile
└── BMS_CD.png
```

## Getting Started

### Prerequisites

- Linux or WSL2 running Ubuntu
- GCC compiler
- GNU Make

### 1. Clone the repository

```bash
git clone https://github.com/omkarupadhya/SS_BMS.git
cd SS_BMS
```

### 2. Prepare the data file

The application expects a `bank_data.txt` file in the working directory. This file stores user records, accounts, transactions, loan applications, and feedback.

The data file is intentionally excluded from the repository to avoid publishing account information or credentials. Before running the application, prepare a valid data file using the record format expected by the database implementation.

**Note:** The repository does not currently include an automated database initialization or sample-data seeding script.

### 3. Compile the application

```bash
make
```

This builds two executables:

- `bank_server`
- `bank_client`

To rebuild from scratch:

```bash
make clean
make
```

### 4. Run the server

Open a terminal in the project directory:

```bash
./bank_server
```

The server listens on TCP port `8080`.

### 5. Run the client

Open a second terminal in the same project directory:

```bash
./bank_client
```

The current client connects to `127.0.0.1:8080`, so the server and client are intended to run on the same machine by default.

Choose the appropriate role and provide valid login credentials from the prepared data file.

## Data Persistence and Concurrency

The system stores its records in a local text file rather than using an external database server.

- File locking coordinates access to shared data during database operations.
- Updates are written through a temporary file and committed using file replacement.
- The server uses a separate process to handle each client connection.

These mechanisms demonstrate operating-system concepts relevant to concurrent systems and persistent data management.

## Limitations and Security Considerations

This project is an educational systems-programming application, **not a production-ready banking platform**.

- Credentials are stored in the local data file in plaintext.
- The client-server protocol does not provide transport encryption.
- The client is configured for localhost rather than remote deployment.
- Persistence relies on a custom text-file format rather than a database management system.
- Production use would require stronger authentication, secure password hashing, encrypted communication, auditing, input hardening, and additional reliability and security testing.

Use only disposable demonstration data. Never use real banking credentials or financial information.

## Testing

The project has undergone manual regression testing of core workflows, including authentication, deposits, withdrawals, transfers, transaction history, loan assignment and processing, account activation, password changes, feedback handling, and persistence across restarts.

The project also includes a manual regression checklist and test plan in the development workspace; these documents are not currently part of the published repository.

## Purpose

This project was developed as part of systems-oriented coursework to explore networking, process management, synchronization, persistence, and modular C programming through a practical banking application.

---

