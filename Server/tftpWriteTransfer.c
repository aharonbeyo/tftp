#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/select.h> // For select() and timeouts

// --- DEFINITIONS (Assuming these are defined globally or included from the main file) ---
#define OP_WRQ  2
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERROR 5
#define BLOCK_SIZE 512
#define PACKET_BUF_SIZE (4 + BLOCK_SIZE)
#define TIMEOUT_SEC 3  // Timeout in seconds
#define MAX_RETRIES 5  // Maximum retransmissions

void tftp_write_transfer(int sockfd, const struct sockaddr_in *cliaddr, 
                         socklen_t len, const char *filename);
// --- EXTERNAL HELPER FUNCTION PROTOTYPES ---
void send_error(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, int code, const char *message);

// Helper to send an ACK packet
void send_ack(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, uint16_t block) {
    char ack_packet[4];
    uint16_t *p = (uint16_t *)ack_packet;

    // Opcode: ACK (4)
    *p++ = htons(OP_ACK);
    // Block Number
    *p = htons(block);
    
    if (sendto(sockfd, ack_packet, 4, 0, (const struct sockaddr *)cliaddr, len) < 0) {
        perror("Failed to send ACK packet");
    }
}

// --- CORE WRITE TRANSFER FUNCTION ---
void tftp_write_transfer(int sockfd, const struct sockaddr_in *cliaddr, 
                         socklen_t len, const char *filename) {
    
    int fd;
    char buffer[PACKET_BUF_SIZE];
    uint16_t expected_block = 1;
    int retries = 0;
    
    // 1. Open or create the file for writing
    // Use a reasonable mode (e.g., 0644) for creation
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        if (errno == EACCES) {
            send_error(sockfd, cliaddr, len, 2, "Access violation (cannot create file)");
        } else {
            send_error(sockfd, cliaddr, len, 0, "Not defined error on file creation");
        }
        return;
    }

    // 2. Initial Acknowledgment: Send ACK block 0
    // This confirms the server is ready and prompts the client to send DATA block 1.
    send_ack(sockfd, cliaddr, len, 0);
    printf("[Child PID %d] Sent initial ACK 0 to client.\n", getpid());

    // --- MAIN WRITE LOOP ---
    while (1) {
        ssize_t n;
        struct timeval tv;
        fd_set readfds;
        int rv;

        // --- Timeout Setup (Required for reliability) ---
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;

        // Use select() to wait for data with a timeout
        rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (rv == -1) {
            // Error occurred in select
            perror("select error");
            send_error(sockfd, cliaddr, len, 0, "Server select error");
            break;
        } else if (rv == 0) {
            // Timeout occurred
            if (retries < MAX_RETRIES) {
                printf("[Child PID %d] Timeout. Retrying ACK %d...\n", getpid(), expected_block - 1);
                // Resend the last successful ACK
                send_ack(sockfd, cliaddr, len, expected_block - 1);
                retries++;
                continue; // Skip recvfrom and loop again
            } else {
                // Max retries reached
                printf("[Child PID %d] Max retries reached. Aborting transfer.\n", getpid());
                send_error(sockfd, cliaddr, len, 0, "Max retries reached, transfer aborted");
                break;
            }
        }
        
        // --- Receive DATA packet ---
        n = recvfrom(sockfd, buffer, PACKET_BUF_SIZE, 0, 
                     (struct sockaddr *)cliaddr, &len);

        if (n < 4) { // Minimum packet size is 4 bytes (Opcode + Block#)
            fprintf(stderr, "[Child PID %d] Received short packet: %ld bytes\n", getpid(), n);
            send_error(sockfd, cliaddr, len, 4, "Illegal TFTP operation (short packet)");
            break;
        }

        uint16_t opcode = ntohs(*(uint16_t *)buffer);
        uint16_t block_num = ntohs(*(uint16_t *)(buffer + 2));
        
        // --- DATA/ACK Protocol Logic ---
        if (opcode == OP_DATA) {
            if (block_num == expected_block) {
                // 3. Correct Block Received: Write data to file
                ssize_t data_len = n - 4;
                if (write(fd, buffer + 4, data_len) < 0) {
                    perror("File write failed");
                    send_error(sockfd, cliaddr, len, 3, "Disk full or I/O error");
                    break;
                }
                
                // 4. Acknowledge the received block
                send_ack(sockfd, cliaddr, len, block_num);
                printf("[Child PID %d] Received DATA %d (%zd bytes). Sent ACK %d.\n", 
                       getpid(), block_num, data_len, block_num);

                // 5. Check for termination (data length < 512 bytes)
                if (data_len < BLOCK_SIZE) {
                    printf("[Child PID %d] Last block received. Transfer finished.\n", getpid());
                    break;
                }

                // Prepare for the next block
                expected_block++;
                retries = 0; // Reset retries on successful receipt
            } else if (block_num < expected_block) {
                // Duplicate DATA received (Client didn't get our last ACK)
                // Resend the last successful ACK to re-synchronize
                printf("[Child PID %d] Received duplicate DATA %d. Resending ACK %d.\n", 
                       getpid(), block_num, block_num);
                send_ack(sockfd, cliaddr, len, block_num);
                retries = 0; // Treat as a successful communication
            } else {
                // Block number is too high (Protocol error)
                send_error(sockfd, cliaddr, len, 4, "Illegal TFTP operation (unexpected block)");
                break;
            }
        } else if (opcode == OP_ERROR) {
            // Client sent an ERROR packet, aborting transfer
            printf("[Child PID %d] Client reported error. Aborting.\n", getpid());
            break;
        } else {
            // Received something other than DATA or ERROR
            send_error(sockfd, cliaddr, len, 4, "Illegal TFTP operation (unexpected opcode)");
            break;
        }
    }

    // --- CLEANUP ---
    close(fd);
}