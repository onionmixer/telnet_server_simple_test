#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define PORT 9092
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// Telnet protocol codes
#define IAC  255  // Interpret As Command
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250  // Subnegotiation Begin
#define SE   240  // Subnegotiation End
#define ECHO 1
#define SUPPRESS_GO_AHEAD 3
#define LINEMODE 34

// Control characters
#define CTRL_C 3
#define CTRL_D 4
#define BACKSPACE 8
#define DEL 127

volatile sig_atomic_t running = 1;

// Thread data structure for timestamp sender
typedef struct {
    int client_fd;
    volatile int *stop_flag;
    pthread_mutex_t *socket_mutex;
} timestamp_thread_data_t;

// Telnet negotiation tracking
typedef struct {
    int echo_acked;
    int sga_acked;
    int ready_sent;
} telnet_negotiation_t;

// Get current timestamp string in format [YYYY-MM-DD HH:MM:SS]
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(buffer, size, "[%04d-%02d-%02d %02d:%02d:%02d]",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
}

void signal_handler(int signum) {
    running = 0;
}

void send_telnet_option(int client_fd, unsigned char command, unsigned char option) {
    unsigned char buf[3];
    buf[0] = IAC;
    buf[1] = command;
    buf[2] = option;
    send(client_fd, buf, 3, 0);
}

