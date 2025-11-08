# Makefile for TFTP WRQ Server and Client Project

# --- Variables ---
CC = gcc  
CFLAGS = -g -Wall -Wextra -std=c99
SERVER_TARGET = .//server//tftpdServer
CLIENT_WRITE_TARGET = .//writeClient//tftp_write_client
CLIENT_READ_TARGET = .//readClient//tftp_read_client
SERVER_SOURCE = .//ServerSource//*.c
CLIENT_WRITE_SOURCE = .//ClientWriteSource//*.c
CLIENT_READ_SOURCE = .//ClientReadSource//*.c

# --- Targets ---

.PHONY: all clean server client run_server run_client

	
# Default target: builds both server and client
all:  $(SERVER_TARGET) $(CLIENT_WRITE_TARGET) $(CLIENT_READ_TARGET)

SERVER_DIR = ./server
# Rule to build the Server executable
$(SERVER_TARGET): $(SERVER_SOURCE)  | $(SERVER_DIR)
	$(CC) $(CFLAGS) $(SERVER_SOURCE) -o $(SERVER_TARGET)

$(SERVER_DIR):
	@mkdir -p $(SERVER_DIR)

# Rule to build the Client executable
$(CLIENT_WRITE_TARGET): $(CLIENT_WRITE_SOURCE)
	$(CC) $(CFLAGS) $(CLIENT_WRITE_SOURCE) -o $(CLIENT_WRITE_TARGET)

# Rule to build the Client Read executable
$(CLIENT_READ_TARGET): $(CLIENT_READ_SOURCE)
	$(CC) $(CFLAGS) $(CLIENT_READ_SOURCE) -o $(CLIENT_READ_TARGET)

# --- Execution Targets ---

# Run the Server (Requires sudo for port 69)
run_server: $(SERVER_TARGET)
	@echo "--- Starting TFTP Server (Requires sudo for port 69) ---"
	@echo "File transfers will be saved in the current directory."
	sudo ./$(SERVER_TARGET)

# Run the Client (Localhost example)
# NOTE: Requires a file named 'test_file.txt' to exist in the directory.
run_client_write: $(CLIENT_WRITE_TARGET)
	@echo "--- Starting TFTP Client (Sending 'test_file.txt' to 127.0.0.1) ---"
	# Example usage: <server_ip> <local_file> <remote_filename>
	./$(CLIENT_WRITE_TARGET) 127.0.0.1 test_file.txt remote_upload.txt

# --- Cleanup Target ---

clean:
	@echo "--- Cleaning up project files ---"
	rm -f $(SERVER_TARGET) $(CLIENT_WRITE_TARGET) $(CLIENT_READ_TARGET)