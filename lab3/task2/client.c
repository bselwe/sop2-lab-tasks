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

#define PORT "2000"
#define RANDMIN 10000000
#define RANDMAX 100000000

volatile sig_atomic_t last_signal = 0;

void sigalrm_handler(int sig)
{
    last_signal = sig;
}

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
    fprintf(stderr, "USAGE: %s domain\n", name);
}

int make_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_DGRAM, 0);
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

void send_and_receive(int fd, struct sockaddr_in addr, int32_t data, int retransmission)
{
    struct itimerval ts;

    if (TEMP_FAILURE_RETRY(sendto(fd, &data, sizeof(int32_t), 0, (struct sockaddr *) &addr, sizeof(addr))) < 0)
        ERR("sendto");
    printf("Number sent: %d\n", ntohl(data));

    if (!retransmission)
    {
        memset(&ts, 0, sizeof(struct itimerval));
        ts.it_value.tv_usec = 300000; // 0.3s
        setitimer(ITIMER_REAL, &ts, NULL);
        last_signal = 0;
    }

    int32_t rcvdata;
    while (recv(fd, &rcvdata, sizeof(int32_t), 0) < 0)
    {
        if (EINTR != errno)
            ERR("recv");

        if (SIGALRM == last_signal)
        {
            send_and_receive(fd, addr, data, 1);
            return;
        }
    }

    // Received data
    printf("Number received: %d\n", ntohl(rcvdata));
}

void do_client(int fd, struct sockaddr_in addr)
{
    int32_t data = htonl(rand() % (RANDMAX - RANDMIN + 1) + RANDMIN);
    printf("Number generated: %d\n", ntohl(data));
    send_and_receive(fd, addr, data, 0);
}

int main(int argc, char **argv)
{
    int fd;
    struct sockaddr_in addr;

    if (argc != 2)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    srand((unsigned) time(NULL) * getpid());
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigalrm_handler, SIGALRM))
        ERR("Seting SIGALRM:");

    fd = make_socket();
    addr = make_address(argv[1], PORT);
    do_client(fd, addr);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Client has terminated.\n");
    return EXIT_SUCCESS;
}