// Thread function to send timestamp every 10 seconds
void *timestamp_sender_thread(void *arg) {
    timestamp_thread_data_t *data = (timestamp_thread_data_t *)arg;
    int client_fd = data->client_fd;
    volatile int *stop_flag = data->stop_flag;
    pthread_mutex_t *socket_mutex = data->socket_mutex;

    while (!(*stop_flag) && running) {
        sleep(10);  // Wait 10 seconds

        if (*stop_flag || !running) {
            break;
        }

        // Get current timestamp
        time_t now = time(NULL);
        char timestamp_msg[128];
        struct tm *tm_info = localtime(&now);

        snprintf(timestamp_msg, sizeof(timestamp_msg),
                 "\r\n[TIMESTAMP] %04d-%02d-%02d %02d:%02d:%02d\r\n",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

        // Send timestamp to client (thread-safe)
        pthread_mutex_lock(socket_mutex);
        ssize_t sent = send(client_fd, timestamp_msg, strlen(timestamp_msg), MSG_NOSIGNAL);
        pthread_mutex_unlock(socket_mutex);

        if (sent <= 0) {
            // Client disconnected or error
            break;
        }

        char ts[32];
        get_timestamp(ts, sizeof(ts));
        printf("%s[CHAR MODE] Sent timestamp to client (fd=%d)\n", ts, client_fd);
    }

    return NULL;
}

void setup_charmode(int client_fd, telnet_negotiation_t *negotiation) {
    // Negotiate character mode (disable line mode)
    send_telnet_option(client_fd, DONT, LINEMODE);
    send_telnet_option(client_fd, WILL, ECHO);
    // Many telnet clients don't explicitly respond to WILL, mark as acked
    negotiation->echo_acked = 1;
    send_telnet_option(client_fd, WILL, SUPPRESS_GO_AHEAD);
    send_telnet_option(client_fd, DO, SUPPRESS_GO_AHEAD);
}

void handle_client(int client_fd, struct sockaddr_in *client_addr) {
    unsigned char buffer[BUFFER_SIZE];
    int bytes_read;
    char client_ip[INET_ADDRSTRLEN];
    char input_line[BUFFER_SIZE] = {0};
    int input_pos = 0;

    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    char ts[32];
    get_timestamp(ts, sizeof(ts));
    printf("%s[CHAR MODE] Client connected: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    // Initialize negotiation tracking
    telnet_negotiation_t negotiation = {
        .echo_acked = 0,
        .sga_acked = 0,
        .ready_sent = 0
    };

    // Setup character mode
    setup_charmode(client_fd, &negotiation);

    // Send welcome message
    const char *welcome = "Welcome to Character Mode Echo Server (Port 9092)\r\n";
    const char *instruction = "Each character is echoed immediately as you type.\r\n";
    const char *quit_msg = "Press Ctrl+D or type 'quit' and Enter to disconnect.\r\n";
    const char *timestamp_info = "A timestamp will be sent every 10 seconds.\r\n";
    const char *negotiating = "Negotiating telnet options...\r\n\r\n";
    send(client_fd, welcome, strlen(welcome), 0);
    send(client_fd, instruction, strlen(instruction), 0);
    send(client_fd, quit_msg, strlen(quit_msg), 0);
    send(client_fd, timestamp_info, strlen(timestamp_info), 0);
    send(client_fd, negotiating, strlen(negotiating), 0);

    // Initialize thread data for timestamp sender
    volatile int stop_flag = 0;
    pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;
    timestamp_thread_data_t thread_data = {
        .client_fd = client_fd,
        .stop_flag = &stop_flag,
        .socket_mutex = &socket_mutex
    };

    // Create timestamp sender thread
    pthread_t timestamp_thread;
    if (pthread_create(&timestamp_thread, NULL, timestamp_sender_thread, &thread_data) != 0) {
        perror("Failed to create timestamp thread");
        close(client_fd);
        return;
    }

    get_timestamp(ts, sizeof(ts));
    printf("%s[CHAR MODE] Timestamp thread started for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                get_timestamp(ts, sizeof(ts));
                printf("%s[CHAR MODE] Client disconnected: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));
            } else {
                perror("recv error");
            }
            break;
        }

        // Process each byte
        for (int i = 0; i < bytes_read; i++) {
            unsigned char ch = buffer[i];

            // Handle telnet protocol commands
            if (ch == IAC) {
                if (i + 1 < bytes_read) {
                    unsigned char cmd = buffer[i + 1];
                    if (cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) {
                        if (i + 2 < bytes_read) {
                            unsigned char opt = buffer[i + 2];

                            // Respond to telnet negotiations
                            if (cmd == DO) {
                                if (opt == ECHO) {
                                    send_telnet_option(client_fd, WILL, opt);
                                    negotiation.echo_acked = 1;
                                } else if (opt == SUPPRESS_GO_AHEAD) {
                                    send_telnet_option(client_fd, WILL, opt);
                                    negotiation.sga_acked = 1;
                                } else {
                                    send_telnet_option(client_fd, WONT, opt);
                                }
                            } else if (cmd == DONT) {
                                send_telnet_option(client_fd, WONT, opt);
                            } else if (cmd == WILL) {
                                if (opt == SUPPRESS_GO_AHEAD) {
                                    send_telnet_option(client_fd, DO, opt);
                                    negotiation.sga_acked = 1;
                                } else {
                                    send_telnet_option(client_fd, DONT, opt);
                                }
                            } else if (cmd == WONT) {
                                send_telnet_option(client_fd, DONT, opt);
                            }

                            // Check if negotiation is complete and send "ready!" message
                            if (!negotiation.ready_sent &&
                                negotiation.echo_acked &&
                                negotiation.sga_acked) {

                                const char *ready_msg = "\r\n*** READY! ***\r\n\r\n";
                                pthread_mutex_lock(&socket_mutex);
                                send(client_fd, ready_msg, strlen(ready_msg), 0);
                                pthread_mutex_unlock(&socket_mutex);
                                negotiation.ready_sent = 1;
                                get_timestamp(ts, sizeof(ts));
                                printf("%s[CHAR MODE] Negotiation complete for client %s:%d\n",
                                       ts, client_ip, ntohs(client_addr->sin_port));
                            }

                            i += 2;
                            continue;
                        }
                    } else if (cmd == IAC) {
                        // Escaped IAC (255), treat as regular character
                        ch = IAC;
                        i++;
                    } else {
                        i++;
                        continue;
                    }
                } else {
                    continue;
                }
            }

            // Handle control characters
            if (ch == CTRL_D) {
                // Ctrl+D: disconnect
                const char *goodbye = "\r\nGoodbye!\r\n";
                pthread_mutex_lock(&socket_mutex);
                send(client_fd, goodbye, strlen(goodbye), 0);
                pthread_mutex_unlock(&socket_mutex);
                get_timestamp(ts, sizeof(ts));
                printf("%s[CHAR MODE] Client sent Ctrl+D: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));
                goto cleanup;
            } else if (ch == CTRL_C) {
                // Ctrl+C: clear current line
                const char *clear = "\r\n";
                pthread_mutex_lock(&socket_mutex);
                send(client_fd, clear, strlen(clear), 0);
                pthread_mutex_unlock(&socket_mutex);
                input_pos = 0;
                memset(input_line, 0, sizeof(input_line));
                continue;
            } else if (ch == BACKSPACE || ch == DEL) {
                // Backspace/Delete
                if (input_pos > 0) {
                    input_pos--;
                    input_line[input_pos] = '\0';
                    // Send backspace sequence: backspace, space, backspace
                    const char *bs_seq = "\b \b";
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, bs_seq, strlen(bs_seq), 0);
                    pthread_mutex_unlock(&socket_mutex);
                }
                continue;
            } else if (ch == '\r' || ch == '\n') {
                // Newline: process the line
                if (ch == '\r') {
                    // Send CRLF
                    const char *crlf = "\r\n";
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, crlf, strlen(crlf), 0);
                    pthread_mutex_unlock(&socket_mutex);
                }

                // Check for quit command
                if (input_pos > 0 && strcmp(input_line, "quit") == 0) {
                    const char *goodbye = "Goodbye!\r\n";
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, goodbye, strlen(goodbye), 0);
                    pthread_mutex_unlock(&socket_mutex);
                    get_timestamp(ts, sizeof(ts));
                    printf("%s[CHAR MODE] Client quit: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));
                    goto cleanup;
                }

                // Echo the complete line if not empty
                if (input_pos > 0) {
                    char echo_msg[BUFFER_SIZE + 20];
                    snprintf(echo_msg, sizeof(echo_msg), "ECHO: %s\r\n", input_line);
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, echo_msg, strlen(echo_msg), 0);
                    pthread_mutex_unlock(&socket_mutex);
                    get_timestamp(ts, sizeof(ts));
                    printf("%s[CHAR MODE] Echoed line to %s:%d: %s\n",
                           ts, client_ip, ntohs(client_addr->sin_port), input_line);
                }

                // Reset input buffer
                input_pos = 0;
                memset(input_line, 0, sizeof(input_line));
                continue;
            } else if (ch >= 32) {
                // Printable character or multibyte data (encoding-neutral)
                // Supports ASCII (0x20-0x7F), UTF-8, EUC-KR, EUC-JP, Shift-JIS, etc.
                if (input_pos < BUFFER_SIZE - 1) {
                    input_line[input_pos++] = ch;
                    input_line[input_pos] = '\0';

                    // Echo the character immediately
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, &ch, 1, 0);
                    pthread_mutex_unlock(&socket_mutex);

                    // Log character (optional, can be verbose)
                    // printf("[CHAR MODE] Char from %s:%d: 0x%02X\n",
                    //        client_ip, ntohs(client_addr->sin_port), ch);
                }
            }
            // Ignore other control characters (0x00-0x1F except handled ones)
        }
    }

