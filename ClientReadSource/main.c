// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>

#include "utils.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <filename>\n", argv[0]);
        return 1;
    }

    struct sockaddr_in servaddr;
     
    char *server_ip = argv[1];
    char *remote_filename = argv[2];
    char local_filename[256]; // Use the same name for local save

    // Create local filename (e.g., just the provided name)
    strncpy(local_filename, remote_filename, 255); 
    int sockfd = SetupSocket(server_ip, &servaddr);
  
    if (sockfd < 0) {
        return -1;
    }   

    if (ConstructAndSendRRQ(sockfd, &servaddr, remote_filename) < 0) 
    {
        close(sockfd);
        return -2;
    }   
   
   mainTransferLogic(sockfd, local_filename);
   close(sockfd);
}