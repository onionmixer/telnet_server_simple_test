# Makefile for Telnet Echo Servers

CC = gcc
CFLAGS = -Wall -Wextra -O2
CFLAGS_DEBUG = -Wall -Wextra -g -DDEBUG=1
LDFLAGS = -lpthread
TARGETS = line_mode_server char_mode_server line_mode_binary_server

.PHONY: all debug clean help

# Build all servers (release mode)
all: $(TARGETS)

# Build line mode server
line_mode_server: line_mode_server.c
	$(CC) $(CFLAGS) -o line_mode_server line_mode_server.c $(LDFLAGS)

# Build character mode server
char_mode_server: char_mode_server.c
	$(CC) $(CFLAGS) -o char_mode_server char_mode_server.c $(LDFLAGS)

# Build line mode binary server
line_mode_binary_server: line_mode_binary_server.c
	$(CC) $(CFLAGS) -o line_mode_binary_server line_mode_binary_server.c $(LDFLAGS)

# Build all servers in debug mode (with core dump support)
debug:
	@echo "Building servers in DEBUG mode with core dump support..."
	$(CC) $(CFLAGS_DEBUG) -o line_mode_server line_mode_server.c $(LDFLAGS)
	$(CC) $(CFLAGS_DEBUG) -o char_mode_server char_mode_server.c $(LDFLAGS)
	$(CC) $(CFLAGS_DEBUG) -o line_mode_binary_server line_mode_binary_server.c $(LDFLAGS)
	@echo "Debug build complete. Core dumps enabled (use 'ulimit -c unlimited' to enable core dumps)"

# Clean build artifacts
clean:
	rm -f $(TARGETS) core
	@echo "Cleaned build artifacts"

# Show help
help:
	@echo "Telnet Echo Server Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                          - Build all servers (release mode)"
	@echo "  make debug                    - Build all servers in DEBUG mode with core dump support"
	@echo "  make line_mode_server         - Build line mode server only"
	@echo "  make char_mode_server         - Build character mode server only"
	@echo "  make line_mode_binary_server  - Build line mode binary server only"
	@echo "  make clean                    - Remove build artifacts"
	@echo "  make help                     - Show this help message"
	@echo ""
	@echo "Running servers:"
	@echo "  ./line_mode_server            - Run line mode server (port 9091)"
	@echo "  ./char_mode_server            - Run character mode server (port 9092)"
	@echo "  ./line_mode_binary_server     - Run line mode binary server (port 9093)"
	@echo ""
	@echo "To test the servers:"
	@echo "  telnet localhost 9091         - Connect to line mode server"
	@echo "  telnet localhost 9092         - Connect to character mode server"
	@echo "  telnet localhost 9093         - Connect to line mode binary server (UTF-8 support)"
	@echo ""
	@echo "Debug mode features:"
	@echo "  - DEBUG messages enabled (shows detailed logs)"
	@echo "  - Core dump support enabled (use 'ulimit -c unlimited' before running)"
