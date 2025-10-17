# Makefile for Telnet Echo Servers

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lpthread
TARGETS = line_mode_server char_mode_server line_mode_binary_server

.PHONY: all clean run-line run-char run-binary help

# Build all servers
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

# Run line mode server
run-line: line_mode_server
	@echo "Starting Line Mode Echo Server on port 9091..."
	./line_mode_server

# Run character mode server
run-char: char_mode_server
	@echo "Starting Character Mode Echo Server on port 9092..."
	./char_mode_server

# Run line mode binary server
run-binary: line_mode_binary_server
	@echo "Starting Line Mode Binary Echo Server on port 9093..."
	./line_mode_binary_server

# Clean build artifacts
clean:
	rm -f $(TARGETS)
	@echo "Cleaned build artifacts"

# Show help
help:
	@echo "Telnet Echo Server Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                          - Build all servers"
	@echo "  make line_mode_server         - Build line mode server only"
	@echo "  make char_mode_server         - Build character mode server only"
	@echo "  make line_mode_binary_server  - Build line mode binary server only"
	@echo "  make run-line                 - Build and run line mode server (port 9091)"
	@echo "  make run-char                 - Build and run character mode server (port 9092)"
	@echo "  make run-binary               - Build and run line mode binary server (port 9093)"
	@echo "  make clean                    - Remove build artifacts"
	@echo "  make help                     - Show this help message"
	@echo ""
	@echo "To test the servers:"
	@echo "  telnet localhost 9091  - Connect to line mode server"
	@echo "  telnet localhost 9092  - Connect to character mode server"
	@echo "  telnet localhost 9093  - Connect to line mode binary server (UTF-8 support)"
