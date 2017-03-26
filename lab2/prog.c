#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
                                     exit(EXIT_FAILURE))

#define MAX_PEERS 5
#define MAX_MESSAGES_COUNT 10
#define MAX_MESSAGE_LENGTH 20
#define MAX_MESSAGE_LENGTH_STR "20"

typedef struct neighbor
{
	pid_t pid;
	mqd_t queue;
} neighbor;

typedef enum msgType {REGISTRATION, TEXT, EXIT} msgType;

typedef struct message
{
	pid_t last; // last node to hold the message
	pid_t from, to; // original from & to
	msgType type;
	char content[MAX_MESSAGE_LENGTH];
} message;

struct node
{
	pid_t pid;
	char queueName[50];
	mqd_t queue;
	neighbor neighbors[MAX_PEERS];
	int neighborsCount;
} node;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s [pid]\n", name);
    fprintf(stderr, "pid - process to connect with\n");
    exit(EXIT_FAILURE);
}

void sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

// Checks if the process exists
int checkProcess(pid_t pid)
{
	if (0 == kill(pid, 0))
		return 1;
	return 0;
}

// Initialize process's queue
void initializeQueue()
{
	struct mq_attr attr;

    attr.mq_maxmsg = MAX_MESSAGES_COUNT;
    attr.mq_msgsize = sizeof(message);
   	sprintf(node.queueName, "/%d_queue", node.pid);

    if ((node.queue = TEMP_FAILURE_RETRY(mq_open(node.queueName, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t) -1) ERR("mq_open");
}

// Open pid's queue (with no create)
mqd_t openQueue(pid_t pid)
{
	char queueName[50];
	mqd_t queue;

	sprintf(queueName, "/%d_queue", pid);
	if ((queue = TEMP_FAILURE_RETRY(mq_open(queueName, O_RDWR | O_NONBLOCK))) == (mqd_t) -1) ERR("mq_open");

	return queue;
}

// Send register message after establishing a connection
void sendRegistrationMessage(mqd_t queue)
{
	message msg;
	msg.from = node.pid;
	msg.type = REGISTRATION;

	printf("[%d] Sending registration request\n", node.pid);

	if (TEMP_FAILURE_RETRY(mq_send(queue, (const char*) &msg, sizeof(message), 1))) ERR("mq_send");
}

mqd_t neighborQueue(pid_t npid)
{
	for (int i = 0; i < node.neighborsCount; i++)
		if(node.neighbors[i].pid == npid && checkProcess(npid))
			return node.neighbors[i].queue;
	return 0;
}

void printNeighbors()
{
	printf("[%d] Neighbors\n", node.pid);
	for (int i = 0; i < node.neighborsCount; i++)
	{
		printf(" %d: %d\n", i, node.neighbors[i].pid);
	}
}

neighbor createNeighbor(pid_t npid, mqd_t queue)
{
	neighbor neighbor;
	neighbor.pid = npid;
	neighbor.queue = queue;
	return neighbor;
}

// Add neighbor to neighbors
int addNeighbor(pid_t npid, mqd_t queue)
{
	if (node.neighborsCount + 1 > MAX_PEERS)
	{
		printf("[%d] Too many neighbors\n", node.pid);
		return -1;
	}

	node.neighbors[node.neighborsCount] = createNeighbor(npid, queue);

	printf("[%d] Added neighbor: %d\n", node.pid, npid);
	
	return node.neighborsCount++;
}

int registerNeighbor(pid_t npid)
{
	mqd_t nqueue = openQueue(npid);

	int nIndex = addNeighbor(npid, nqueue);

	return nIndex;
}

void initializeNode()
{
	node.pid = getpid();
    node.neighborsCount = 0;

    printf("[%d] Initialized\n", node.pid);
}

void setQueueNotifier()
{
	static struct sigevent not;
    not.sigev_notify = SIGEV_SIGNAL;
    not.sigev_signo = SIGRTMIN;
    if (mq_notify(node.queue, &not) < 0) ERR("mq_notify");
}

void sendTextMessage(pid_t last, pid_t from, pid_t to, char *content)
{
	message msg;
	msg.last = node.pid;
	msg.from = from;
	msg.to = to;
	msg.type = TEXT;
	strncpy(msg.content, content, MAX_MESSAGE_LENGTH);
	mqd_t queue = neighborQueue(to);

	// queue = 0 means that there is no neighbor with that pid (send to all neighbors)
	if (queue == 0)
	{
		for (int i = 0; i < node.neighborsCount; i++)
			if(node.neighbors[i].pid != last && node.neighbors[i].pid != from)
				if (TEMP_FAILURE_RETRY(mq_send(node.neighbors[i].queue, (const char*) &msg, sizeof(message), 2))) ERR("mq_send");
		return;
	}

	if (TEMP_FAILURE_RETRY(mq_send(queue, (const char*) &msg, sizeof(message), 2))) ERR("mq_send");
}

// Send exit message to neighbors
void sendExitMessageToNeighbors(int receivedFrom)
{
	message msg;
	msg.from = node.pid;
	msg.type = EXIT;

	for (int i = 0; i < node.neighborsCount; i++)
	{
		if (receivedFrom != node.neighbors[i].pid && checkProcess(node.neighbors[i].pid))
		{
			printf("[%d] Sending exit message to %d\n", node.pid, node.neighbors[i].pid);
			if (TEMP_FAILURE_RETRY(mq_send(node.neighbors[i].queue, (const char*) &msg, sizeof(message), 3))) ERR("mq_send");
		}
	}
}

void cleanAndQuit()
{
	mq_close(node.queue);
	if (mq_unlink(node.queueName)) ERR("mq unlink");

	for (int i = 0; i < node.neighborsCount; i++)
		mq_close(node.neighbors[i].queue);

	printf("[%d] Terminating...\n", node.pid);
	exit(EXIT_SUCCESS);
}

void exitHandler(int sig)
{
	sendExitMessageToNeighbors(-1);
	cleanAndQuit();
}

// Handler for receiving messages in own queue
void receivedMessageHandler(int sig)
{
	message rmsg;
	unsigned msg_prio;

	setQueueNotifier();

	while (1)
	{
        if (mq_receive(node.queue, (char*) &rmsg, sizeof(message), &msg_prio) < 1) 
        {
            if(errno == EAGAIN) break;
            else ERR("mq_receive");
        }

        switch(rmsg.type)
    	{
    		case REGISTRATION:
    		{
    			printf("[%d] Received registration request, adding to neighbors...\n", node.pid);
    			registerNeighbor(rmsg.from);
    			//printNeighbors();
    		}
    		break;

    		case TEXT:
    		{
    			if (rmsg.to == node.pid)
    				printf("[%d] Message from %d: %s\n", node.pid, rmsg.from, rmsg.content);
    			else
    				sendTextMessage(rmsg.last, rmsg.from, rmsg.to, rmsg.content);
    		}
    		break;

    		case EXIT:
    		{
    			printf("[%d] Received exit request, processing...\n", node.pid);
    			sendExitMessageToNeighbors(rmsg.from);
    			cleanAndQuit();
    		}
    		break;
        }
    }
}

// Process's work
void nodeWork()
{
	char buf[50];
	pid_t npid;
	char content[MAX_MESSAGE_LENGTH];

	// Wait for stdin
	while (1) 
	{
		if (read(STDIN_FILENO, buf, sizeof(buf)) < 0)
		{
			if(errno == EINTR)
			{
				//printf("[%d] Reading interrupted\n", node.pid);
				continue;
			}
		}

		sscanf(buf, "%d %"MAX_MESSAGE_LENGTH_STR"[^\n]c", (int*) &npid, content);

		if (checkProcess(npid))
			sendTextMessage(node.pid, node.pid, npid, content);
		else
			printf("[%d] There is no process with PID %d\n", node.pid, npid);
	}
}

int main(int argc, char **argv) 
{
    if (argc > 2) usage(argv[0]);

    initializeNode();
    initializeQueue();
	
    sethandler(exitHandler, SIGINT);
    sethandler(receivedMessageHandler, SIGRTMIN);
    setQueueNotifier();

    if (argc == 2)
    {
    	int neighbor = atoi(argv[1]);

    	if (checkProcess(neighbor))
	    {
	    	int nIndex = registerNeighbor(neighbor);
	    	if (nIndex != -1)
	    		sendRegistrationMessage(node.neighbors[nIndex].queue);
	    } 
	    else 
	    {
	    	printf("[%d] There is no process with PID %d\n", node.pid, neighbor);
	    }
    }

    nodeWork();

    return EXIT_SUCCESS;
}
