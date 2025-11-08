#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#define TFTP_PORT 69
#define OP_RRQ  1
#define OP_WRQ  2
#define OP_DATA 3
#define OP_ACK  4
#define OP_ERROR 5
#define BLOCK_SIZE 512
#define PACKET_BUF_SIZE (4 + BLOCK_SIZE) // Opcode(2) + Block#(2) + Data(512)

// --- FUNCTION PROTOTYPES ---
void send_error(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, 
                int code, const char *message);
void handle_tftp_request(int master_sockfd, const char *buffer, ssize_t n, 
                         const struct sockaddr_in *cliaddr, socklen_t len);

void tftpWriteTransfer(int sockfd, const struct sockaddr_in *cliaddr, 
                         socklen_t len, const char *filename);
void tftpReadTransfer(int sockfd, const struct sockaddr_in *cliaddr, 
                        socklen_t len, const char *filename);

// --- MAIN FUNCTION ---
int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);
    char buffer[PACKET_BUF_SIZE];
    
    // 1. Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        return 1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    // Server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(TFTP_PORT);
    
    // 2. Bind the socket to port 69
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    printf("TFTP Server listening on UDP port %d. Ready for multiple clients.\n", TFTP_PORT);

    while (1) {
        // 3. Wait for an initial client request (RRQ or WRQ)
        ssize_t n = recvfrom(sockfd, buffer, PACKET_BUF_SIZE, 0, 
                             (struct sockaddr *)&cliaddr, &len);
        
        if (n > 0) {
            // 4. Delegate the request handling to a new process
            handle_tftp_request(sockfd, buffer, n, &cliaddr, len);
        }

        // 5. Clean up finished child processes (Zombies) without blocking
        waitpid(-1, NULL, WNOHANG); 
    }
    
    // This part is unreachable, but good practice for cleanup
    close(sockfd);
    return 0;
}

// --- TFTP REQUEST HANDLER ---
void handle_tftp_request(int master_sockfd, const char *buffer, ssize_t n, 
                         const struct sockaddr_in *cliaddr, socklen_t len) {
    
    uint16_t opcode = ntohs(*(uint16_t *)buffer);
    const char *filename = buffer + 2;
    ssize_t filename_len = strlen(filename);
    
    if (filename_len + 2 >= n || (opcode != OP_RRQ && opcode != OP_WRQ)) {
        fprintf(stderr, "Malformed or invalid TFTP request received.\n");
        return;
    }

    // --- FORK: Create a new child process for this transfer ---
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        send_error(master_sockfd, cliaddr, len, 0, "Server error: could not fork");
        return;
    } 
    
    // Parent Process: returns to the main loop to listen on port 69
    if (pid > 0) {
        return;
    }

    // --- CHILD PROCESS starts here ---
    close(master_sockfd); // Child closes the master listener socket
    
    // 1. Create a NEW socket for the transfer
    int transfer_sockfd;
    if ((transfer_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("transfer socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // 2. Bind to any available ephemeral port
    struct sockaddr_in serv_transfer_addr;
    memset(&serv_transfer_addr, 0, sizeof(serv_transfer_addr));
    serv_transfer_addr.sin_family = AF_INET;
    serv_transfer_addr.sin_addr.s_addr = INADDR_ANY;
    serv_transfer_addr.sin_port = 0; // OS chooses port
    
    if (bind(transfer_sockfd, (const struct sockaddr *)&serv_transfer_addr, 
             sizeof(serv_transfer_addr)) < 0) {
        perror("transfer socket bind failed");
        close(transfer_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[Child PID %d] Starting transfer for '%s' from %s:%d...\n", 
           getpid(), filename, inet_ntoa(cliaddr->sin_addr), ntohs(cliaddr->sin_port));

    // 3. Delegate to the appropriate transfer logic
    if (opcode == OP_RRQ) {
        tftpReadTransfer(transfer_sockfd, cliaddr, len, filename);
    } else { // Must be OP_WRQ
        tftpWriteTransfer(transfer_sockfd, cliaddr, len, filename);
    }

    // 4. Cleanup and exit the child process
    printf("[Child PID %d] Transfer complete. Exiting.\n", getpid());
    close(transfer_sockfd);
    exit(EXIT_SUCCESS);
}

// --- TFTP TRANSFER LOGIC STUBS (Requires full implementation) ---

// Helper function to send an ERROR packet
void send_error(int sockfd, const struct sockaddr_in *cliaddr, socklen_t len, 
                int code, const char *message) {
    // ... (Implementation as shown previously)
    char error_packet[PACKET_BUF_SIZE];
    uint16_t *p = (uint16_t *)error_packet;
    
    *p++ = htons(OP_ERROR);
    *p++ = htons(code);
    
    strcpy((char *)p, message);
    size_t packet_size = 4 + strlen(message) + 1;

    if (sendto(sockfd, error_packet, packet_size, 0, 
               (const struct sockaddr *)cliaddr, len) < 0) {
        perror("Error sending error packet");
    }
}

