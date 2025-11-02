# Makefile for TFTP WRQ Server and Client Project

# --- Variables ---
CC = gcc  
CFLAGS = -g -Wall -Wextra -std=c99
SERVER_TARGET = tftpdServer
CLIENT_TARGET = tftp_client
SERVER_SOURCE = .//Server//tftpdServer.c
CLIENT_SOURCE = .//Client//tftp_client_wrq.c

# --- Targets ---

.PHONY: all clean server client run_server run_client

# Default target: builds both server and client
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Rule to build the Server executable
$(SERVER_TARGET): $(SERVER_SOURCE)
	$(CC) $(CFLAGS) $(SERVER_SOURCE) -o $(SERVER_TARGET)

# Rule to build the Client executable
$(CLIENT_TARGET): $(CLIENT_SOURCE)
	$(CC) $(CFLAGS) $(CLIENT_SOURCE) -o $(CLIENT_TARGET)

# --- Execution Targets ---

# Run the Server (Requires sudo for port 69)
run_server: $(SERVER_TARGET)
	@echo "--- Starting TFTP Server (Requires sudo for port 69) ---"
	@echo "File transfers will be saved in the current directory."
	sudo ./$(SERVER_TARGET)

# Run the Client (Localhost example)
# NOTE: Requires a file named 'test_file.txt' to exist in the directory.
run_client: $(CLIENT_TARGET)
	@echo "--- Starting TFTP Client (Sending 'test_file.txt' to 127.0.0.1) ---"
	# Example usage: <server_ip> <local_file> <remote_filename>
	./$(CLIENT_TARGET) 127.0.0.1 test_file.txt remote_upload.txt

# --- Cleanup Target ---

clean:
	@echo "--- Cleaning up project files ---"
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)