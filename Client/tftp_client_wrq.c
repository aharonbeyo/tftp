#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>

#define SERVER_PORT 69
#define MAX_BUFFER_SIZE 516     // 2 (Opcode) + 2 (Block #) + 512 (Data)
#define TFTP_DATA_SIZE 512      // Max data size per packet
#define MAX_RETRANSMIT 5        // Max retransmissions before giving up
#define TIMEOUT_SEC 3           // Timeout for socket receive (seconds)

// TFTP Opcodes (Network Byte Order)
#define OP_RRQ      1
#define OP_WRQ      2
#define OP_DATA     3
#define OP_ACK      4
#define OP_ERROR    5

// Transfer Mode
#define MODE "octet"

// --- Packet Construction/Parsing Helpers ---

// Builds the initial WRQ packet
int create_wrq_packet(char *buffer, const char *filename) {
    // Opcode (2 bytes) - WRQ = 2
    *(uint16_t *)buffer = htons(OP_WRQ);
    int offset = 2;

    // Filename
    size_t filename_len = strlen(filename);
    memcpy(buffer + offset, filename, filename_len);
    offset += filename_len;

    // Filename null-terminator
    buffer[offset++] = 0;

    // Mode
    size_t mode_len = strlen(MODE);
    memcpy(buffer + offset, MODE, mode_len);
    offset += mode_len;

    // Mode null-terminator
    buffer[offset++] = 0;

    return offset; // Return total packet size
}

// Builds a DATA packet
int create_data_packet(char *buffer, uint16_t block_num, const char *data, int data_len) {
    // Opcode (2 bytes) - DATA = 3
    *(uint16_t *)buffer = htons(OP_DATA); 
    // Block Number (2 bytes)
    *(uint16_t *)(buffer + 2) = htons(block_num); 
    
    // Data payload
    memcpy(buffer + 4, data, data_len);
    
    return 4 + data_len; // Total packet size
}

// Configures the socket to timeout on recvfrom
void set_socket_timeout(int sockfd, int seconds) 
{
   struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) 
    {
        perror("Error setting socket timeout");
    }
}

// --- Main Client Logic ---

void tftp_write_file(const char *server_ip, const char *local_filename, const char *remote_filename) {
    int sockfd;
    struct sockaddr_in serv_addr;
    socklen_t addr_len = sizeof(serv_addr);
    char send_buffer[MAX_BUFFER_SIZE];
    char recv_buffer[MAX_BUFFER_SIZE];
    
    FILE *fp;
    char file_data[TFTP_DATA_SIZE];
    int bytes_read;
    uint16_t current_block = 1;
    int total_bytes = 0;
    
    // 1. Open local file for reading
    fp = fopen(local_filename, "rb");
    if (!fp) {
        perror("Failed to open local file");
        return;
    }
    
    // 2. Socket setup
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        fclose(fp);
        return;
    }
    
    // Set a basic timeout for retransmission logic
    set_socket_timeout(sockfd, TIMEOUT_SEC);
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT); // Initial request goes to port 69
    
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported\n");
        close(sockfd);
        fclose(fp);
        return;
    }

    // --- A. Send WRQ Request ---
    
    int wrq_len = create_wrq_packet(send_buffer, remote_filename);
    printf("Sending WRQ for file '%s' to server...\n", remote_filename);

    int retries = 0;
    int n;

    // WRQ Transmission and ACK 0 Loop
    do {
        if (sendto(sockfd, send_buffer, wrq_len, 0, (const struct sockaddr *)&serv_addr, addr_len) < 0) {
            perror("Error sending WRQ");
            goto cleanup;
        }
        
        // Wait for ACK 0
        n = recvfrom(sockfd, recv_buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr *)&serv_addr, &addr_len);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout occurred
                retries++;
                printf("Timeout on WRQ. Retrying (%d/%d)...\n", retries, MAX_RETRANSMIT);
            } else {
                perror("recvfrom error");
                goto cleanup;
            }
        } else if (n >= 4) {
            uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
            uint16_t block = ntohs(*(uint16_t *)(recv_buffer + 2));

            if (opcode == OP_ACK && block == 0) {
                printf("Received initial ACK 0. Starting transfer.\n");
                // The server's address in serv_addr is now its TID (new port)
                break; // Break the WRQ loop, transfer begins
            } else if (opcode == OP_ERROR) {
                printf("Server Error (%u): %s\n", block, recv_buffer + 4);
                goto cleanup;
            } else {
                fprintf(stderr, "Unexpected packet received (Opcode: %u).\n", opcode);
                goto cleanup;
            }
        }
    } while (retries < MAX_RETRANSMIT);
    
    if (retries == MAX_RETRANSMIT) {
        fprintf(stderr, "Failed to get ACK 0 after %d retries. Aborting.\n", MAX_RETRANSMIT);
        goto cleanup;
    }

    // --- B. Data Transfer Loop (Lock-Step) ---

    int done = 0;
    while (!done) {
        // 1. Read data from file
        bytes_read = fread(file_data, 1, TFTP_DATA_SIZE, fp);
        if (ferror(fp)) {
            perror("File read error");
            goto cleanup;
        }

        // 2. Construct DATA packet
        int data_len = create_data_packet(send_buffer, current_block, file_data, bytes_read);

        // 3. Send and wait for ACK loop
        retries = 0;
        int ack_received = 0;
        
        do {
            // Send DATA packet
            if (sendto(sockfd, send_buffer, data_len, 0, (const struct sockaddr *)&serv_addr, addr_len) < 0) {
                perror("Error sending DATA packet");
                goto cleanup;
            }
            
            // Wait for ACK
            n = recvfrom(sockfd, recv_buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr *)&serv_addr, &addr_len);
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout occurred, retransmit
                    retries++;
                    printf("Timeout on Block %u. Retrying (%d/%d)...\n", current_block, retries, MAX_RETRANSMIT);
                } else {
                    perror("recvfrom error during transfer");
                    goto cleanup;
                }
            } else if (n >= 4) {
                uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
                uint16_t block = ntohs(*(uint16_t *)(recv_buffer + 2));

                if (opcode == OP_ACK && block == current_block) {
                    // Correct ACK received, move to next block
                    printf("Received ACK %u. Bytes sent: %d. Total: %d\n", current_block, bytes_read, total_bytes + bytes_read);
                    total_bytes += bytes_read;
                    ack_received = 1;
                    break;
                } else if (opcode == OP_ACK && block < current_block) {
                    // Duplicate ACK (shouldn't happen on client side, but ignore if it does)
                    continue; 
                } else if (opcode == OP_ERROR) {
                    printf("Server Error (%u) on Block %u: %s\n", block, current_block, recv_buffer + 4);
                    goto cleanup;
                } else {
                    fprintf(stderr, "Unexpected packet during transfer (Opcode: %u, Block: %u). Terminating.\n", opcode, block);
                    goto cleanup;
                }
            }

        } while (retries < MAX_RETRANSMIT);

        if (retries == MAX_RETRANSMIT) {
            fprintf(stderr, "Failed to get ACK %u after %d retries. Aborting.\n", current_block, MAX_RETRANSMIT);
            goto cleanup;
        }
        
        // Check if this was the last (short) packet
        if (bytes_read < TFTP_DATA_SIZE) {
            done = 1; 
        } else {
            current_block++;
        }
    }

    printf("\nFile transfer of '%s' complete. Total bytes sent: %d\n", local_filename, total_bytes);

cleanup:
    if (fp) fclose(fp);
    if (sockfd) close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <local_file_to_send> <remote_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    tftp_write_file(argv[1], argv[2], argv[3]);
    
    return EXIT_SUCCESS;
}