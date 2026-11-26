#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <limits.h>
#define PORT 8080
#define MAX_CLIENTS 20
#define MAX_CAPACITY 20
#define BUFFER_SIZE 1024
#define ID_LEN 20
#define NAME_LEN 64
#define NUM_EVENTS 3
#define NUM_VALID_STUDENTS 8
#define DB_FILE "registrations.db"
#define CLIENT_TIMEOUT_SEC 300

typedef struct {
    int id;
    char name[NAME_LEN];
    int max_capacity;
    int current_count;
    char registered_students[MAX_CAPACITY][ID_LEN];
} Event;

typedef struct {
    int socket;
    char student_id[ID_LEN];
    int active;
} ClientInfo;

static Event events[NUM_EVENTS] = {
    {101, "AI Workshop", 5, 0, {{0}}},
    {102, "Cybersecurity Seminar", 4, 0, {{0}}},
    {103, "Research Conference", 6, 0, {{0}}}
};

static const char *valid_students[NUM_VALID_STUDENTS] = {
    "STU001", "STU002", "STU003", "STU004",
    "STU005", "STU006", "STU007", "STU008"
};

static ClientInfo connected_clients[MAX_CLIENTS];
static int server_fd = -1;
static volatile sig_atomic_t shutdown_requested = 0;
static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handle_sigint(int sig) {
    (void)sig;
    shutdown_requested = 1;
}

static void log_msg(const char *level, const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm tm_info;
    char time_buf[32];

    if (localtime_r(&now, &tm_info) == NULL) {
        snprintf(time_buf, sizeof(time_buf), "unknown-time");
    } else {
        strftime(time_buf, sizeof(time_buf),
                 "%Y-%m-%d %H:%M:%S", &tm_info);
    }

    pthread_mutex_lock(&log_mutex);
    printf("[%s] [%s] ", time_buf, level);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

/* caller must hold event_mutex */
static int save_registrations_locked(void)
{
    FILE *fp = fopen(DB_FILE, "w");

    if (fp == NULL) {
        log_msg("ERROR", "Cannot open %s for writing: %s",
                DB_FILE, strerror(errno));
        return -1;
    }

    for (int i = 0; i < NUM_EVENTS; i++) {
        for (int j = 0; j < events[i].current_count; j++) {
            if (fprintf(fp, "%d,%s\n", events[i].id,
                        events[i].registered_students[j]) < 0) {
                log_msg("ERROR", "Failed writing registrations to %s",
                        DB_FILE);
                fclose(fp);
                return -1;
            }
        }
    }

    if (fclose(fp) != 0) {
        log_msg("ERROR", "Failed closing %s: %s",
                DB_FILE, strerror(errno));
        return -1;
    }

    return 0;
}

static int is_valid_student(const char *id)
{
    for (int i = 0; i < NUM_VALID_STUDENTS; i++) {
        if (strcmp(valid_students[i], id) == 0) {return 1;}
    }
    return 0;
}

static int is_valid_student_in_event(const Event *event, const char *id)
{
    for (int i = 0; i < event->current_count; i++) {
        if (strcmp(event->registered_students[i], id) == 0) {return 1;}
    }
    return 0;
}

/* load only authorized, unique records & never exceed event capacity */
static void load_registrations(void)
{
    FILE *fp = fopen(DB_FILE, "r");

    if (fp == NULL) {
        if (errno != ENOENT) {
            log_msg("ERROR", "Cannot open %s: %s",
                    DB_FILE, strerror(errno));
        } else {
            log_msg("INFO",
                    "No existing %s; starting with empty registrations",
                    DB_FILE);
        }
        return;
    }

    char line[BUFFER_SIZE];
    int loaded = 0;
    int skipped = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        int event_id;
        char student_id[ID_LEN];
        char extra;
        if (sscanf(line, "%d,%19[A-Za-z0-9]%c",
                   &event_id, student_id, &extra) < 2 ||
            !is_valid_student(student_id)) {
            skipped++;
            continue;
        }

        int found = -1;
        for (int i = 0; i < NUM_EVENTS; i++) {
            if (events[i].id == event_id) { found = i; break; }
        }

        if (found < 0 ||
            events[found].current_count >= events[found].max_capacity ||
            is_valid_student_in_event(&events[found], student_id)) {
            skipped++;
            continue;
        }

        snprintf(events[found].registered_students[
                     events[found].current_count],
                 ID_LEN, "%s", student_id);
        events[found].current_count++;
        loaded++;
    }

    fclose(fp);
    log_msg("INFO", "Loaded %d registration(s); skipped %d invalid record(s)",
            loaded, skipped);
}

