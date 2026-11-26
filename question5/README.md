# Concurrent TCP-Based University Event Registration System

## Overview

A concurrent university event registration system implemented using TCP sockets and POSIX threads. The application allows students to authenticate, view available university events, and register for events through a client-server architecture.

The server supports multiple simultaneous clients, protects shared registration data, enforces event capacity limits, prevents duplicate registrations, and maintains registration records across server restarts.

## Implementation

### TCP Communication Between Client and Server

The application uses TCP sockets for reliable communication between the client and server. The server listens for incoming connections on port `8080` and accepts client connections. Each client communicates with the server through its established socket connection.

Communication uses a newline-delimited command protocol:

| Command | Description |
|---|---|
| `AUTH` | Sends a student ID for authentication. |
| `LIST` | Requests the available university events. |
| `REG` | Requests registration for a selected event. |
| `EXIT` | Ends the client session. |

The server processes client requests and sends responses through the corresponding socket connection.

### Student Authentication

Students authenticate using predefined student IDs ranging from `STU001` to `STU008`. When a client connects, the server verifies the submitted ID against the authorized list.

Only authenticated students can access the event registration functionality. Invalid student IDs are rejected.

### Concurrency Approach

The server uses a thread-per-client concurrency model implemented with POSIX threads (`pthread`). Whenever a client connects, the server creates a separate worker thread to handle that client's requests.

This allows multiple students to connect and interact with the registration system concurrently without requiring requests from different clients to be handled sequentially.

### Shared Resource Protection

A POSIX mutex protects shared event and registration data from concurrent access.

Worker threads acquire the mutex before performing operations that read or modify protected registration state. This ensures that registration checks and updates are performed safely, preventing race conditions and inconsistent data.

### Event Capacity Management

Each event has a predefined maximum registration capacity. Before accepting a registration request, the server checks the current number of registrations against the event's capacity.

If spaces are available, registration is accepted and registration count is updated. If the event is full, the request is rejected.

The capacity check and registration update are protected by the mutex to prevent concurrent requests from exceeding the event's maximum capacity.

### Prevention of Duplicate Registrations

Before accepting a registration request, the server checks whether the student is already registered for the selected event.

If an existing registration is found, the new request is rejected. The duplicate check and registration operation are protected by the mutex to prevent multiple concurrent requests from creating duplicate registrations.

### Handling Unexpected Client Disconnections

The server monitors client connections and detects unexpected disconnections during communication. When a disconnection occurs, the server logs the event and closes the associated client socket without terminating the entire server or interrupting other client sessions.

An idle receive timeout is also implemented to prevent inactive client sessions from remaining open indefinitely.

## Registration Persistence

Registration records are stored in `registrations.db`. Each record contains an event ID and the corresponding student ID, separated by a comma.

Example:

```text
102,STU001
102,STU002
102,STU003
102,STU004
```

The server loads existing registration records when it starts and saves new registrations to the database. This preserves registration information across server restarts and ensures that existing registrations are considered when calculating event capacity.

## Project Files

| File | Description |
|---|---|
| `server.c` | Implements the TCP server, authentication, concurrent client handling, synchronization, and registration management. |
| `client.c` | Implements the client application and menu-driven registration interface. |
| `run_tests.sh` | Automates compilation and functional testing. |
| `evidence/` | Contains test outputs, server and client logs, and registration database records. |

## Compilation

From the project directory, compile the server and client:

```bash
gcc -Wall -Wextra -Werror -pedantic -std=c11 -pthread server.c -o server

gcc -Wall -Wextra -Werror -pedantic -std=c11 client.c -o client
```

## Running the Application

Start the server in the first terminal:

```bash
./server
```

Start a client in another terminal:

```bash
./client
```

Authenticate using one of the authorized student IDs (`STU001`–`STU008`) and follow the displayed menu to register for an event or exit.

To stop the server, press `Ctrl+C` in the server terminal.

## Testing

Run the automated test suite from the project directory:

```bash
chmod +x run_tests.sh
./run_tests.sh
```

The test script verifies:

- Compilation with strict compiler warnings enabled.
- Valid and invalid student authentication.
- Successful and duplicate registration attempts.
- Concurrent client registration.
- Event capacity enforcement.
- Unexpected client disconnections.
- Server shutdown using SIGINT.
- Registration persistence across server restarts.

All tests passed. The concurrent registration test confirmed that Event 102 reached its maximum capacity of four registrations without exceeding the limit.

The persistence test confirmed that all four registrations were successfully loaded after restarting the server. A newly authenticated client also viewed Event 102 at `4/4` capacity.

Test outputs and supporting logs are available in the `evidence/` directory.

