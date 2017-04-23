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
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define BACKLOG 3
#define MAX_CLIENTS 30
#define DATA_SIZE 3

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
    fprintf(stderr, "USAGE: %s port\n", name);
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

void initialize_clients(int client_socket[])
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_socket[i] = 0;
    }
}

int accept_client(int fd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(fd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}

void add_client(int client_socket[], int client)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) 
    {
        if (client_socket[i] == 0)
        {
            client_socket[i] = client;
            printf("Added new client\n");
            break;
        }
    }
}

int32_t get_max(int32_t data[])
{
    int i;
    int32_t max = -1;

    for (i = 0; i < DATA_SIZE; i++)
    {
        if (i == 0) 
            max = ntohl(data[i]);

        int32_t num = ntohl(data[i]); 
        if (num > max)
            max = num;
    }

    return max;
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

void do_server(int fd)
{
    int32_t data[DATA_SIZE];
    int max_fd = fd;
    int socket;
    int client_socket[MAX_CLIENTS];
    int received_count = 0;
    int max_number = 0;
    int size;
    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;

    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    initialize_clients(client_socket);

    while (do_work)
    {
        rfds = base_rfds;

        // Add clients to set
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            socket = client_socket[i];

            if (socket > 0)
                FD_SET(socket, &rfds);
            
            if (socket > max_fd)
                max_fd = socket;
        }

        // Wait for activity
        if (pselect(max_fd + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
            // Incoming connection
            if (FD_ISSET(fd, &rfds))
            {
                int client = accept_client(fd);
                add_client(client_socket, client);
                continue;
            }

            // IO operation
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                socket = client_socket[i];

                if (FD_ISSET(socket, &rfds))
                {
                    if ((size = bulk_read(socket, (char *) data, sizeof(int32_t[DATA_SIZE]))) < 0 && ECONNRESET != errno) 
                        ERR("read");
                    
                    if (size == (int) sizeof(int32_t[DATA_SIZE]))
                    {
                        printf("Received data ");
                        print_data(data);
                        
                        int32_t received_max = get_max(data);
                        if (received_count == 0 || received_max > max_number)
                            max_number = received_max;
                        received_count += DATA_SIZE;
                        
                        int32_t data_to_send = htonl(max_number);
                        if (bulk_write(socket, (char *) &data_to_send, sizeof(int32_t)) < 0 && errno != EPIPE)
                            ERR("write");

                        printf("Sent data (%d)\n", max_number);
                        
                    }
                    else
                    {
                        if (TEMP_FAILURE_RETRY(close(socket)) < 0)
                            ERR("close");
                        client_socket[i] = 0;
                        printf("Client disconnected.\n");
                    }
                }
            }
        }
        else
        {
            if (EINTR == errno)
                continue;
            ERR("pselect");
        }
    }

    printf("Received count: %d\n", received_count);
}

int main(int argc, char **argv)
{
    int fd, flags;

    if (argc != 2)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT");

    fd = bind_inet_socket(atoi(argv[1]), SOCK_STREAM);
    flags = fcntl(fd, F_GETFL) | O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
    do_server(fd);

    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}