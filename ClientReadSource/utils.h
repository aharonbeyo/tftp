#ifndef UTILS_H_H
#define UTILS_H_H

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

void send_ack(int sockfd, const struct sockaddr_in *target_addr, socklen_t len, uint16_t block);
int mainTransferLogic(int sockfd, const char *local_filename);
int packetProcessingLogic(int sockfd, uint16_t *expected_block, int *retries, int *file_transfer_complete, char *recv_buffer, ssize_t numberBytesReceived, struct sockaddr_in *remote_transfer_addr, int remote_len,int fd);
int ConstructAndSendRRQ(int sockfd, const struct sockaddr_in *servaddr, const char *filename);
int SetupSocket(const char *server_ip, struct sockaddr_in *servaddr);
#endif