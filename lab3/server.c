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

#define BACKLOG 3
#define PORT 2000
volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig)
{
    do_work = 0;
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
    fprintf(stderr, "USAGE: %s\n", name);
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

int is_ready_request(int32_t data[5])
{
    return ntohl(data[0]);
}

void prepare_task(int32_t data[5])
{
    // 0 - ready request
    // 1 - operand1
    // 2 - operand2
    // 3 - operation
    // 4 - result

    data[0] = htonl(0);
    data[1] = htonl(rand() % 10);
    data[2] = htonl(rand() % 10);
    data[3] = htonl((int32_t)((rand() % 2 ? (rand() % 2 ? '+' : '-') : '*')));
    data[4] = htonl(0);
}

int send_task(int fd, struct sockaddr_in addr, socklen_t size, int32_t data[5])
{
    if (sendto(fd, data, sizeof(int32_t[5]), 0, (struct sockaddr *) &addr, size) < 0)
    {
        if (ECONNRESET == errno)
        {
            fprintf(stderr, "Connection reset by peer.\n");
            return 0;
        }
        else
            ERR("send");
    }

    return 1;
}

void print_answer(int32_t data[5])
{
    printf("%d %c %d = %d\n", ntohl(data[1]), ntohl(data[3]), ntohl(data[2]), ntohl(data[4]));
}

void do_server(int fd)
{
    int client_connected = 0;
    int task_sent = 0;
    int tasks_count = 0;
    int fd_res;
    int retransmission_request  = 0;
    int retransmission_sent = 0;
    int32_t data[5];
    fd_set base_rfds, rfds;
    struct sockaddr_in addr, connected_addr;
    socklen_t size = sizeof(addr);
    struct timespec* timeout = NULL;
    struct timespec time;

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);

    while (do_work)
    {
        rfds = base_rfds;

        timeout = NULL;
        if (client_connected && task_sent) 
        {
            time.tv_sec = 2;
            timeout = &time;
            retransmission_request = 1;
        }

        if ((fd_res = pselect(fd + 1, &rfds, NULL, NULL, timeout, &oldmask)) > 0)
        {
            if (recvfrom(fd, data, sizeof(int32_t[5]), 0, (struct sockaddr *) &addr, &size) < 0)
            {
                if (errno == EINTR)
                    continue;
                ERR("recvfrom");
            }
            
            // Check if we already handle any client
            if (is_ready_request(data))
            {
                printf("Received ready request.\n");
                if (client_connected)
                {
                    printf("Already connected with other client.\n");
                    continue;
                }
                else 
                {
                    client_connected = 1;
                    connected_addr = addr;
                    task_sent = 0;
                }
            }

            if (!task_sent)
            {
                prepare_task(data);

                if (send_task(fd, connected_addr, size, data))
                {
                    tasks_count++;
                    task_sent = 1;
                }

                printf("Sent task.\n");
                continue;
            }
            
            // Answer received
            print_answer(data);
            
            client_connected = 0;
            retransmission_request = 0;
            retransmission_sent = 0;
        }
        else
        {
            if (fd_res == 0)
            {
                if (retransmission_request & !retransmission_sent)
                {
                    if (send_task(fd, connected_addr, size, data))
                    {
                        tasks_count++;
                        task_sent = 1;
                    }
                    retransmission_sent = 1;
                    printf("Sent retransmission task.\n");
                }
                else
                {
                    client_connected = 0;
                    retransmission_request = 0;
                    retransmission_sent = 0;
                    printf("Client disconnected.\n");
                }

                continue;
            }

            if (errno == EINTR)
                continue;
            ERR("pselect");
        }
    }

    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    printf("Tasks sent: %d\n", tasks_count);
}

int main(int argc, char **argv)
{
    int fd;

    if (argc != 1)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    srand(time(NULL));
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT");

    fd = bind_inet_socket(PORT, SOCK_DGRAM);;
    do_server(fd);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}