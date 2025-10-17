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

#define PORT 9093
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
#define BINARY 0
#define ECHO 1
#define SUPPRESS_GO_AHEAD 3
#define LINEMODE 34

// LINEMODE suboption (RFC 1184)
#define LM_MODE 1
#define LM_FORWARDMASK 2
#define LM_SLC 3

// LINEMODE MODE bits (RFC 1184)
#define MODE_EDIT 0x01      // Local line editing
#define MODE_TRAPSIG 0x02   // Signal trapping
#define MODE_ACK 0x04       // Mode change acknowledgment

volatile sig_atomic_t running = 1;

// Thread data structure for timestamp sender
typedef struct {
    int client_fd;
    volatile int *stop_flag;
    pthread_mutex_t *socket_mutex;
} timestamp_thread_data_t;

// Telnet negotiation tracking
typedef struct {
    int binary_acked;
    int linemode_acked;
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

// UTF-8 helper functions
// Returns the expected length of a UTF-8 sequence based on the lead byte
// Returns 0 if the byte is not a valid UTF-8 lead byte
int utf8_sequence_length(unsigned char lead_byte) {
    if (lead_byte < 0x80) return 1;        // 0xxxxxxx: ASCII (1 byte)
    if ((lead_byte & 0xE0) == 0xC0) return 2; // 110xxxxx: 2-byte sequence
    if ((lead_byte & 0xF0) == 0xE0) return 3; // 1110xxxx: 3-byte sequence (Korean, etc.)
    if ((lead_byte & 0xF8) == 0xF0) return 4; // 11110xxx: 4-byte sequence
    return 0; // Invalid lead byte
}

// Check if the buffer ends with an incomplete UTF-8 sequence
// Returns the number of bytes at the end that form an incomplete sequence
int check_incomplete_utf8(const unsigned char *buf, int len) {
    if (len == 0) return 0;

    // Check last 1-3 bytes for incomplete sequences
    for (int i = 1; i <= 4 && i <= len; i++) {
        int pos = len - i;
        unsigned char byte = buf[pos];

        // Check if this is a UTF-8 lead byte
        int expected_len = utf8_sequence_length(byte);
        if (expected_len > 0) {
            // Found a lead byte, check if sequence is complete
            int actual_len = len - pos;
            if (actual_len < expected_len) {
                // Incomplete sequence
                return actual_len;
            } else {
                // Complete sequence
                return 0;
            }
        }

        // Continue if it's a continuation byte (10xxxxxx)
        if ((byte & 0xC0) != 0x80) {
            // Not a continuation byte and not a valid lead byte
            return 0;
        }
    }

    return 0;
}

// Find line ending in buffer, returns position after the line ending
// Supports CRLF, CR NUL, LF, and CR alone
// Returns -1 if no line ending found
int find_line_ending(const unsigned char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\r') {
            // Check for CRLF or CR NUL
            if (i + 1 < len) {
                if (buf[i + 1] == '\n' || buf[i + 1] == '\0') {
                    return i + 2; // Position after CRLF or CR NUL
                }
            }
            // CR alone at end of buffer, wait for next recv
            if (i + 1 == len) {
                return -1;
            }
            // CR followed by something else, treat as line ending
            return i + 1;
        } else if (buf[i] == '\n') {
            // LF alone
            return i + 1;
        }
    }
    return -1;
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
        printf("%s[LINE MODE BINARY] Sent timestamp to client (fd=%d)\n", ts, client_fd);
    }

    return NULL;
}

void setup_linemode(int client_fd, telnet_negotiation_t *negotiation) {
    // Step 1: Enable BINARY mode for 8-bit transparency (UTF-8 support)
    send_telnet_option(client_fd, DO, BINARY);
    send_telnet_option(client_fd, WILL, BINARY);
    // Mark as acked - most clients accept BINARY silently
    negotiation->binary_acked = 1;

    // Step 2: Request LINEMODE from client
    send_telnet_option(client_fd, DO, LINEMODE);

    // Step 3: For true line mode, client should do local echo
    // So server should NOT echo (WONT ECHO instead of WILL ECHO)
    send_telnet_option(client_fd, WONT, ECHO);
    // Many telnet clients don't respond to WONT, so mark as acked immediately
    negotiation->echo_acked = 1;

    // Step 4: Suppress Go-Ahead for efficiency
    send_telnet_option(client_fd, WILL, SUPPRESS_GO_AHEAD);
    send_telnet_option(client_fd, DO, SUPPRESS_GO_AHEAD);

    // Step 5: Send LINEMODE MODE subnegotiation with EDIT bit enabled
    // Format: IAC SB LINEMODE LM_MODE MODE_VALUE IAC SE
    // MODE_VALUE = MODE_EDIT (0x01) for line editing
    // Or MODE_EDIT | MODE_TRAPSIG (0x03) for line editing + signal trapping
    unsigned char linemode_cmd[] = {
        IAC, SB, LINEMODE,
        LM_MODE,              // MODE command
        MODE_EDIT,            // Enable EDIT bit (0x01) for true line mode
        IAC, SE
    };
    send(client_fd, linemode_cmd, sizeof(linemode_cmd), 0);

    char ts[32];
    get_timestamp(ts, sizeof(ts));
    printf("%s[LINE MODE BINARY] Negotiation sent: BINARY, LINEMODE, WONT ECHO, MODE=0x%02x (EDIT enabled)\n",
           ts, MODE_EDIT);
}

