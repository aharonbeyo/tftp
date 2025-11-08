#include "utils.h"

void tftpWriteFile(const char *server_ip, const char *local_filename, const char *remote_filename);

int InitializeTransfer(int sockfd, struct sockaddr_in *serv_addr, socklen_t addr_len,  int wrq_len, char *send_buffer, char *recv_buffer)
{
     //  printf("Sending WRQ for file '%s' to server...\n", remote_filename);

    int retries = 0;
    int n;
    // WRQ Transmission and ACK 0 Loop
    do {
        if (sendto(sockfd, send_buffer, wrq_len, 0, (const struct sockaddr *)serv_addr, addr_len) < 0) {
            perror("Error sending WRQ");
            return -1;
        }
        
        // Wait for ACK 0
        n = recvfrom(sockfd, recv_buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr *)serv_addr, &addr_len);
        
        if (n < 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                // Timeout occurred
                retries++;
                printf("Timeout on WRQ. Retrying (%d/%d)...\n", retries, MAX_RETRANSMIT);
            } 
            else
             {
                perror("recvfrom error");
                return -1;
            }
        } 
        else if (n >= 4) 
        {
            uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
            uint16_t block = ntohs(*(uint16_t *)(recv_buffer + 2));

            if (opcode == OP_ACK && block == 0) 
            {
                printf("Received initial ACK 0. Starting transfer.\n");
                // The server's address in serv_addr is now its TID (new port)
                break; // Break the WRQ loop, transfer begins
            } 
            else if (opcode == OP_ERROR) 
            {
                printf("Server Error (%u): %s\n", block, recv_buffer + 4);
                 return -1;
            } else 
            {
                fprintf(stderr, "Unexpected packet received (Opcode: %u).\n", opcode);
                 return -1;
            }
        }
    } while (retries < MAX_RETRANSMIT);
    
    if (retries == MAX_RETRANSMIT) {
        fprintf(stderr, "Failed to get ACK 0 after %d retries. Aborting.\n", MAX_RETRANSMIT);
        return -1;
    }

 
   return 0;
}
// --- Main Client Logic ---

int sendDataPack(int sockfd, struct sockaddr_in *serv_addr, socklen_t addr_len, char *send_buffer, char *recv_buffer, uint16_t current_block, int data_len, int bytes_read, int *total_bytes)
{
    int retries = 0;
        
    do 
    {
        // Send DATA packet
        if (sendto(sockfd, send_buffer, data_len, 0, (const struct sockaddr *)serv_addr, addr_len) < 0) 
        {
            perror("Error sending DATA packet");
            return -1;
        }
        
        // Wait for ACK
        int n = recvfrom(sockfd, recv_buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr *)serv_addr, &addr_len);
        
        if (n < 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                // Timeout occurred, retransmit
                retries++;
                printf("Timeout on Block %u. Retrying (%d/%d)...\n", current_block, retries, MAX_RETRANSMIT);
            } 
            else 
            {
                perror("recvfrom error during transfer");
                return -1;
            }
        } 
        else if (n >= 4) 
        {
            uint16_t opcode = ntohs(*(uint16_t *)recv_buffer);
            uint16_t block = ntohs(*(uint16_t *)(recv_buffer + 2));    

            if (opcode == OP_ACK && block == current_block)
            {
                // Correct ACK received, move to next block
                printf("Received ACK %u. Bytes sent: %d. Total: %d\n", current_block, bytes_read, *total_bytes + bytes_read);
                *total_bytes += bytes_read;
                break;
            } 
            else if (opcode == OP_ACK && block < current_block) 
            {
                // Duplicate ACK (shouldn't happen on client side, but ignore if it does)
               continue; 
            } 
            else if (opcode == OP_ERROR) 
            {
                printf("Server Error (%u) on Block %u: %s\n", block, current_block, recv_buffer + 4);
                return -1;
            } else 
            {
                fprintf(stderr, "Unexpected packet during transfer (Opcode: %u, Block: %u). Terminating.\n", opcode, block);
                return -1;
            }
        }

    } while (retries < MAX_RETRANSMIT);

    if (retries == MAX_RETRANSMIT) {
        fprintf(stderr, "Failed to get ACK %u after %d retries. Aborting.\n", current_block, MAX_RETRANSMIT);
        return -1;
    }

   return 0;
}



void tftpWriteFile(const char *server_ip, const char *local_filename, const char *remote_filename) 
{
    int sockfd;
    char send_buffer[MAX_BUFFER_SIZE];
    char recv_buffer[MAX_BUFFER_SIZE];
    
    FILE *fp;
    char file_data[TFTP_DATA_SIZE];
    int bytes_read;
    uint16_t current_block = 1;
    int total_bytes = 0;
    int succeeded = 0;
    // 1. Open local file for reading
    fp = fopen(local_filename, "rb");
    if (!fp) 
    {
        perror("Failed to open local file");
        return;
    }
    
    // 2. Socket setup
    struct sockaddr_in serv_addr;
    socklen_t addr_len = sizeof(serv_addr);

    sockfd = SetUpSocket(server_ip, &serv_addr);
   
    if (sockfd < 0) 
    {
        fclose(fp);
        return;
    }
 

    // --- A. Send WRQ Request ---
    int wrq_len = createWrqPacket(send_buffer, remote_filename);
    int init_result = InitializeTransfer(sockfd, &serv_addr, addr_len, wrq_len, send_buffer, recv_buffer);

    if (init_result < 0) 
    {
        fclose(fp);
        close(sockfd);
        return;
    }
    // // --- B. Data Transfer Loop (Lock-Step) ---
    printf("Sending WRQ for file '%s' to server...\n", remote_filename);
    long int file_size = getFileSizeStat(local_filename);

    while (1) 
    {
        // 1. Read data from file
        bytes_read = fread(file_data, 1, TFTP_DATA_SIZE, fp);
        if (ferror(fp)) 
        {
            perror("File read error");
            break;
        }

        // 2. Construct DATA packet
        int data_len = createDataPacket(send_buffer, current_block, file_data, bytes_read);
        int ans = sendDataPack(sockfd, &serv_addr, addr_len, send_buffer, recv_buffer, current_block, data_len, bytes_read, &total_bytes);

        if (ans < 0) 
            break;

        if (bytes_read < TFTP_DATA_SIZE || total_bytes >= file_size) 
        {
            succeeded = 1;
            break;
        }

        current_block++;
    }
   
    if (fp) 
        fclose(fp);
    if (sockfd) 
        close(sockfd);
    
    if (succeeded)
    {
        printf("\nFile transfer of '%s' complete. Total bytes sent: %d\n", local_filename, total_bytes);
    }
    else
    {
        printf("\nFile transfer of '%s' failed.\n", local_filename);
    }

 
}

