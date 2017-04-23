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

#define TRY_COUNT 3
#define DATA_SIZE 3

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
    fprintf(stderr, "USAGE: %s domain port\n", name);
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0 && errno == EAGAIN)
            continue;
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int make_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

struct sockaddr_in make_address(char *address, char *port)
{
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if ((ret = getaddrinfo(address, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}

int connect_socket(char *name, char *port)
{
    struct sockaddr_in addr;
    int socketfd;
    socketfd = make_socket();
    addr = make_address(name, port);
    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        if (errno != EINTR)
            ERR("connect");
        else
        {
            fd_set wfds;
            int status;
            socklen_t size = sizeof(int);
            FD_ZERO(&wfds);
            FD_SET(socketfd, &wfds);
            if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &wfds, NULL, NULL)) < 0)
                ERR("select");
            if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
                ERR("getsockopt");
            if (0 != status)
                ERR("connect");
        }
    }
    return socketfd;
}

void prepare_data(int32_t data[DATA_SIZE])
{
    if (DATA_SIZE < 3) return;

    data[0] = htonl(rand() % 1000 + 1);
    data[1] = htonl(rand() % 1000 + 1);
    data[2] = htonl(rand() % 1000 + 1);
}

void print_data(int32_t data[])
{
    printf("(");
    for (int i = 0; i < DATA_SIZE; i++)
    {
        if (i < DATA_SIZE - 1)
            printf("%d, ", ntohl(data[i]));
        else
            printf("%d)\n", ntohl(data[i]));
    }
}

int check_hit(int32_t rcvdata, int32_t data[])
{
    for (int i = 0; i < DATA_SIZE; i++)
    {
        if (ntohl(data[i]) == rcvdata)
            return 1;
    }
    return 0;
}

void do_client(int fd)
{
    int32_t data[DATA_SIZE];
    
    for (int i = 0; i < TRY_COUNT; i++)
    {
        prepare_data(data);

        if (bulk_write(fd, (char *)data, sizeof(int32_t[DATA_SIZE])) < 0)
            ERR("write");
        printf("Sent data ");
        print_data(data);

        int32_t rcvdata;
        if (bulk_read(fd, (char *) &rcvdata, sizeof(int32_t)) < (int)sizeof(int32_t))
            ERR("read");
        rcvdata = ntohl(rcvdata);
        printf("Received data (%d)\n", rcvdata);
        
        if (check_hit(rcvdata, data))
            printf("HIT!\n");
    }
}

int main(int argc, char **argv)
{
    int fd, flags;

    if (argc != 3)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    srand((unsigned) time(NULL) * getpid());
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE");

    fd = connect_socket(argv[1], argv[2]);
    do_client(fd);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Client has terminated.\n");
    return EXIT_SUCCESS;
}