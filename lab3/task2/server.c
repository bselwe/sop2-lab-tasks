#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#define ERR(source) (perror(source),                                 \
                     fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), \
                     exit(EXIT_FAILURE))

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ \
   typeof (exp) _rc; \
   do { \
     _rc = (exp); \
   } while (_rc == -1 && errno == EINTR); \
   _rc; })
#endif

#define PORT 2000
#define BACKLOG 3
#define MASKLENGTH 27 // 8 digit number requires at most 27 bits

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s\n", name);
}

int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

int bind_inet_socket(uint16_t port, int type)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (SOCK_STREAM == type)
        if (listen(socketfd, BACKLOG) < 0)
            ERR("listen");
    return socketfd;
}

void do_server(int fd)
{
    int32_t data, mask = 0;
    struct sockaddr_in addr;
    socklen_t size = sizeof(addr);
    
    while (1)
    {
        if (TEMP_FAILURE_RETRY(recvfrom(fd, &data, sizeof(int32_t), 0, (struct sockaddr *) &addr, &size)) < 0)
            ERR("recvfrom");

        data = ntohl(data);
        printf("Number received: %d\n", data);
        mask = mask | data;

        int send = (rand() % 10 + 1) <= 7 ? 1 : 0;
        if (!send)
            continue;

        int32_t data = htonl(mask);
        if (TEMP_FAILURE_RETRY(sendto(fd, &data, sizeof(int32_t), 0, (struct sockaddr *) &addr, size)) < 0)
        {
            if (ECONNRESET == errno)
                continue;
            ERR("send");
        }
        printf("Number sent: %d\n", mask);

        if (mask == ~(~0 << MASKLENGTH))
        {
            printf("Stop processing.\n");
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int fd;

    if (argc != 1)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    srand((unsigned) time(NULL) * getpid());
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE");

    fd = bind_inet_socket(PORT, SOCK_DGRAM);;
    do_server(fd);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}