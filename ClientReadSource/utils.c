#include "utils.h"


void send_ack(int sockfd, const struct sockaddr_in *target_addr, socklen_t len, uint16_t block) 
{
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

    if (sendto(sockfd, rrq_packet, rrq_len, 0, (const struct sockaddr *)servaddr, sizeof(struct sockaddr_in)) < 0) {
        perror("Failed to send RRQ"); close(sockfd); 
        return -1;
    }
    printf("Sent RRQ for file '%s'. Waiting for DATA 1...\n", filename);

    return 0;
}
