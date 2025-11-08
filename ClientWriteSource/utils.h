#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>

#define SERVER_PORT 69
#define MAX_BUFFER_SIZE 516     // 2 (Opcode) + 2 (Block #) + 512 (Data)
#define TFTP_DATA_SIZE 512      // Max data size per packet
#define MAX_RETRANSMIT 5        // Max retransmissions before giving up
#define TIMEOUT_SEC 3           // Timeout for socket receive (seconds)

// TFTP Opcodes (Network Byte Order)
#define OP_RRQ      1
#define OP_WRQ      2
#define OP_DATA     3
#define OP_ACK      4
#define OP_ERROR    5

// Transfer Mode
#define MODE "octet"


int createDataPacket(char *buffer, uint16_t block_num, const char *data, int data_len);
int createWrqPacket(char *buffer, const char *filename);
int SetUpSocket(const char *server_ip, struct sockaddr_in *serv_addr);
long int getFileSizeStat(const char* filename);