#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <sys/stat.h>
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

#define LIFETIME 7
#define SLEEPMIN 500
#define SLEEPMAX 2500
volatile sig_atomic_t quit = 0;

void sigint_handler(int sig)
{
    quit = 1;
}

void sigalarm_handler(int sig)
{
    quit = 1;
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

void prepare_ready_request(int32_t data[5])
{
    // 0 - ready request
    // 1 - operand1
    // 2 - operand2
    // 3 - operation
    // 4 - result

    data[0] = htonl((int32_t) 1);
    data[1] = data[2] = data[3] = data[4] = htonl((int32_t) 0);
}

int send_data(int fd, struct sockaddr_in addr, socklen_t size, int32_t data[5])
{
    if (sendto(fd, data, sizeof(int32_t[5]), 0, (struct sockaddr*) &addr, size) < 0)
    {
        if (errno == EINTR && quit == 1)
            return 0;
        ERR("sendto");
    }
    return 1;
}

int receive_data(int fd, struct sockaddr_in addr, socklen_t size, int32_t data[5])
{
    if (recvfrom(fd, data, sizeof(int32_t[5]), 0, (struct sockaddr*) &addr, &size) < 0)
    {
        if (errno == EINTR && quit == 1)
            return 0;
        ERR("recvfrom");
    }
    return 1;
}

void print_task(int32_t data[5])
{
    printf("Task received: %d %c %d = ?\n", ntohl(data[1]), ntohl(data[3]), ntohl(data[2]));
}

void solve_task(int32_t data[5])
{
    int32_t op1, op2, result;
    op1 = ntohl(data[1]);
    op2 = ntohl(data[2]);

    switch ((char) ntohl(data[3]))
    {
        case '+':
            result = op1 + op2;
        break;

        case '-':
            result = op1 - op2;
        break;

        case '*':
            result = op1 * op2;
        break;
    }

    data[4] = htonl(result);
}

int client_sleep()
{
    int sleep_time = rand() % (SLEEPMAX - SLEEPMIN + 1) + SLEEPMIN;
    struct timespec time;
    time.tv_sec = (int) (sleep_time / 1000);
    time.tv_nsec = (sleep_time - time.tv_sec * 1000) * 1000;

    printf("Sleeping for %d...\n", sleep_time);

    if (nanosleep(&time, NULL) < 0)
    {
        if (errno == EINTR && quit == 1)
            return 0;
            
        ERR("nanosleep");
    }

    return 1;
}

void do_client(int fd, struct sockaddr_in addr, socklen_t size)
{
    int32_t data[5];
    alarm(LIFETIME);

    // Ready request
    prepare_ready_request(data);
    if (!send_data(fd, addr, size, data)) return;
    printf("Sent ready request.\n");

    // Task
    if (!receive_data(fd, addr, size, data)) return;
    print_task(data);
    solve_task(data);
    if (!client_sleep()) return;
    if (!send_data(fd, addr, size, data)) return;
    printf("Sent task answer.\n");

    // Retransmission
    printf("Waiting for retransmission task...\n");
    if (!receive_data(fd, addr, size, data)) return;
    printf("Received retransmission task.\n");
    solve_task(data);
    if (!send_data(fd, addr, size, data)) return;
    printf("Sent retransmission task answer.\n");
}

int main(int argc, char **argv)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t size = sizeof(addr);
    
    if (argc != 3)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    srand(time(NULL));
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    if (sethandler(sigalarm_handler, SIGALRM))
        ERR("Settin SIGALRM");
    
    fd = make_socket();
    addr = make_address(argv[1], argv[2]);
    do_client(fd, addr, size);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Client has terminated.\n");
    return EXIT_SUCCESS;
}