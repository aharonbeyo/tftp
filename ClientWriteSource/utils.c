#include "utils.h"
#include <sys/stat.h>

long int getFileSizeStat(const char* filename) 
{
    struct stat st;

    // stat() returns 0 on success, -1 on failure
    if (stat(filename, &st) == 0) {
        return (long int)st.st_size; // st_size holds the file size in bytes
    } else {
        // Handle error (e.g., file not found, permission issues)
        perror("Error getting file size");
        return -1;
    }
}

void setSocketTimeout(int sockfd, int seconds) 
{
   struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) 
    {
        perror("Error setting socket timeout");
    }
}

int SetUpSocket(const char *server_ip, struct sockaddr_in *serv_addr) 
{
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Set a basic timeout for retransmission logic
    setSocketTimeout(sockfd, TIMEOUT_SEC);
     memset(serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr->sin_family = AF_INET;
    serv_addr->sin_port = htons(SERVER_PORT); // Initial request goes to port 69
    
    if (inet_pton(AF_INET, server_ip, &serv_addr->sin_addr) <= 0) 
    {
        fprintf(stderr, "Invalid address/ Address not supported\n");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int createWrqPacket(char *buffer, const char *filename) {
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
int createDataPacket(char *buffer, uint16_t block_num, const char *data, int data_len) {
    // Opcode (2 bytes) - DATA = 3
    *(uint16_t *)buffer = htons(OP_DATA); 
    // Block Number (2 bytes)
    *(uint16_t *)(buffer + 2) = htons(block_num); 
    
    // Data payload
    memcpy(buffer + 4, data, data_len);
    
    return 4 + data_len; // Total packet size
}
