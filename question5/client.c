#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#define DEFAULT_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024
#define ID_LEN 20

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
        if (idx < max_len - 1) {buf[idx++] = c;} else {too_long = 1;}
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

static int receive_and_print_events(int sock)
{
    char line[BUFFER_SIZE];

    if (send_all(sock, "LIST\n", 5) < 0) {
        fprintf(stderr, "Failed to request events from server\n");
        return -1;
    }

    printf("\n Available University Events \n");

    int started = 0;

    for (;;) {
        ssize_t bytes = recv_line(sock, line, sizeof(line));

        if (bytes <= 0) {
            fprintf(stderr,
                    "Server disconnected or sent invalid response\n");
            return -1;
        }

        if (strcmp(line, "EVENTS_START") == 0) { started = 1; continue; }

        if (strcmp(line, "EVENTS_END") == 0) {
            if (!started) {
                fprintf(stderr,
                        "Invalid event-list response from server\n");
                return -1;
            }
            break;
        }

        printf("%s\n", line);
    }

    printf("\n");
    return 0;
}

static int discard_line(void)
{
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* discard remaining input */
    }
    return ch == EOF ? -1 : 0;
}

int main(int argc, char *argv[])
{
    const char *server_ip = (argc > 1) ? argv[1] : DEFAULT_IP;
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char student_id[ID_LEN];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip,
                  &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IPv4 server address: %s\n",
                server_ip);
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr,
                sizeof(serv_addr)) < 0) {
        perror("Connection failed. Is the server running?");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to University Event Registration Server "
           "(%s:%d).\n", server_ip, PORT);

    printf("Enter Student ID: ");
    fflush(stdout);

    if (scanf("%19s", student_id) != 1) {
        fprintf(stderr, "No student ID supplied\n");
        close(sock);
        return EXIT_FAILURE;
    }

    (void)discard_line();

    int written = snprintf(buffer, sizeof(buffer), "AUTH %s\n", student_id);

    if (written < 0 ||
        (size_t)written >= sizeof(buffer) ||
        send_all(sock, buffer, (size_t)written) < 0) {
        fprintf(stderr,
                "Failed to send authentication request\n");
        close(sock);
        return EXIT_FAILURE;
    }

    ssize_t received = recv_line(sock, buffer, sizeof(buffer));

    if (received <= 0) {
        fprintf(stderr,
                "Server closed connection during authentication\n");
        close(sock);
        return EXIT_FAILURE;
    }

    if (strncmp(buffer, "AUTH_OK ", 8) == 0) {
        printf("SUCCESS %s\n", buffer + 8);
    } else if (strncmp(buffer, "AUTH_FAIL ", 10) == 0) {
        printf("ERROR %s\n", buffer + 10);
        printf("Authentication failed. Closing connection...\n");
        close(sock);
        return EXIT_SUCCESS;
    } else {
        printf("ERROR %s\n", buffer);
        close(sock);
        return EXIT_FAILURE;
    }

    for (;;) {
        if (receive_and_print_events(sock) < 0) {break;}

        printf("\nOptions:\n");
        printf("1. Register for an event\n");
        /* printf("2. Refresh event list\n"); */
        printf("2. Exit\n");
        printf("Select an option: ");
        fflush(stdout);

        int choice;

        if (scanf("%d", &choice) != 1) {
            int ch = getchar();
            if (ch == EOF) {
                printf("\nInput closed. Ending registration session...\n");
                break;
            }
            while (ch != '\n' && ch != EOF) {ch = getchar();}
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        (void)discard_line();

        if (choice == 1) {
            int event_id;

            printf("Enter Event ID to register: ");
            fflush(stdout);

            if (scanf("%d", &event_id) != 1) {
                int ch = getchar();
                if (ch == EOF) {
                    printf("\nInput closed. Ending registration session...\n");
                    break;
                }
                while (ch != '\n' && ch != EOF) {ch = getchar();}
                printf("Invalid Event ID format\n");
                continue;
            }

            (void)discard_line();

            written = snprintf(buffer, sizeof(buffer), "REG %d\n", event_id);
            if (written < 0 ||
                (size_t)written >= sizeof(buffer) ||
                send_all(sock, buffer, (size_t)written) < 0) {
                fprintf(stderr,
                        "Server disconnected unexpectedly\n");
                break;
            }

            received = recv_line(sock, buffer, sizeof(buffer));
            if (received <= 0) {
                fprintf(stderr, "Server disconnected unexpectedly\n");
                break;
            }

            if (strncmp(buffer, "REG_OK ", 7) == 0) {
                printf("\nSUCCESS %s\n", buffer + 7);
            } else if (strncmp(buffer, "REG_DUP ", 8) == 0) {
                printf("\nREJECTED - DUPLICATE %s\n", buffer + 8);
            } else if (strncmp(buffer, "REG_FULL ", 9) == 0) {
                printf("\nREJECTED - FULL %s\n", buffer + 9);
            } else if (strncmp(buffer, "REG_FAIL ", 9) == 0) {
                printf("\nERROR %s\n", buffer + 9);
            } else {
                printf("\nERROR Unexpected server response: %s\n", buffer);
	    }
	  } else if (choice == 2) {
	       if (send_all(sock, "EXIT\n", 5) < 0) {
        	   fprintf(stderr,
                	"Could not send exit command; closing connection.\n");
    	       } else {
        	   printf("\nRegistration session closed. Goodbye, %s.\n",
               	   	student_id);
    	       }
    	       break;
	  } else {
            printf("Invalid option. Try again.\n");
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}

