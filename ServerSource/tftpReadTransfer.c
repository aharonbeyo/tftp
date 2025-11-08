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
#define OP_RRQ  1
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERROR 5
#define BLOCK_SIZE 512
#define PACKET_BUF_SIZE (4 + BLOCK_SIZE)
#define TIMEOUT_SEC 3  // Timeout in seconds
#define MAX_RETRIES 5  // Maximum retransmissions

// --- EXTERNAL HELPER FUNCTION PROTOTYPES ---
// void send_error(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, int code, const char *message);
void tftpReadTransfer(int sockfd, const struct sockaddr_in *cliaddr, 
                        socklen_t len, const char *filename);
void send_error(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, 
                int code, const char *message);
                
// Helper to construct and send a DATA packet
ssize_t send_data(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, 
                  uint16_t block, const char *data, ssize_t data_len, char *packet_buffer) {
    
    // Construct the packet in the provided buffer
    uint16_t *p = (uint16_t *)packet_buffer;

    // Opcode: DATA (3)
    *p++ = htons(OP_DATA);
    // Block Number
    *p++ = htons(block);
    
    // Copy data payload
    memcpy(packet_buffer + 4, data, data_len);
    ssize_t total_size = 4 + data_len;
    
    // Send the packet
    return sendto(sockfd, packet_buffer, total_size, 0, 
                  (const struct sockaddr *)cliaddr, len);
}


// --- CORE READ TRANSFER FUNCTION ---
void tftpReadTransfer(int sockfd, const struct sockaddr_in *cliaddr, 
                        socklen_t len, const char *filename) {
    
    int fd;
    // Two buffers: one for reading file data, one for holding the last sent packet
    char file_buffer[BLOCK_SIZE];
    char last_data_packet[PACKET_BUF_SIZE];
    char recv_buffer[PACKET_BUF_SIZE]; 
    
    uint16_t current_block = 1;
    int retries = 0;
    ssize_t last_data_size = 0; // Size of the last data payload sent
    ssize_t last_packet_size = 0; // Total size of the last packet sent
    
    // 1. Open the file for reading
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            send_error(sockfd, cliaddr, len, 1, "File not found");
        } else if (errno == EACCES) {
            send_error(sockfd, cliaddr, len, 2, "Access violation (cannot read file)");
        } else {
            send_error(sockfd, cliaddr, len, 0, "Not defined error on file open");
        }
        return;
    }

    printf("[Child PID %d] Starting RRQ transfer for file: %s\n", getpid(), filename);

    // --- MAIN READ LOOP ---
    while (1) {
        ssize_t n;
        struct timeval tv;
        fd_set readfds;
        int rv;
        
        // --- State: Send Data Block or Retransmit ---
        if (retries == 0) { 
            // New Block: Read file data and send it (only on the first attempt)
            ssize_t bytes_read = read(fd, file_buffer, BLOCK_SIZE);
            if (bytes_read < 0) {
                perror("File read failed");
                send_error(sockfd, cliaddr, len, 3, "I/O error during read");
                break;
            }
            
            // Construct and send the DATA packet. Store packet details for retransmission.
            last_packet_size = send_data(sockfd, cliaddr, len, current_block, 
                                         file_buffer, bytes_read, last_data_packet);
            
            if (last_packet_size < 0) {
                perror("Failed to send DATA packet");
                break;
            }
            last_data_size = bytes_read; // Keep track of the payload size

            printf("[Child PID %d] Sent DATA %d (%zd bytes).\n", getpid(), current_block, bytes_read);

            // Termination Check 1: If it was the last block, send it, then wait for final ACK.
            if (bytes_read < BLOCK_SIZE) {
                printf("[Child PID %d] Sent last block. Waiting for final ACK...\n", getpid());
            }
        } else {
            // Retransmit: Resend the last packet stored in last_data_packet
            if (sendto(sockfd, last_data_packet, last_packet_size, 0, 
                       (const struct sockaddr *)cliaddr, len) < 0) {
                perror("Failed to retransmit DATA packet");
                break;
            }
            printf("[Child PID %d] Retransmitting DATA %d. Attempt %d/%d.\n", 
                   getpid(), current_block, retries + 1, MAX_RETRIES);
        }

        // --- Timeout Setup (Wait for ACK) ---
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;

        // Use select() to wait for data with a timeout
        rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (rv == -1) {
            perror("select error");
            send_error(sockfd, cliaddr, len, 0, "Server select error");
            break;
        } else if (rv == 0) {
            // Timeout occurred
            if (retries < MAX_RETRIES) {
                retries++;
                continue; // Loop again to retransmit
            } else {
                printf("[Child PID %d] Max retries reached. Aborting transfer.\n", getpid());
                send_error(sockfd, cliaddr, len, 0, "Max retries reached, transfer aborted");
                break;
            }
        }
        
        // --- Receive ACK packet ---
        n = recvfrom(sockfd, recv_buffer, PACKET_BUF_SIZE, 0, 
                     (struct sockaddr *)cliaddr, &len);

        if (n < 4) {
            fprintf(stderr, "[Child PID %d] Received short ACK/Error packet.\n", getpid());
            // Treat as bad packet, let timeout handle retransmission
            continue; 
        }

        uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
        uint16_t block_num = ntohs(*(uint16_t *)(recv_buffer + 2));
        
        // --- ACK Protocol Logic ---
        if (opcode == OP_ACK) {
            if (block_num == current_block) {
                // 3. Expected ACK Received: Prepare for next block
                printf("[Child PID %d] Received ACK %d.\n", getpid(), block_num);
                
                // Termination Check 2: If the block we just ACKed was the last block (data < 512)
                if (last_data_size < BLOCK_SIZE) {
                    printf("[Child PID %d] Final ACK received. Transfer finished.\n", getpid());
                    break;
                }
                
                // Advance to the next block
                current_block++;
                retries = 0; // Reset retries on successful ACK
                // Loop will now read the next block from the file
            } else if (block_num < current_block) {
                // Received an old ACK (Client might have received duplicate DATA)
                printf("[Child PID %d] Received old ACK %d. Ignoring.\n", getpid(), block_num);
                retries = 0; // Reset retries, as communication is fine
            } else {
                // ACK number is too high (Protocol error)
                send_error(sockfd, cliaddr, len, 4, "Illegal TFTP operation (unexpected ACK)");
                break;
            }
        } else if (opcode == OP_ERROR) {
            // Client sent an ERROR packet, aborting transfer
            printf("[Child PID %d] Client reported error. Aborting.\n", getpid());
            break;
        } else {
            // Received something other than ACK or ERROR
            send_error(sockfd, cliaddr, len, 4, "Illegal TFTP operation (unexpected opcode)");
            break;
        }
    }

    // --- CLEANUP ---
    close(fd);
}