#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

// --- TFTP Constants (Re-defined for completeness) ---
#define SERVER_PORT 69
#define OP_RRQ  1
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERROR 5
#define MODE "octet"
#define BLOCK_SIZE 512
#define PACKET_BUF_SIZE (4 + BLOCK_SIZE)
#define TIMEOUT_SEC 3
#define MAX_RETRIES 5

// Helper to send an ACK packet
void send_ack(int sockfd, const struct sockaddr_in *target_addr, socklen_t len, uint16_t block) {
    char ack_packet[4];
    uint16_t *p = (uint16_t *)ack_packet;

    // Opcode: ACK (4)
    *p++ = htons(OP_ACK);
    // Block Number
    *p = htons(block);
    
    if (sendto(sockfd, ack_packet, 4, 0, (const struct sockaddr *)target_addr, len) < 0) {
        perror("Failed to send ACK packet");
    }
}

int SetupSocket(const char *server_ip, struct sockaddr_in *servaddr)
{
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed"); return 1;
    }

    memset(servaddr, 0, sizeof(struct sockaddr_in));
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &servaddr->sin_addr) <= 0) {
        perror("Invalid server IP address"); close(sockfd); return 1;
    }
    return sockfd;
}

int ConstructAndSendRRQ(int sockfd, const struct sockaddr_in *servaddr, const char *filename)
{
 // Construct and Send RRQ 
    char rrq_packet[PACKET_BUF_SIZE];
    uint16_t *p_op = (uint16_t *)rrq_packet;
    *p_op = htons(OP_RRQ); 
    char *p_data = rrq_packet + 2;
    size_t len_filename = strlen(filename) + 1;
    size_t len_mode = strlen(MODE) + 1;
    memcpy(p_data, filename, len_filename);
    p_data += len_filename;
    memcpy(p_data, MODE, len_mode);
    p_data += len_mode;
    size_t rrq_len = p_data - rrq_packet;

    if (sendto(sockfd, rrq_packet, rrq_len, 0, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Failed to send RRQ"); close(sockfd); 
        return -1;
    }
    printf("Sent RRQ for file '%s'. Waiting for DATA 1...\n", filename);

    return 0;
}

// --- MAIN FUNCTION (Complete Implementation) ---
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <filename>\n", argv[0]);
        return 1;
    }

    int sockfd, fd;
    struct sockaddr_in servaddr, remote_transfer_addr;
    socklen_t remote_len = sizeof(remote_transfer_addr);
    
    char recv_buffer[PACKET_BUF_SIZE];
    char *server_ip = argv[1];
    char *remote_filename = argv[2];
    char local_filename[256]; // Use the same name for local save

    // Create local filename (e.g., just the provided name)
    strncpy(local_filename, remote_filename, 255); 
    sockfd =SetupSocket(server_ip, &servaddr);
  
    if (sockfd < 0) {
        return -1;
    }   

    if (ConstructAndSendRRQ(sockfd, &servaddr, remote_filename) < 0) 
    {
        close(sockfd);
        return -2;
    }   
   

    // --- MAIN TRANSFER LOGIC ---
    uint16_t expected_block = 1;
    int retries = 0;
    int file_transfer_complete = 0;
    
    // 3. Open local file for writing (O_CREAT | O_TRUNC will create/overwrite)
    fd = open(local_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        perror("Failed to open local file for writing"); close(sockfd); return 1;
    }

    while (!file_transfer_complete) {
        ssize_t n;
        struct timeval tv;
        fd_set readfds;
        int rv;

        // --- Timeout Setup ---
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;

        // Use select() to wait for data with a timeout
        rv = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (rv == -1) {
            perror("select error"); break;
        } else if (rv == 0) {
            // Timeout occurred: Retransmit last ACK
            if (retries < MAX_RETRIES) {
                printf("Timeout. Retrying ACK %d...\n", expected_block - 1);
                // The ACK needs to go to the server's transfer port, not port 69
                send_ack(sockfd, &remote_transfer_addr, remote_len, expected_block - 1);
                retries++;
                continue; 
            } else {
                printf("Max retries reached. Aborting download.\n");
                break;
            }
        }
        
        // --- Receive Packet ---
        // Note: For the FIRST packet, the source port will be new. 
        n = recvfrom(sockfd, recv_buffer, PACKET_BUF_SIZE, 0, 
                     (struct sockaddr *)&remote_transfer_addr, &remote_len);

        if (n < 4) {
            fprintf(stderr, "Received short packet.\n");
            retries = 0; // Communication happened, reset retries
            continue; 
        }

        uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
        uint16_t block_num = ntohs(*(uint16_t *)(recv_buffer + 2));
        
        // --- Packet Processing Logic ---
        if (opcode == OP_DATA) {
            // A. INITIAL SETUP: Capture Server's Ephemeral Port (Happens on first DATA only)
            if (expected_block == 1) {
                printf("Received first packet from server transfer port %d.\n", 
                       ntohs(remote_transfer_addr.sin_port));
            }

            // B. Data received is the expected block
            if (block_num == expected_block) {
                ssize_t data_len = n - 4;
                
                // Write data to file
                if (write(fd, recv_buffer + 4, data_len) < 0) {
                    perror("File write failed"); break;
                }
                
                // Send acknowledgment (ACK)
                send_ack(sockfd, &remote_transfer_addr, remote_len, block_num);
                printf("Received DATA %d (%zd bytes). Sent ACK %d.\n", block_num, data_len, block_num);

                // Check for termination
                if (data_len < BLOCK_SIZE) {
                    file_transfer_complete = 1;
                }

                expected_block++;
                retries = 0; // Reset retries on successful receipt
            } else if (block_num < expected_block) {
                // Duplicate DATA received: Resend last successful ACK
                printf("Received duplicate DATA %d. Resending ACK %d.\n", block_num, block_num);
                send_ack(sockfd, &remote_transfer_addr, remote_len, block_num);
                retries = 0;
            } else {
                // Block number is too high (Protocol error)
                fprintf(stderr, "Received unexpected block %d. Expected %d.\n", block_num, expected_block);
                break;
            }
        } else if (opcode == OP_ERROR) {
            // Server reported error
            char *error_msg = recv_buffer + 4;
            fprintf(stderr, "Server Error %d: %s\n", block_num, error_msg);
            break;
        } else {
            // Unexpected opcode
            fprintf(stderr, "Received unexpected opcode: %d\n", opcode);
            break;
        }
    }

    // --- CLEANUP ---
    close(fd);
    close(sockfd);

    if (file_transfer_complete) {
        printf("File '%s' successfully downloaded.\n", local_filename);
        return 0;
    } else {
        // If loop broke without completion, delete the partial file
        unlink(local_filename); 
        fprintf(stderr, "Download failed or aborted. Partial file deleted.\n");
        return 1;
    }
}