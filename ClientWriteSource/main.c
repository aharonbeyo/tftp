#include "utils.h"

void tftpWriteFile (const char *server_ip, const char *local_filename, const char *remote_filename);

int main(int argc, char *argv[]) 
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <local_file_to_send> <remote_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    tftpWriteFile(argv[1], argv[2], argv[3]);
    
    return EXIT_SUCCESS;    
}