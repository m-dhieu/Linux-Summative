# Multithreaded Library Book Reservation System

## Overview

This project implements a multithreaded university library book reservation system using POSIX threads. It simulates students submitting book reservation requests while library staff process them concurrently.

The system uses a bounded circular queue to manage reservation requests and synchronization mechanisms to ensure safe access to shared data between threads.

## System Architecture

The application consists of three concurrent threads:

- **Student Thread (Producer):** Generates 15 reservation requests, each with an incrementing reservation number and a randomly generated book ID. It submits one request every second and waits when the queue is full.
- **Library Staff Thread (Consumer):** Retrieves and processes reservation requests from the queue. Each request takes three seconds to process. Reservations with even book IDs are approved, while those with odd book IDs are rejected. The thread waits when the queue is empty.
- **Monitoring Thread:** Periodically reports the number of submitted, processed, approved, and rejected reservations, together with the current queue size. Monitoring occurs every four seconds and continues until processing is complete.

## Synchronization and Queue Management

The system uses a circular buffer with a maximum capacity of five reservation requests.

The following synchronization mechanisms are implemented:

- **Mutex locks:** Protect shared queue data and reservation statistics from concurrent access.
- **Condition variables:** Coordinate producer and consumer activity. The `not_full` condition variable prevents the producer from submitting requests when the queue is full, while `not_empty` prevents the consumer from retrieving requests when the queue is empty.
- **Completion signaling:** A condition variable coordinates the monitoring thread's termination after all reservations have been processed.
- **Logging mutex:** Prevents messages from different threads from being interleaved during output.

Condition variables are checked using `while` loops to ensure threads recheck the relevant conditions after waking.

## Compilation

Compile using GCC:

```bash
gcc -Wall -Wextra -Werror -pedantic -std=c11 -pthread reservation.c -o reservation
```

## Execution

Run the compiled program:

```bash
./reservation
```

The program displays reservation submissions, queue activity, processing decisions, periodic monitoring reports, and a final summary after all threads terminate.

## Execution Evidence

The `evidence` directory contains:

- **execution_output.txt:** Captured terminal output from a complete program execution. It demonstrates reservation submission and processing, queue-full waiting behaviour, monitoring reports, and graceful termination.

The final execution confirms that all 15 reservations were submitted and processed, with the queue returning to an empty state.

## Conclusion

The project demonstrates the use of POSIX threads, mutexes, condition variables, and circular buffer management to implement a concurrent producer-consumer system. It also demonstrates synchronization of shared resources, coordination between threads, periodic monitoring, and graceful termination.

