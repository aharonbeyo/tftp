
#include "utils.h"


int packetProcessingLogic(int sockfd, uint16_t *expected_block, int *retries, int *file_transfer_complete, char *recv_buffer, ssize_t numberBytesReceived, struct sockaddr_in *remote_transfer_addr, int remote_len,int fd) 
{
        uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
        uint16_t block_num = ntohs(*(uint16_t *)(recv_buffer + 2));
        
        // --- Packet Processing Logic ---
        if (opcode == OP_DATA) 
        {
            // A. INITIAL SETUP: Capture Server's Ephemeral Port (Happens on first DATA only)
            if ((*expected_block) == 1)
            {
                printf("Received first packet from server transfer port %d.\n", 
                       ntohs(remote_transfer_addr->sin_port));
            }

            // B. Data received is the expected block
            if (block_num == *expected_block) {
                ssize_t data_len = numberBytesReceived - 4;
                
                // Write data to file
                if (write(fd, recv_buffer + 4, data_len) < 0) {
                    perror("File write failed"); 
                    return -1;
                }
                
                // Send acknowledgment (ACK)
                send_ack(sockfd, remote_transfer_addr, remote_len, block_num);
                printf("Received DATA %d (%zd bytes). Sent ACK %d.\n", block_num, data_len, block_num);

                // Check for termination
                if (data_len < BLOCK_SIZE) {
                    (*file_transfer_complete) = 1;
                }

                (*expected_block)++;
                (*retries) = 0; // Reset retries on successful receipt
            } 
            else if (block_num < (*expected_block))
            {
                // Duplicate DATA received: Resend last successful ACK
                printf("Received duplicate DATA %d. Resending ACK %d.\n", block_num, block_num);
                send_ack(sockfd, remote_transfer_addr, remote_len, block_num);
                (*retries) = 0;
            } 
            else 
            {
                // Block number is too high (Protocol error)
                fprintf(stderr, "Received unexpected block %d. Expected %d.\n", block_num, (*expected_block));
                return -1;
            }
        } 
        else if (opcode == OP_ERROR) 
        {
            // Server reported error
            char *error_msg = recv_buffer + 4;
            fprintf(stderr, "Server Error %d: %s\n", block_num, error_msg);
            return -1;
        } 
        else 
        {
            // Unexpected opcode
            fprintf(stderr, "Received unexpected opcode: %d\n", opcode);
            return -1;
        }
    
    return 0;
}

int mainTransferLogic(int sockfd, const char *local_filename) 
{
    uint16_t expected_block = 1;
    int retries = 0;
    int file_transfer_complete = 0;
    struct sockaddr_in remote_transfer_addr;
    socklen_t remote_len = sizeof(remote_transfer_addr);
    char recv_buffer[PACKET_BUF_SIZE];
    memset(&remote_transfer_addr, 0, sizeof(remote_transfer_addr));
    // Open local file for writing (O_CREAT | O_TRUNC will create/overwrite)
    int fd = open(local_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    
    if (fd < 0) 
    {
        perror("Failed to open local file for writing"); 
        close(sockfd); 
        return -1;
    }

    while (!file_transfer_complete) 
    {
        ssize_t numberBytesReceived;
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
            perror("select error"); 
            break;
        } 
        else if (rv == 0) 
        {
            if (expected_block > 1 && retries < MAX_RETRIES) 
            {
                // Only re-send ACK if we have received at least one DATA block (expected_block > 1)
                send_ack(sockfd, &remote_transfer_addr, remote_len, expected_block - 1);
                retries++;
                continue;
            } 
            else if (expected_block == 1 && retries < MAX_RETRIES) 
            {
                // We are waiting for DATA 1. We just wait for the server to retransmit DATA 1
                // (since it was the server's RRQ that timed out). Do not send an ACK.
                retries++;
                continue;
            } 
            else 
            {
                // Max retries reached or other failure
                printf("Max retries reached. Aborting download.\n");
                break;
            }
        }
        
        // --- Receive Packet ---
        // Note: For the FIRST packet, the source port will be new. 
        numberBytesReceived = recvfrom(sockfd, recv_buffer, PACKET_BUF_SIZE, 0, 
                     (struct sockaddr *)&remote_transfer_addr, &remote_len);

        if (numberBytesReceived < 4) {
            fprintf(stderr, "Received short packet.\n");
            retries = 0; // Communication happened, reset retries
            continue; 
        }

        int ans = packetProcessingLogic(sockfd, &expected_block, &retries, &file_transfer_complete, recv_buffer, numberBytesReceived, &remote_transfer_addr, remote_len, fd);

        if (ans < 0) 
        {
            break;
        }
          
    }
 // --- CLEANUP ---
    close(fd);
   

    if (file_transfer_complete) 
    {
        printf("File '%s' successfully downloaded.\n", local_filename);
        return 0;
    } else {
        // If loop broke without completion, delete the partial file
        unlink(local_filename); 
        fprintf(stderr, "Download failed or aborted. Partial file deleted.\n");
        return -1;
    }

}
