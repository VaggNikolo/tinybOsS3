#include "tinyos.h"
#include "util.h"
#include "kernel_dev.h"
#include "kernel_pipe.h"

/*
A socket can be:
				- A listener if it listens
				- A peer if it's connected (maybe a listener at the same time)
				- Unbound if it's not connected
*/
typedef struct peer_socket
{
	pipe_cb *readPipe;
	pipe_cb *writePipe;
} sockPee;

typedef struct listener_socket
{
	rlnode request_queue;
	CondVar req;
} sockLis;

typedef enum socket_state
{
	UNBOUND,
	PEER,
	LISTENER
} sockType;


typedef struct socket_control_block
{
	sockType type;
	port_t port;

	union
	{
		sockLis listener;
		sockPee peer;
	};

} socketCB;


typedef struct queue_node
{
	Fid_t fid; 
	socketCB *reqSock;
	rlnode node;
	CondVar cv;
	int admitted;
} qNode;


socketCB *PORT_MAP[MAX_PORT + 1] = {NULL};

int socket_read(void *this, char *buf, unsigned int size);
int socket_write(void *this, const char *buf, unsigned int size);
int socket_close(void *this);