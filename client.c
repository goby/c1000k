/*****************************************************
 * The TCP socket client to help you to test if your
 * OS supports c1000k(1 million connections).
 * @author: ideawu
 * @link: http://www.ideawu.com/
 *****************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define MAX_PORTS 1

static char * MESSAGE = "p";

int main(int argc, char **argv){
    if(argc <=  2){
        printf("Usage: %s ip port conn\n", argv[0]);
        exit(0);
    }

    struct sockaddr_in addr;
    const char *ip = argv[1];
    int base_port = atoi(argv[2]);
    int conn_count = atoi(argv[3]);
    int opt = 1;
    int bufsize;
    socklen_t optlen;
    int connections = 0;
    int *socks;

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    socks = calloc(conn_count, sizeof(int));

    char tmp_data[10];
    int index = 0;
    while(connections <= conn_count){
        if(++index >= MAX_PORTS){
            index = 0;
        }
        int port = base_port + index;
        //printf("connect to %s:%d\n", ip, port);

        addr.sin_port = htons((short)port);

        int sock;
        if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
            goto sock_err;
        }
        if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1){
            goto sock_err;
        }
        socks[connections] = sock;

        connections ++;

        if(connections % 1000 == 999){
            //printf("press Enter to continue: ");
            //getchar();
            printf("connections: %d, fd: %d\n", connections, sock);
        }
        usleep(1 * 1000);

        bufsize = 5000;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    }

    while(1) { 
        for (index = 0; index < conn_count; index++) {
            write(socks[index], MESSAGE, 1);
            if (index % 1000 == 0) usleep(1000);
        }
    }

    return 0;
sock_err:
    printf("connections: %d\n", connections);
    printf("error: %s\n", strerror(errno));
    return 0;
}