void handle_client(int client_fd, struct sockaddr_in *client_addr) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char line_buf[BUFFER_SIZE * 2]; // Accumulation buffer for line data
    int line_len = 0;
    int bytes_read;
    char client_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    char ts[32];
    get_timestamp(ts, sizeof(ts));
    printf("%s[LINE MODE BINARY] Client connected: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    // Initialize negotiation tracking
    telnet_negotiation_t negotiation = {
        .binary_acked = 0,
        .linemode_acked = 0,
        .echo_acked = 0,
        .sga_acked = 0,
        .ready_sent = 0
    };

    // Setup line mode with binary
    setup_linemode(client_fd, &negotiation);

    // Send welcome message
    const char *welcome = "Welcome to Line Mode Binary Echo Server (Port 9093)\r\n";
    const char *instruction = "Type a line and press Enter. It will be echoed back.\r\n";
    const char *quit_msg = "Type 'quit' to disconnect.\r\n";
    const char *timestamp_info = "A timestamp will be sent every 10 seconds.\r\n";
    const char *binary_info = "BINARY mode enabled for UTF-8 support.\r\n";
    const char *negotiating = "Negotiating telnet options...\r\n\r\n";
    send(client_fd, welcome, strlen(welcome), 0);
    send(client_fd, instruction, strlen(instruction), 0);
    send(client_fd, quit_msg, strlen(quit_msg), 0);
    send(client_fd, timestamp_info, strlen(timestamp_info), 0);
    send(client_fd, binary_info, strlen(binary_info), 0);
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
    printf("%s[LINE MODE BINARY] Timestamp thread started for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                get_timestamp(ts, sizeof(ts));
                printf("%s[LINE MODE BINARY] Client disconnected: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));
            } else {
                perror("recv error");
            }
            break;
        }

        // Extract data bytes from telnet protocol stream
        unsigned char data[BUFFER_SIZE];
        int data_len = 0;
        int i = 0;

        while (i < bytes_read) {
            if ((unsigned char)buffer[i] == IAC) {
                if (i + 1 < bytes_read) {
                    unsigned char cmd = (unsigned char)buffer[i + 1];

                    // Handle IAC IAC (escaped 255) - restore to single 0xFF
                    if (cmd == IAC) {
                        data[data_len++] = IAC;
                        i += 2;
                        continue;
                    }

                    // Handle DO/DONT/WILL/WONT options
                    if (cmd == DO || cmd == DONT || cmd == WILL || cmd == WONT) {
                        if (i + 2 < bytes_read) {
                            unsigned char opt = (unsigned char)buffer[i + 2];

                            // Respond to client's option requests
                            if (cmd == DO) {
                                // Client asks us to enable an option
                                if (opt == BINARY) {
                                    send_telnet_option(client_fd, WILL, opt);
                                    negotiation.binary_acked = 1;
                                } else if (opt == SUPPRESS_GO_AHEAD) {
                                    send_telnet_option(client_fd, WILL, opt);
                                    negotiation.sga_acked = 1;
                                } else if (opt == ECHO) {
                                    send_telnet_option(client_fd, WONT, opt);
                                    negotiation.echo_acked = 1;
                                } else {
                                    send_telnet_option(client_fd, WONT, opt);
                                }
                            } else if (cmd == DONT) {
                                send_telnet_option(client_fd, WONT, opt);
                                if (opt == ECHO) {
                                    negotiation.echo_acked = 1;
                                } else if (opt == BINARY) {
                                    negotiation.binary_acked = 1;
                                }
                            } else if (cmd == WILL) {
                                // Client agrees to enable an option
                                if (opt == BINARY) {
                                    send_telnet_option(client_fd, DO, opt);
                                    negotiation.binary_acked = 1;
                                } else if (opt == LINEMODE) {
                                    send_telnet_option(client_fd, DO, opt);
                                    negotiation.linemode_acked = 1;
                                } else if (opt == SUPPRESS_GO_AHEAD) {
                                    send_telnet_option(client_fd, DO, opt);
                                    negotiation.sga_acked = 1;
                                } else if (opt == ECHO) {
                                    send_telnet_option(client_fd, DO, opt);
                                    negotiation.echo_acked = 1;
                                } else {
                                    send_telnet_option(client_fd, DONT, opt);
                                }
                            } else if (cmd == WONT) {
                                send_telnet_option(client_fd, DONT, opt);
                                if (opt == LINEMODE) {
                                    negotiation.linemode_acked = 1;
                                } else if (opt == BINARY) {
                                    negotiation.binary_acked = 1;
                                }
                            }

                            // Check if negotiation is complete and send "ready!" message
                            if (!negotiation.ready_sent &&
                                negotiation.binary_acked &&
                                negotiation.linemode_acked &&
                                negotiation.echo_acked &&
                                negotiation.sga_acked) {

                                const char *ready_msg = "\r\n*** READY! (BINARY mode active) ***\r\n\r\n";
                                pthread_mutex_lock(&socket_mutex);
                                send(client_fd, ready_msg, strlen(ready_msg), 0);
                                pthread_mutex_unlock(&socket_mutex);
                                negotiation.ready_sent = 1;
                                get_timestamp(ts, sizeof(ts));
                                printf("%s[LINE MODE BINARY] Negotiation complete for client %s:%d\n",
                                       ts, client_ip, ntohs(client_addr->sin_port));
                            }

                            i += 3; // Skip IAC, command, option
                            continue;
                        } else {
                            // Incomplete sequence, skip rest
                            break;
                        }
                    }

                    // Handle Subnegotiation Begin (IAC SB ... IAC SE)
                    if (cmd == SB) {
                        // Find the end of subnegotiation (IAC SE)
                        int j = i + 2;
                        while (j < bytes_read - 1) {
                            if ((unsigned char)buffer[j] == IAC &&
                                (unsigned char)buffer[j + 1] == SE) {
                                i = j + 2;
                                break;
                            }
                            j++;
                        }
                        if (j >= bytes_read - 1) {
                            // Incomplete subnegotiation, skip rest
                            break;
                        }
                        continue;
                    }

                    // Skip other IAC commands
                    i += 2;
                } else {
                    // IAC at end of buffer
                    break;
                }
            } else {
                // Regular data byte
                data[data_len++] = buffer[i++];
            }
        }

        // Append extracted data to line buffer
        if (data_len > 0) {
            // Check if buffer has enough space
            if (line_len + data_len > sizeof(line_buf)) {
                get_timestamp(ts, sizeof(ts));
                printf("%s[LINE MODE BINARY] Line buffer overflow, resetting\n", ts);
                line_len = 0;
            }
            memcpy(line_buf + line_len, data, data_len);
            line_len += data_len;
        }

        // Check for incomplete UTF-8 sequence at end of buffer
        int incomplete_bytes = check_incomplete_utf8(line_buf, line_len);
        int process_len = line_len - incomplete_bytes;

        // Look for line endings in the processable portion
        int line_end_pos = find_line_ending(line_buf, process_len);

        while (line_end_pos > 0) {
            // Extract the line content (without line ending)
            int line_content_len = line_end_pos;
            // Remove the line ending characters
            while (line_content_len > 0 &&
                   (line_buf[line_content_len-1] == '\r' ||
                    line_buf[line_content_len-1] == '\n' ||
                    line_buf[line_content_len-1] == '\0')) {
                line_content_len--;
            }

            // Process the line if not empty
            if (line_content_len > 0) {
                // Null-terminate for string operations
                unsigned char line_content[BUFFER_SIZE * 2];
                memcpy(line_content, line_buf, line_content_len);
                line_content[line_content_len] = '\0';

                // Check for quit command
                if (strcmp((char*)line_content, "quit") == 0) {
                    const char *goodbye = "Goodbye!\r\n";
                    pthread_mutex_lock(&socket_mutex);
                    send(client_fd, goodbye, strlen(goodbye), 0);
                    pthread_mutex_unlock(&socket_mutex);
                    get_timestamp(ts, sizeof(ts));
                    printf("%s[LINE MODE BINARY] Client quit: %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));
                    goto cleanup;
                }

                // Echo back the line
                char echo_msg[BUFFER_SIZE * 2 + 20];
                snprintf(echo_msg, sizeof(echo_msg), "ECHO: %s\r\n", line_content);

                pthread_mutex_lock(&socket_mutex);
                send(client_fd, echo_msg, strlen(echo_msg), 0);
                pthread_mutex_unlock(&socket_mutex);

                get_timestamp(ts, sizeof(ts));
                printf("%s[LINE MODE BINARY] Echoed to %s:%d: %s\n",
                       ts, client_ip, ntohs(client_addr->sin_port), line_content);
            }

            // Remove processed line from buffer
            memmove(line_buf, line_buf + line_end_pos, line_len - line_end_pos);
            line_len -= line_end_pos;

            // Recalculate incomplete UTF-8 bytes
            incomplete_bytes = check_incomplete_utf8(line_buf, line_len);
            process_len = line_len - incomplete_bytes;

            // Check for another line ending
            line_end_pos = find_line_ending(line_buf, process_len);
        }
    }

cleanup:

    // Stop the timestamp thread
    stop_flag = 1;
    get_timestamp(ts, sizeof(ts));
    printf("%s[LINE MODE BINARY] Stopping timestamp thread for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

    // Wait for timestamp thread to finish
    pthread_join(timestamp_thread, NULL);
    get_timestamp(ts, sizeof(ts));
    printf("%s[LINE MODE BINARY] Timestamp thread stopped for client %s:%d\n", ts, client_ip, ntohs(client_addr->sin_port));

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
    printf("%sLine Mode Telnet Echo Server started on port %d\n", ts, PORT);
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
