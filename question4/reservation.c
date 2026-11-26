#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#define QUEUE_CAPACITY 5
#define TOTAL_REQUESTS 15
#define MONITOR_INTERVAL_SEC 4

typedef struct {
    int reservation_id;
    int book_id;
} ReservationRequest;

typedef struct {
    ReservationRequest buffer[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int total_submitted;
    int total_processed;
    int approved_count;
    int rejected_count;
    bool all_done;
    bool shutdown;
    pthread_mutex_t mutex;
    pthread_mutex_t log_mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    pthread_cond_t done_cond;
} LibrarySystem;

/* prevent messages from different threads being printed simultaneously */
void safe_log(LibrarySystem *sys, const char *format, ...)
{
    pthread_mutex_lock(&sys->log_mutex);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
    pthread_mutex_unlock(&sys->log_mutex);
}

/* init. shared state & synchronization, & clean up if initialization fails */
int init_system(LibrarySystem *sys)
{
    sys->head = 0;
    sys->tail = 0;
    sys->count = 0;
    sys->total_submitted = 0;
    sys->total_processed = 0;
    sys->approved_count = 0;
    sys->rejected_count = 0;
    sys->all_done = false;
    sys->shutdown = false;

    int rc;

    rc = pthread_mutex_init(&sys->mutex, NULL);
    if (rc != 0) return rc;

    rc = pthread_mutex_init(&sys->log_mutex, NULL);
    if (rc != 0) goto fail_mutex;

    rc = pthread_cond_init(&sys->not_full, NULL);
    if (rc != 0) goto fail_log_mutex;

    rc = pthread_cond_init(&sys->not_empty, NULL);
    if (rc != 0) goto fail_not_full;

    rc = pthread_cond_init(&sys->done_cond, NULL);
    if (rc != 0) goto fail_not_empty;

    return 0;

fail_not_empty:
    pthread_cond_destroy(&sys->not_empty);
fail_not_full:
    pthread_cond_destroy(&sys->not_full);
fail_log_mutex:
    pthread_mutex_destroy(&sys->log_mutex);
fail_mutex:
    pthread_mutex_destroy(&sys->mutex);

    return rc;
}

/* destroy synchronization primitives after all threads terminate */
void destroy_system(LibrarySystem *sys)
{
    pthread_cond_destroy(&sys->done_cond);
    pthread_cond_destroy(&sys->not_empty);
    pthread_cond_destroy(&sys->not_full);
    pthread_mutex_destroy(&sys->log_mutex);
    pthread_mutex_destroy(&sys->mutex);
}

/* request shutdown & wake threads waiting on condition variables */
void request_shutdown(LibrarySystem *sys)
{
    pthread_mutex_lock(&sys->mutex);
    sys->shutdown = true;
    pthread_cond_broadcast(&sys->not_full);
    pthread_cond_broadcast(&sys->not_empty);
    pthread_cond_broadcast(&sys->done_cond);
    pthread_mutex_unlock(&sys->mutex);
}

/* student thread (Producer) generates & submits 15 reservation requests */
void *student_producer(void *arg)
{
    LibrarySystem *sys = (LibrarySystem *)arg;

    unsigned int seed =
        (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();

    for (int i = 1; i <= TOTAL_REQUESTS; i++) {
        sleep(1);
        ReservationRequest req;
        req.reservation_id = i;
        req.book_id = (rand_r(&seed) % 900) + 100;
        pthread_mutex_lock(&sys->mutex);

        while (sys->count == QUEUE_CAPACITY && !sys->shutdown) {
            safe_log(sys,
                     "Student: queue FULL (%d/%d) "
                     "Waiting to submit reservation #%d (Book ID: %d)...\n",
                     sys->count, QUEUE_CAPACITY,
                     req.reservation_id, req.book_id);
            pthread_cond_wait(&sys->not_full, &sys->mutex);
        }

        if (sys->shutdown) {
            pthread_mutex_unlock(&sys->mutex);
            return NULL;
        }

        sys->buffer[sys->tail] = req;
        sys->tail = (sys->tail + 1) % QUEUE_CAPACITY;
        sys->count++;
        sys->total_submitted++;
        safe_log(sys,
                 "Student: submitted reservation #%d (Book ID: %d) "
                 "| queue size: %d/%d\n",
                 req.reservation_id, req.book_id,
                 sys->count, QUEUE_CAPACITY);
        pthread_cond_signal(&sys->not_empty);
        pthread_mutex_unlock(&sys->mutex);
    }

    safe_log(sys,
             "Student: finished submitting all %d reservations. Exiting...\n",
             TOTAL_REQUESTS);

    return NULL;
}

/* library staff thread (Consumer) retrieves & processes reservation requests */
void *staff_consumer(void *arg)
{
    LibrarySystem *sys = (LibrarySystem *)arg;

    while (true) {
        pthread_mutex_lock(&sys->mutex);
        while (sys->count == 0 && !sys->shutdown) {
            safe_log(sys,
                     "Staff: queue EMPTY. Waiting for new reservation requests...\n");
            pthread_cond_wait(&sys->not_empty, &sys->mutex);
        }

        if (sys->count == 0 && sys->shutdown) {
            pthread_mutex_unlock(&sys->mutex);
            break;
        }

        ReservationRequest req = sys->buffer[sys->head];
        sys->head = (sys->head + 1) % QUEUE_CAPACITY;
        sys->count--;

        safe_log(sys,
                 "Staff: retrieved reservation #%d (Book ID: %d) "
                 "| queue size: %d/%d. Processing...\n",
                 req.reservation_id, req.book_id,
                 sys->count, QUEUE_CAPACITY);

        pthread_cond_signal(&sys->not_full);
        pthread_mutex_unlock(&sys->mutex);
        sleep(3);
        bool is_available = (req.book_id % 2 == 0);
        pthread_mutex_lock(&sys->mutex);
        sys->total_processed++;

        if (is_available) {
            sys->approved_count++;
            safe_log(sys,
                     "Staff: APPROVED reservation #%d "
                     "(Book ID: %d is EVEN/available)\n",
                     req.reservation_id, req.book_id);
        } else {
            sys->rejected_count++;
            safe_log(sys,
                     "Staff: REJECTED reservation #%d "
                     "(Book ID: %d is ODD/unavailable)\n",
                     req.reservation_id, req.book_id);
        }

        if (sys->total_processed == TOTAL_REQUESTS) {
            sys->all_done = true;
            pthread_cond_broadcast(&sys->done_cond);
            pthread_mutex_unlock(&sys->mutex);
            break;
        }

        pthread_mutex_unlock(&sys->mutex);
    }

    safe_log(sys, "Staff: finished processing reservations. Exiting...\n");
    return NULL;
}

/* monitoring thread reports shared statistics every four seconds */
void *monitor_system(void *arg)
{
    LibrarySystem *sys = (LibrarySystem *)arg;
    pthread_mutex_lock(&sys->mutex);

    while (!sys->all_done && !sys->shutdown) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += MONITOR_INTERVAL_SEC;

        int rc = pthread_cond_timedwait(&sys->done_cond, &sys->mutex, &ts);
        if (rc == ETIMEDOUT || sys->all_done) {
            double approval_rate = 0.0;
            if (sys->total_processed > 0) {
                approval_rate =
                    100.0 * sys->approved_count / sys->total_processed;
            }
            safe_log(sys,
                     "\n SYSTEM MONITOR REPORT \n\n"
                     "  Submitted Reservations       : %d / %d\n"
                     "  Processed Reservations       : %d / %d\n"
                     "  Approved Reservations        : %d\n"
                     "  Rejected Reservations        : %d\n"
                     "  Approval Rate                : %.1f%%\n"
                     "  Current Reservation Queue    : %d / %d\n\n",
                     sys->total_submitted,
                     TOTAL_REQUESTS,
                     sys->total_processed,
                     TOTAL_REQUESTS,
                     sys->approved_count,
                     sys->rejected_count,
                     approval_rate,
                     sys->count,
                     QUEUE_CAPACITY);
        }

        if (rc != 0 && rc != ETIMEDOUT) {
            sys->shutdown = true;
            pthread_cond_broadcast(&sys->not_full);
            pthread_cond_broadcast(&sys->not_empty);
            break;
        }
    }

    pthread_mutex_unlock(&sys->mutex);
    safe_log(sys, "Monitor: monitoring complete. Exiting...\n");
    return NULL;
}

/* join thread and report errors */
int join_thread(pthread_t tid, const char *name)
{
    int rc = pthread_join(tid, NULL);
    if (rc != 0) {
        fprintf(stderr,
                "Failed to join %s thread: %s\n",
                name, strerror(rc));
    }
    return rc;
}

/* init. system, create threads, coordinate termination & release resources */
int main(void)
{
    LibrarySystem *sys = malloc(sizeof(*sys));
    if (sys == NULL) {
        perror("Failed to allocate shared system memory");
        return EXIT_FAILURE;
    }

    int rc = init_system(sys);
    if (rc != 0) {
        fprintf(stderr,
                "Failed to initialize synchronization: %s\n",
                strerror(rc));
        free(sys);
        return EXIT_FAILURE;
    }

    safe_log(sys,
             "Starting University Library Book Reservation System...\n");
    safe_log(sys,
             "Queue Capacity: %d | Total Reservations: %d\n\n",
             QUEUE_CAPACITY, TOTAL_REQUESTS);
    pthread_t producer_tid;
    pthread_t consumer_tid;
    pthread_t monitor_tid;
    bool producer_started = false;
    bool consumer_started = false;
    bool monitor_started = false;
    
    rc = pthread_create(&producer_tid, NULL, student_producer, sys);
    if (rc != 0) {
        fprintf(stderr, "Failed to create producer thread: %s\n",
                strerror(rc));
        goto creation_failure;
    }
    producer_started = true;

    rc = pthread_create(&consumer_tid, NULL, staff_consumer, sys);
    if (rc != 0) {
        fprintf(stderr, "Failed to create consumer thread: %s\n",
                strerror(rc));
        goto creation_failure;
    }
    consumer_started = true;

    rc = pthread_create(&monitor_tid, NULL, monitor_system, sys);
    if (rc != 0) {
        fprintf(stderr, "Failed to create monitor thread: %s\n",
                strerror(rc));
        goto creation_failure;
    }
    monitor_started = true;

    int join_failed = 0;
    if (join_thread(producer_tid, "producer") != 0) join_failed = 1;
    if (join_thread(consumer_tid, "consumer") != 0) join_failed = 1;
    if (join_thread(monitor_tid, "monitor") != 0) join_failed = 1;
    if (join_failed) return EXIT_FAILURE;

    safe_log(sys, "\nAll threads terminated gracefully\n");
    safe_log(sys,
             "Final Summary -> Submitted: %d | Processed: %d | "
             "Approved: %d | Rejected: %d | Queue Size: %d\n",
             sys->total_submitted,
             sys->total_processed,
             sys->approved_count,
             sys->rejected_count,
             sys->count);
    destroy_system(sys);
    free(sys);
    return EXIT_SUCCESS;

creation_failure:
    request_shutdown(sys);
    bool creation_join_failed = false;
    if (producer_started &&
        join_thread(producer_tid, "producer") != 0)
        creation_join_failed = true;
    if (consumer_started &&
        join_thread(consumer_tid, "consumer") != 0)
        creation_join_failed = true;
    if (monitor_started &&
        join_thread(monitor_tid, "monitor") != 0)
        creation_join_failed = true;
    if (creation_join_failed) return EXIT_FAILURE;
    destroy_system(sys);
    free(sys);
    return EXIT_FAILURE;
}