/* newline-delimited TCP reader; returns -2 for overlong line */
static ssize_t recv_line(int sock, char *buf, size_t max_len)
{
    size_t idx = 0;
    int too_long = 0;

    if (max_len < 2) { errno = EINVAL; return -1; }

    for (;;) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n == 0) {if (idx == 0 && !too_long) {return 0;} break;}
        if (n < 0) {if (errno == EINTR) {continue;} return -1;}
        if (c == '\n') {break;}
        if (c == '\r') {continue;}
        if (idx < max_len - 1) {buf[idx++] = c;
        } else {too_long = 1;}
    }

    buf[idx] = '\0';
    return too_long ? -2 : (ssize_t)idx;
}

static int send_all(int sock, const char *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t sent = send(sock, buf + total, len - total, 0);
        if (sent < 0 && errno == EINTR) {continue;}
        if (sent <= 0) {return -1;}
        total += (size_t)sent;
    }
    return 0;
}

static int add_client(int sock)
{
    int added = 0;
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!connected_clients[i].active) {
            connected_clients[i].socket = sock;
            connected_clients[i].student_id[0] = '\0';
            connected_clients[i].active = 1;
            added = 1;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    return added;
}

/* returns -1 for duplicate active login, & 0 when binding succeeds */
static int authenticate_and_bind_client(int sock, const char *id)
{
    int socket_found = 0;
    pthread_mutex_lock(&client_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (connected_clients[i].active &&
            connected_clients[i].socket != sock &&
            strcmp(connected_clients[i].student_id, id) == 0) {
            pthread_mutex_unlock(&client_mutex);
            return -1;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (connected_clients[i].active &&
            connected_clients[i].socket == sock) {
            snprintf(connected_clients[i].student_id,
                     sizeof(connected_clients[i].student_id),
                     "%s", id);
            socket_found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&client_mutex);
    return socket_found ? 0 : -1;
}

static void remove_client(int sock)
{
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (connected_clients[i].active &&
            connected_clients[i].socket == sock) {
            log_msg("DISCONNECT",
                    "Client disconnected (socket %d, ID: %s)",
                    sock,
                    connected_clients[i].student_id[0] ?
                    connected_clients[i].student_id : "UNAUTHENTICATED");
            connected_clients[i].active = 0;
            connected_clients[i].student_id[0] = '\0';
            connected_clients[i].socket = -1;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

static void send_error_and_close(int sock, const char *message) 
{
    (void)send_all(sock, message, strlen(message));
    close(sock);
}

static void *handle_client(void *arg)
{
    int client_sock = *(int *)arg;
    free(arg);
    struct timeval tv;
    tv.tv_sec = CLIENT_TIMEOUT_SEC;
    tv.tv_usec = 0;

    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv)) < 0) {
        log_msg("WARN",
                "Could not set receive timeout on socket %d: %s",
                client_sock, strerror(errno));
    }

    if (!add_client(client_sock)) {
        send_error_and_close(
            client_sock,
            "ERR_FULL Server connection limit reached\n");
        return NULL;
    }

    log_msg("CONNECT",
            "Accepted client connection on socket %d", client_sock);
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char authenticated_id[ID_LEN] = "";
    int is_authenticated = 0;

    for (;;) {
        ssize_t received = recv_line(client_sock, buffer, sizeof(buffer));
        if (received <= 0) {
            if (received == -2) {
                (void)send_all(
                    client_sock,
                    "ERR_CMD Command too long\n",
                    strlen("ERR_CMD Command too long\n"));
            } else if (received < 0 &&
			(errno == EAGAIN || errno == EWOULDBLOCK)) {
                log_msg("TIMEOUT",
                        "Socket %d timed out after %d seconds",
                        client_sock, CLIENT_TIMEOUT_SEC);
            } else if (received < 0) {
                log_msg("WARN",
                        "Receive error on socket %d: %s",
                        client_sock, strerror(errno));
            }
            break;
        }

        if (strncmp(buffer, "AUTH ", 5) == 0) {
            const char *id = buffer + 5;
            if (is_authenticated) {
                snprintf(response, sizeof(response),
                         "AUTH_FAIL Session already authenticated\n");
            } else if (!is_valid_student(id)) {
                snprintf(response, sizeof(response),
                         "AUTH_FAIL Invalid Student ID\n");
                log_msg("AUTH_FAIL",
                        "Rejected ID '%s' on socket %d",
                        id, client_sock);
            } else if (authenticate_and_bind_client(
                           client_sock, id) < 0) {
                snprintf(response, sizeof(response),
                         "AUTH_FAIL Student %s already logged in\n",
                         id);
                log_msg("AUTH_FAIL",
                        "Duplicate active login for %s", id);
            } else {
                is_authenticated = 1;
                snprintf(authenticated_id,
                         sizeof(authenticated_id), "%s", id);
                snprintf(response, sizeof(response),
                         "AUTH_OK Welcome %s!\n", authenticated_id);
                log_msg("AUTH_OK",
                        "Student %s authenticated on socket %d",
                        authenticated_id, client_sock);
            }
            if (send_all(client_sock, response, strlen(response)) < 0) {
                break;
            }
        } else if (!is_authenticated) {
            snprintf(response, sizeof(response),
                     "ERR_UNAUTH Please authenticate first\n");
            if (send_all(client_sock, response, strlen(response)) < 0) {
                break;
            }
        } else if (strcmp(buffer, "LIST") == 0) {
            size_t offset = 0;
            int failed = 0;
            pthread_mutex_lock(&event_mutex);
            int written = snprintf(
                response, sizeof(response), "EVENTS_START\n");
            
	    if (written < 0 ||
                (size_t)written >= sizeof(response)) {failed = 1;
            } else {offset = (size_t)written;}

            for (int i = 0; i < NUM_EVENTS && !failed; i++) {
                written = snprintf(
                    response + offset,
                    sizeof(response) - offset,
                    "ID: %d | %-22s | Spots: %d/%d\n",
                    events[i].id,
                    events[i].name,
                    events[i].current_count,
                    events[i].max_capacity);
                if (written < 0 ||
                    (size_t)written >= sizeof(response) - offset) {failed = 1;
                } else {offset += (size_t)written;}
            }

            if (!failed) {
                written = snprintf(
                    response + offset,
                    sizeof(response) - offset,
                    "EVENTS_END\n");
                if (written < 0 ||
                    (size_t)written >= sizeof(response) - offset) {failed = 1;
                } else {offset += (size_t)written;}
            }

            pthread_mutex_unlock(&event_mutex);

            if (failed ||
                send_all(client_sock, response, offset) < 0) {break;}
        } else if (strncmp(buffer, "REG ", 4) == 0) {
            char *endptr = NULL;
            errno = 0;
            long value = strtol(buffer + 4, &endptr, 10);

            if (buffer[4] == '\0' ||
                endptr == buffer + 4 ||
                *endptr != '\0' ||
                errno == ERANGE ||
                value < 1 ||
                value > INT_MAX) {
                snprintf(response, sizeof(response),
                         "REG_FAIL Invalid Event ID format\n");
            } else {
                int event_id = (int)value;
                pthread_mutex_lock(&event_mutex);
                int found = -1;

                for (int i = 0; i < NUM_EVENTS; i++) {
                    if (events[i].id == event_id) { found = i; break; }
                }

                if (found < 0) {
                    snprintf(response, sizeof(response),
                             "REG_FAIL Event ID %d does not exist\n",
                             event_id);
                } else if (is_valid_student_in_event(
                               &events[found], authenticated_id)) {
                    snprintf(response, sizeof(response),
                             "REG_DUP You already registered for %s\n",
                             events[found].name);
                    log_msg("REG_DUP",
                            "%s attempted duplicate registration for %s",
                            authenticated_id, events[found].name);
                } else if (events[found].current_count >=
                           events[found].max_capacity) {
                    snprintf(response, sizeof(response),
                             "REG_FULL Event '%s' is full (%d/%d)\n",
                             events[found].name,
                             events[found].current_count,
                             events[found].max_capacity);
                    log_msg("REG_FULL",
                            "%s rejected from full event %s (%d/%d)",
                            authenticated_id,
                            events[found].name,
                            events[found].current_count,
                            events[found].max_capacity);
                } else {
                    int slot = events[found].current_count;
                    snprintf(events[found].registered_students[slot],
                             ID_LEN, "%s", authenticated_id);
                    events[found].current_count++;

                    if (save_registrations_locked() == 0) {
                        snprintf(response, sizeof(response),
                                 "REG_OK Successfully registered for %s! "
                                 "(%d/%d filled)\n",
                                 events[found].name,
                                 events[found].current_count,
                                 events[found].max_capacity);
                        log_msg("REG_OK",
                                "%s registered for %s (%d/%d)",
                                authenticated_id,
                                events[found].name,
                                events[found].current_count,
                                events[found].max_capacity);
                    } else {
                        events[found].current_count--;
                        events[found].registered_students[slot][0] = '\0';
                        (void)save_registrations_locked();
                        snprintf(response, sizeof(response),
                                 "REG_FAIL Registration could not be saved..."
                                 "Please retry\n");
                    }
                }
                pthread_mutex_unlock(&event_mutex);
            }

            if (send_all(client_sock, response, strlen(response)) < 0) {
                break;
            }
        } else if (strcmp(buffer, "EXIT") == 0) {break;
        } else {
            snprintf(response, sizeof(response), "ERR_CMD Unknown command\n");
            if (send_all(client_sock, response, strlen(response)) < 0) {
                break;
            }
        }
    }

    remove_client(client_sock);
    close(client_sock);
    return NULL;
}

static void shutdown_clients(void)
{
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (connected_clients[i].active) {
            (void)shutdown(
                connected_clients[i].socket, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    load_registrations();
    struct sockaddr_in address;
    int opt = 1;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    log_msg("SERVER",
            "University Event Registration Server listening on port %d",
            PORT);
    log_msg("SERVER",
            "Authorized IDs: STU001-STU008 | Idle timeout: %ds",
            CLIENT_TIMEOUT_SEC);

    while (!shutdown_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(
            server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock < 0) {
            if (errno == EINTR && shutdown_requested) {break;}
            if (errno == EINTR) {continue;}
            if (shutdown_requested) {break;}
            log_msg("ERROR", "accept failed: %s", strerror(errno));
            continue;
        }

        int *socket_arg = malloc(sizeof(*socket_arg));
        if (socket_arg == NULL) {
            log_msg("ERROR",
                    "Unable to allocate client socket argument");
            close(client_sock);
            continue;
        }

        *socket_arg = client_sock;
        pthread_t thread_id;
        int rc = pthread_create(
            &thread_id, NULL, handle_client, socket_arg);

        if (rc != 0) {
            log_msg("ERROR", "pthread_create failed: %s",
                    strerror(rc));
            close(client_sock);
            free(socket_arg);
            continue;
        }

        rc = pthread_detach(thread_id);
        if (rc != 0) {
            log_msg("WARN", "pthread_detach failed: %s",
                    strerror(rc));
        }
    }

    log_msg("SERVER",
            "Shutdown requested; closing listener and client sessions");

    if (server_fd >= 0) { close(server_fd); server_fd = -1; }

    shutdown_clients();
    pthread_mutex_lock(&event_mutex);
    (void)save_registrations_locked();
    pthread_mutex_unlock(&event_mutex);
    log_msg("SERVER", "Shutdown complete!");
    return EXIT_SUCCESS;
}