cleanup:
    // Stop the timestamp thread
    stop_flag = 1;
    get_timestamp(ts, sizeof(ts));
    printf("%s[CHAR MODE] Stopping timestamp thread for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    // Wait for timestamp thread to finish
    pthread_join(timestamp_thread, NULL);
    get_timestamp(ts, sizeof(ts));
    printf("%s[CHAR MODE] Timestamp thread stopped for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    // Cleanup
    pthread_mutex_destroy(&socket_mutex);
    close(client_fd);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int opt = 1;

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, MAX_CLIENTS) == -1) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    char ts[32];
    get_timestamp(ts, sizeof(ts));
    printf("%sCharacter Mode Telnet Echo Server started on port %d\n", ts, PORT);
    printf("Press Ctrl+C to stop the server\n\n");

    // Accept and handle clients
    while (running) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) {
            perror("select error");
            break;
        }

        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

            if (client_fd == -1) {
                if (errno != EINTR) {
                    perror("accept failed");
                }
                continue;
            }

            pid_t pid = fork();

            if (pid == 0) {
                // Child process
                close(server_fd);
                handle_client(client_fd, &client_addr);
                exit(EXIT_SUCCESS);
            } else if (pid > 0) {
                // Parent process
                close(client_fd);
            } else {
                perror("fork failed");
                close(client_fd);
            }
        }
    }

    get_timestamp(ts, sizeof(ts));
    printf("\n%sShutting down server...\n", ts);
    close(server_fd);
    return 0;
}
