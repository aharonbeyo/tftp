#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>

#define LISTEN_PORT 69
#define MAX_BUFFER_SIZE 516 // 2 (Opcode) + 2 (Block #) + 512 (Data)
#define TFTP_DATA_SIZE 512

// TFTP Opcodes (Network Byte Order)
#define OP_RRQ  1
#define OP_WRQ  2
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERROR 5

// --- Packet Construction/Parsing Helpers ---

// Parses the WRQ packet and extracts the filename.
// Returns 0 on success, -1 on failure.
int parse_wrq(const char *buffer, int len, char *filename_out, int max_len) {
    if (len < 6) return -1; // Minimum size: Opcode(2) + Filename(1) + 0(1) + Mode(1) + 0(1)
    
    // Skip Opcode (2 bytes)
    int offset = 2;
    
    // Find first null terminator (Filename)
    const char *filename_start = buffer + offset;
    const char *filename_end = memchr(filename_start, 0, len - offset);
    
    if (!filename_end) return -1; // No null terminator for filename
    
    int filename_len = filename_end - filename_start;
    if (filename_len >= max_len) return -1; // Filename too long
    
    strncpy(filename_out, filename_start, filename_len);
    filename_out[filename_len] = '\0';
    
    // Skip filename and its null terminator
    offset += filename_len + 1;
    
    // Find second null terminator (Mode) - we don't strictly need mode for this basic example
    const char *mode_start = buffer + offset;
    const char *mode_end = memchr(mode_start, 0, len - offset);
    
    if (!mode_end) return -1; // No null terminator for mode
    
    // Note: A full implementation would check the mode (e.g., "octet")
    
    return 0; // Success
}

// Builds an ACK packet for the given block number
int create_ack_packet(char *buffer, uint16_t block_num) {
    // Opcode (2 bytes)
    *(uint16_t *)buffer = htons(OP_ACK); 
    // Block Number (2 bytes)
    *(uint16_t *)(buffer + 2) = htons(block_num); 
    return 4; // ACK packets are always 4 bytes
}

// Builds an ERROR packet
int create_error_packet(char *buffer, uint16_t err_code, const char *err_msg) {
    // Opcode (2 bytes)
    *(uint16_t *)buffer = htons(OP_ERROR); 
    // Error Code (2 bytes)
    *(uint16_t *)(buffer + 2) = htons(err_code); 
    
    int len = 4;
    // Error Message (variable length)
    len += snprintf(buffer + 4, MAX_BUFFER_SIZE - 4, "%s", err_msg);
    // Null Terminator (1 byte)
    buffer[len++] = 0;
    
    return len;
}

// --- Main Server Logic ---

