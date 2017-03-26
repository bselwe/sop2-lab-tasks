#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <limits.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ \
   typeof (exp) _rc; \
   do { \
     _rc = (exp); \
   } while (_rc == -1 && errno == EINTR); \
   _rc; })
#endif

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
                     exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

void usage(char *name)
{
    fprintf(stderr,"USAGE: %s\n", name);
    exit(EXIT_FAILURE);
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

void sig_handler(int sig)
{
    last_signal = sig;
}

void sigchld_handler(int sig)
{
    pid_t pid;
    while(1) 
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid) return;
        if (0 >= pid) 
        {
            if (ECHILD == errno) return;
            ERR("waitpid");
        }
    }
}

// void make_fifo(char *path, mode_t mode)
// {
// 	int status = mkfifo(path, mode);
// 	if (status == -1 && errno != EEXIST) ERR("mkfifo");
// 	if (errno != EEXIST)
// 		printf("[%d] FIFO created\n", getpid());
// }

void clean_fd(int *fd)
{
	if (close(fd[0])) ERR("close");
	if (close(fd[1])) ERR("close");
}

char* generate_message(int l)
{
	//int l = rand() % 100;
	int tmp = l;
	unsigned char c = 0; // 0 - 255

	while(tmp > 0) 
	{
		tmp /= 10;
		c++;
	}

	if(l == 0) c = 1;

	char *buf;
	if (NULL == (buf = (char*) malloc(sizeof(char) * (c + 1)))) ERR("malloc");
	buf[0] = c;
	sprintf(buf + 1, "%d", l);

	return buf;
}

void child_work(int *fd)
{
	int readfd = fd[0];
	int writefd = fd[1];

	//printf("[%d] read id: %d\n", getpid(), readfd);
	//printf("[%d] write id: %d\n", getpid(), writefd);

	srand(getpid());

	char *readMsg, *writeMsg;
	unsigned char readLength, writeLength;
	int number;

	while(last_signal != SIGINT)
	{
		// Read
		if (TEMP_FAILURE_RETRY(read(readfd, &readLength, 1)) < 1) 
			if (errno != EINTR && errno != EPIPE) ERR("read");
		if (NULL == (readMsg = (char*) malloc(sizeof(char) * readLength))) ERR("malloc");
		if (TEMP_FAILURE_RETRY(read(readfd, readMsg, readLength)) < readLength)
			if (errno != EINTR && errno != EPIPE) ERR("read");
		number = atoi(readMsg);
		printf("[%d] read msg: %d\n", getpid(), number);
		sleep(1);

		// Write
		writeMsg = generate_message(number);
		writeLength = writeMsg[0] + 1;
		if (TEMP_FAILURE_RETRY(write(writefd, writeMsg, writeLength)) < 0)
			if (errno != EINTR && errno != EPIPE) ERR("write");

		//free(readMsg);
		//free(writeMsg);
	}
}

void parent_work(int *fd)
{
	int readfd = fd[0];
	int writefd = fd[1];

	//printf("[PARENT] read id: %d\n", readfd);
	//printf("[PARENT] write id: %d\n", writefd);

	srand(getpid());

	char *readMsg, *writeMsg;
	unsigned char readLength, writeLength;
	int number = 1;

	while(last_signal != SIGINT)
	{
		// Write
		writeMsg = generate_message(number);
		writeLength = writeMsg[0] + 1;
		if (TEMP_FAILURE_RETRY(write(writefd, writeMsg, writeLength)) < 0) 
			if (errno != EINTR && errno != EPIPE) ERR("write");

		// Read
		if (TEMP_FAILURE_RETRY(read(readfd, &readLength, 1)) < 1) 
			if (errno != EINTR && errno != EPIPE) ERR("read");
		if (NULL == (readMsg = (char*) malloc(sizeof(char) * readLength))) ERR("malloc");
		if (TEMP_FAILURE_RETRY(read(readfd, readMsg, readLength)) < readLength)
			if (errno != EINTR && errno != EPIPE) ERR("read");
		number = atoi(readMsg);
		printf("[PARENT] read msg: %d\n", number);
		sleep(1);

		//free(readMsg);
		//free(writeMsg);
	}
}

void create_children(int n, int *fd, int *fds)
{
	int tmpfd[2];
	int j = 0;

	// Utworzenie procesow potomnych
	for (int i = 0; i < n; i++)
	{
		if (pipe(tmpfd)) ERR("pipe");

		if (i > 0)
		{
			switch (fork())
			{
				case 0:
				{
					if (close(tmpfd[0])) ERR("close");

					fd[0] = fds[--j];
					fd[1] = tmpfd[1];

					while (--j >= 0) if (fds[j] && close(fds[j])) ERR("close");
					free(fds);

					child_work(fd);

					clean_fd(fd);
					exit(EXIT_SUCCESS);
				}
				break;

				case -1:
					ERR("fork");
				break;
			}
		}

		fds[j++] = tmpfd[1];
		fds[j++] = tmpfd[0];
	}

	// Przyporzadkowanie pipe'ow procesowi glownemu
	fd[0] = fds[--j];
	fd[1] = fds[0];

	while (--j > 0) if (fds[j] && close(fds[j])) ERR("close");
	free(fds);
}

int main(int argc, char** argv)
{	
	if (argc != 1) usage(argv[0]);
	if (sethandler(sig_handler, SIGINT)) ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGINT handler");
    if (sethandler(sigchld_handler, SIGCHLD)) ERR("Setting parent SIGCHLD:");

    int n = 3; // ilość procesów w "obiegu"
    int fd[2], *fds;
	if (NULL == (fds = (int*) malloc(sizeof(int) * 2 * n))) ERR("malloc");

	create_children(n, fd, fds);
	parent_work(fd);

	clean_fd(fd);
	while(wait(NULL)>0);
	return EXIT_SUCCESS;
}