/*****************************************************
 * The TCP socket client to help you to test if your
 * OS supports c1000k(1 million connections).
 * @author: ideawu
 * @link: http://www.ideawu.com/
 * @modified:
 *    goby <goby@foxmail.com>
 *
 *    goby: Use epoll to continue data transmission.
 *****************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/resource.h>

static char * MESSAGE = "GET / HTTP/1.1 \r\nUser-Agent: AsyncBenchmark 0.1\r\nHost: 1\r\n\r\n";

static int
max_open_files(int nlimit) {
    struct rlimit limit;

    limit.rlim_cur = nlimit;
    limit.rlim_max = nlimit;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        fprintf(stderr, "[WARN]setrlimit() failed with errno=%d\n", errno);
        return -1;
    }
    return 0;
}

static int 
count_of_cpu() {
    long nprocs;
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) {
        fprintf(stderr, "[WARN]Could not determine count of CPUs. Use default 1.\n");
        nprocs = 1;
    }
    return nprocs;
}

static int 
make_socket_non_blocking (int sfd) {
    int flags, s;
    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        perror("set fcntl failed!");
        fprintf(stderr, "set fcntl failed.");
        return -1;
    }

    return 0;
}

static int
create_socket(struct sockaddr_in* addr, int *sock) {
    if (addr == NULL) return -1;
    if((*sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("create sock error.");
        return -1;
    }

    if(connect(*sock, 
               (struct sockaddr *)addr, 
               sizeof(struct sockaddr_in)) == -1){
        perror("connect to host failed");
        return -1;
    }

    int bufsize =5000;
    setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, 
        &bufsize, sizeof(bufsize));
    setsockopt(*sock, SOL_SOCKET, SO_RCVBUF, 
        &bufsize, sizeof(bufsize));
    
    if (make_socket_non_blocking(*sock) == -1) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv){
    if(argc <=  2){
        printf("Usage: %s ip port conn\n", argv[0]);
        exit(0);
    }

    struct sockaddr_in addr;
    const char *ip = argv[1];
    int base_port = atoi(argv[2]);
    int conn_count = atoi(argv[3]);
    int clients;
    int opt = 1;
    int bufsize;
    socklen_t optlen;
    int connections = 0;
    int efd;
    struct epoll_event events[64];
    char buffer[10240];
    int i;
    pid_t pid;
    long request_count = 0;
  
    clients = count_of_cpu();
    for (i = 0; i < clients; i++) {
        pid = fork();
        if(pid <= (pid_t)0) {
            sleep(1);
            break;
        }
    }

    if (pid < (pid_t) 0) {
        fprintf(stderr, "[ERRO]Problems forking worker num %d.\n", i);
        perror("fork failed");
        return 3;
    }

    if (pid != 0) {
        long pre_count = 0; 
        while(1) {
            long qps = request_count - pre_count;
            pre_count = request_count;
            fprintf(stdout, "[INFO] QPS: %ld\n", qps);
            sleep(1);
        }
        return 0;
    }

    max_open_files(655350);

    efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create1 failed");
        abort();
    }  

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    char tmp_data[10];
    int index = 0;
    while(connections <= conn_count){
        int port = base_port;

        struct epoll_event event;
        event.events = EPOLLOUT | EPOLLIN | EPOLLET;
        //printf("connect to %s:%d\n", ip, port);

        addr.sin_port = htons((short)port);

        int sock;
        if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
            goto sock_err;
        }
        if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1){
            goto sock_err;
        }
        connections ++;

        if(connections % 1000 == 999){
            printf("[INFO]Connections: %d, fd: %d\n", connections, sock);
        }
        usleep(1 * 1000);

        bufsize = 5000;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
        make_socket_non_blocking(sock);

        event.data.fd = sock;
        epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event);
    }
    fprintf(stdout, "Start requesting...\n");    
    while(1) { 
        int n, i;
        n = epoll_wait(efd, events, 64, -1);
        for(i = 0; i < n; i++) {
            if (events[i].events & EPOLLOUT) {
                int n = strlen(MESSAGE); 
                int nsend = 0; 
 
                while(n > 0) 
                { 
                    nsend = write(events[i].data.fd, MESSAGE + nsend, n); 
                    if (nsend == EPIPE) {
                        int sock;
                        struct epoll_event event;
                        event.events = EPOLLOUT | EPOLLIN | EPOLLET;
                        close(events[i].data.fd);
                        create_socket(&addr, &sock);
                        event.data.fd = sock;
                        epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event);
                    }
                    if(nsend < 0 && errno != EAGAIN) 
                    { 
                	fprintf(stderr, "epoll error!\n");
                        close(events[i].data.fd); 
                        perror("write error");
                        return (-1); 
                    } 
                    n -= nsend; 
                } 
            }
            if (events[i].events & EPOLLIN) {
                memset(buffer, 0, sizeof(buffer));
                int n = 0; 
                int nrecv = 0; 
 
                while(1){ 
                    nrecv = read(events[i].data.fd, buffer + n, 10239) ; 
                    if(nrecv == -1 && errno != EAGAIN) 
                    { 
                        perror("read error!"); 
                    } 
                    if((nrecv == -1 && errno == EAGAIN) || nrecv == 0) 
                    {
                        break; 
                    } 
                    n += nrecv;
                    request_count++;
                } 
            }
            else if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!((events[i].events & EPOLLIN) ||(events[i].events & EPOLLOUT) ))) {
                fprintf(stderr, "epoll error!\n");
                close(events[i].data.fd);
                continue;
            }
        }
    }
    fprintf(stderr, "Finished.\n");
    return 0;
sock_err:
    printf("connections: %d\n", connections);
    printf("error: %s\n", strerror(errno));
    return 0;
}