void start_tftp_server(int port) {
    int listen_sockfd, transfer_sockfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addr_len = sizeof(cli_addr);
    char buffer[MAX_BUFFER_SIZE];
    
    // 1. Create the listening socket on port 69
    if ((listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    
    if (bind(listen_sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        close(listen_sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("TFTP Server listening on UDP port %d...\n", port);
    
    while (1) {
        // --- A. Wait for Initial Request (RRQ/WRQ) on port 69 ---
        
        int n = recvfrom(listen_sockfd, buffer, MAX_BUFFER_SIZE, 0, 
                         (struct sockaddr *)&cli_addr, &addr_len);
        
        if (n < 4) {
            fprintf(stderr, "Received small packet (%d bytes). Ignoring.\n", n);
            continue;
        }
        
        uint16_t opcode = ntohs(*(uint16_t *)buffer);
        
        if (opcode == OP_WRQ) {
            // --- B. Handle WRQ ---
            char filename[256];
            if (parse_wrq(buffer, n, filename, sizeof(filename)) != 0) {
                fprintf(stderr, "Invalid WRQ packet received.\n");
                continue;
            }
            
            printf("WRQ received for file: %s from %s:%d\n", filename, 
                   inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            
            // --- C. Create a new socket for the transfer (new TID) ---
            
            if ((transfer_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                perror("Transfer socket creation failed");
                continue;
            }
            // Bind to an ephemeral port (port 0)
            if (bind(transfer_sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                perror("Transfer socket bind failed");
                close(transfer_sockfd);
                continue;
            }

            // Get the actual ephemeral port assigned
            struct sockaddr_in actual_addr;
            socklen_t actual_addr_len = sizeof(actual_addr);
            getsockname(transfer_sockfd, (struct sockaddr *)&actual_addr, &actual_addr_len);
            printf("Starting transfer on new port: %d\n", ntohs(actual_addr.sin_port));
            
            // --- D. Open file for writing ---
            
            FILE *fp = fopen(filename, "wb");
            if (!fp) {
                // Send Error 2: Access violation (or other IO error)
                int err_len = create_error_packet(buffer, 2, "Cannot open file for writing.");
                sendto(transfer_sockfd, buffer, err_len, 0, (struct sockaddr *)&cli_addr, addr_len);
                fprintf(stderr, "Access violation or IO error opening file: %s\n", filename);
                close(transfer_sockfd);
                continue;
            }
            
            // --- E. Send initial ACK (Block 0) from the new port ---
            
            int ack_len = create_ack_packet(buffer, 0);
            if (sendto(transfer_sockfd, buffer, ack_len, 0, (struct sockaddr *)&cli_addr, addr_len) < 0) {
                perror("Error sending ACK 0");
                fclose(fp);
                close(transfer_sockfd);
                continue;
            }
            
            // --- F. Data Transfer Loop (Stop-and-Wait) ---
            
            uint16_t expected_block = 1;
            int total_bytes = 0;
            int done = 0;

            // TODO: Implement a timeout mechanism here (e.g., using select() or SO_RCVTIMEO)

            while (!done) {
                // 1. Receive DATA packet
                addr_len = sizeof(cli_addr); // Always reset for safety
                n = recvfrom(transfer_sockfd, buffer, MAX_BUFFER_SIZE, 0, 
                             (struct sockaddr *)&cli_addr, &addr_len);
                
                if (n < 0) {
                    perror("recvfrom error in transfer loop");
                    break;
                }
                
                // Basic packet validation
                if (n < 4) {
                    fprintf(stderr, "Packet too short during transfer. Terminating.\n");
                    break;
                }
                
                uint16_t data_opcode = ntohs(*(uint16_t *)buffer);
                uint16_t received_block = ntohs(*(uint16_t *)(buffer + 2));
                int data_len = n - 4;

                if (data_opcode == OP_DATA && received_block == expected_block) {
                    // 2. Received expected DATA block
                    
                    // Write data to file
                    if (fwrite(buffer + 4, 1, data_len, fp) != data_len) {
                        perror("File write error");
                        // Send Error 3: Disk full or allocation exceeded
                        int err_len = create_error_packet(buffer, 3, "Disk write error.");
                        sendto(transfer_sockfd, buffer, err_len, 0, (struct sockaddr *)&cli_addr, addr_len);
                        break; 
                    }
                    total_bytes += data_len;
                    
                    // 3. Send ACK for received block
                    ack_len = create_ack_packet(buffer, expected_block);
                    sendto(transfer_sockfd, buffer, ack_len, 0, (struct sockaddr *)&cli_addr, addr_len);
                    
                    printf("Received Block %u, Bytes: %d. Total: %d\n", expected_block, data_len, total_bytes);

                    // Check for last packet (data size < 512)
                    if (data_len < TFTP_DATA_SIZE) {
                        done = 1; // Transfer complete
                    } else {
                        expected_block++; // Expect the next block
                    }

                } else if (data_opcode == OP_DATA && received_block < expected_block) {
                    // Duplicate DATA packet (Client didn't get our ACK)
                    // Re-send ACK for the received block
                    printf("Duplicate DATA %u received. Re-sending ACK %u.\n", received_block, received_block);
                    ack_len = create_ack_packet(buffer, received_block);
                    sendto(transfer_sockfd, buffer, ack_len, 0, (struct sockaddr *)&cli_addr, addr_len);
                    
                } else if (data_opcode == OP_ERROR) {
                    fprintf(stderr, "Client sent ERROR during transfer. Terminating.\n");
                    break;
                } else {
                    fprintf(stderr, "Unexpected packet received (Opcode: %u, Block: %u). Terminating.\n", data_opcode, received_block);
                    // Send Error 4: Illegal TFTP operation
                    int err_len = create_error_packet(buffer, 4, "Illegal TFTP operation.");
                    sendto(transfer_sockfd, buffer, err_len, 0, (struct sockaddr *)&cli_addr, addr_len);
                    break;
                }
            } // end while(!done)
            
            printf("File transfer of %s finished. Total bytes: %d\n", filename, total_bytes);
            fclose(fp);
            close(transfer_sockfd);
            
        } else if (opcode == OP_RRQ) {
            fprintf(stderr, "RRQ received. Only WRQ is supported in this example. Ignoring.\n");
        } else {
            fprintf(stderr, "Unknown opcode %u received. Ignoring.\n", opcode);
        }
    }
}

int main(void)
 {
    // You should ensure the directory where this server runs has write permissions.
    start_tftp_server(LISTEN_PORT);
    return 0;
}