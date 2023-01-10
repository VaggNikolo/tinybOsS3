#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"

/*
The listener calls the socket and the socket calls
bind() which points to the port of listener bades on the port table we created.
Then listen() is called and is on standby waiting for new connections through accept().
The listener keeps track of all the requests and serves them serially.
When someone connects it follows a read and write procedure.
So we have created socket_read() and socket_write() which implement the pipe features.
When the procedure is over, the requester calls close() and it sends EOF to our listener
so it can call close() too once it serves all requests.
*/

file_ops socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close};

int socket_read(void *this, char *buf, unsigned int size)
{
	// Grab the SCB
	socketCB *cb = (socketCB *)this;

	// If it's connected and the reader is open read
	if (cb->type == PEER && cb->peer.readPipe != NULL)
	{
		// Use the pipe_read
		return pipe_read(cb->peer.readPipe, buf, size); //
	}
	else
		// Else there's nothing to read
		return NOFILE;
}

int socket_write(void *this, const char *buf, unsigned int size)
{
	// Grab the SCB
	socketCB *cb = (socketCB *)this;

	// If it's connected and the writer is open read
	if (cb->type == PEER && cb->peer.writePipe != NULL)
	{
		// Use the pipe_write
		return pipe_write(cb->peer.writePipe, buf, size);
	}
	else
		// Else there's nothing to read
		return NOFILE;
}

int socket_close(void *this)
{
	// If the object is valid
	if (this != NULL)
	{
		// Grab the SCB
		socketCB *cb = (socketCB *)this;

		// If it's connected
		if (cb->type == PEER)
		{
			int r, w;
			r = pipe_reader_close(cb->peer.readPipe);
			w = pipe_writer_close(cb->peer.writePipe);
			if (r + w != 0) // 0 means they are closed, anything else means something is open or both
				return -1;
		}

		// If it's a listener
		if (cb->type == LISTENER)
		{
			PORT_MAP[cb->port] = NULL;			 // Take it out of the port table
			kernel_broadcast(&cb->listener.req); // Wake it up
		}
		cb = NULL;
		return 0;
	}
	return NOFILE;
}

Fid_t sys_Socket(port_t port)
{
	// Returns a new socket bound on a port

	/*We reserve an FCB, acquiring the first available place in the FIDT.
	We then acquire our FCB and connect it with our FID*/

	if (port < NOPORT || port > MAX_PORT)
		return NOFILE;

	// Local FID and FCB arrays
	Fid_t fid[1];
	FCB *fcb[1];
	// Dynamic allocation of the socket
	socketCB *cb = (socketCB *)xmalloc(sizeof(socketCB));
	// We get an FCB
	int reservedFCB = FCB_reserve(1, fid, fcb);
	// If it can't reserve the FCB it fails
	if (!reservedFCB)
		return NOFILE;

	// Point our socket port to the one given
	cb->port = port;
	// Make the socket type unbound
	cb->type = UNBOUND;

	// We point out FCB's stream object to our socket
	fcb[0]->streamobj = cb;
	// And the stream functions to our defined socket function struct
	fcb[0]->streamfunc = &socket_file_ops;

	// If our port already has something
	if (PORT_MAP[port] != NULL)
	{
		// Return the FID
		return fid[0];
	}
	// Else place the socket on the table
	PORT_MAP[port] = cb;
	// And return the FID
	return fid[0];
}

int sys_Listen(Fid_t sock)
{
	// Here we turn our unbound socket into a listening socket

	// We grab the FCB pointing to the FID given
	FCB *fcb = get_fcb(sock);

	// If the FCB exists and it has functions
	if (fcb != NULL && fcb->streamfunc == &socket_file_ops)
	{

		// We grab the socket from the FCB
		socketCB *cb = fcb->streamobj;

		if (cb == NULL) // The socket doesn't exist
			return -1;

		if (cb->type != UNBOUND) // The socket isn't unbound
			return -1;

		if ((PORT_MAP[cb->port])->type == LISTENER) // Is already a listener
			return -1;

		if (cb->port <= NOPORT || cb->port > MAX_PORT) // Port's not inside the range
			return -1;

		// Make the socket's type a listener
		cb->type = LISTENER;
		// Initialize our CondVar that is for checking if we have a new request on our list
		cb->listener.req = COND_INIT;
		// Initialize the queue on unionTypeListener
		rlnode_init(&(cb->listener.request_queue), NULL);

		return 0; // All went well
	}
	return -1; // Something went wrong
}

Fid_t sys_Accept(Fid_t lsock)
{
	// Waits for a connection

	// We grab the FCB pointing to the FID given
	FCB *fcb = get_fcb(lsock);

	// If the FCB exists and it has functions
	if (fcb != NULL && fcb->streamfunc == &socket_file_ops)
	{

		// We grab the socket from the FCB
		socketCB *cb = fcb->streamobj;

		if (cb == NULL) // The socket doesn't exist
			return -1;

		if (cb->port <= NOPORT || cb->port > MAX_PORT) // Port's not inside the range
			return -1;

		if ((PORT_MAP[cb->port])->type != LISTENER) // Not pointing to a listener
			return -1;

		if (cb->type == PEER) // Port is already connected
			return -1;

		socketCB *l = PORT_MAP[cb->port];

		// While the request list is empty
		while (is_rlist_empty(&(PORT_MAP[cb->port]->listener.request_queue)))
		{
			kernel_wait(&(PORT_MAP[cb->port]->listener.req), SCHED_USER); // Wait for a request to wake it up
			if (PORT_MAP[cb->port] == NULL)
				return -1;
		}

		// From here and on the request CondVar of our listener has woken up

		// We create a peer to unite with our listener
		Fid_t peerID = sys_Socket(cb->port);

		if (peerID == NOFILE)
			return -1;

		// We grab the FCB for our new socket
		FCB *peerFCB = get_fcb(peerID);
		// Then we grab the socket of our new FCB
		socketCB *peer = peerFCB->streamobj;

		// We pop the first node from the request list of socket "l"
		rlnode *requestNode = rlist_pop_front(&(l->listener.request_queue));
		// We grab the qNode type Struct from the rlNode
		qNode *reqNode = requestNode->obj;
		// Grab the peer from it
		socketCB *reqPeer = reqNode->reqSock;
		// And its FID to properly connect the pipes
		Fid_t reqPeerID = reqNode->fid;

		if (reqPeer == NULL || peer == NULL) // If any of the peers don't exist if fails
			return -1;

		// After creating the 2 peers, we will create the 2 pipes

		// Pipe 1
		pipe_cb *pipe1 = (pipe_cb *)xmalloc(sizeof(pipe_cb));
		pipe1->pit.read = reqPeerID;
		pipe1->pit.write = peerID;
		pipe1->r_position = 0;
		pipe1->w_position = 0;
		pipe1->has_space = COND_INIT;
		pipe1->has_data = COND_INIT;
		// Pipe 2
		pipe_cb *pipe2 = (pipe_cb *)xmalloc(sizeof(pipe_cb));
		pipe2->pit.read = peerID;
		pipe2->pit.write = reqPeerID;
		pipe2->r_position = 0;
		pipe2->w_position = 0;
		pipe2->has_space = COND_INIT;
		pipe2->has_data = COND_INIT;

		// As long as the pipes are fine, we create the connection
		if (pipe1 != NULL && pipe2 != NULL)
		{
			peer->type = PEER;
			peer->peer.readPipe = pipe2;
			peer->peer.writePipe = pipe1;

			reqPeer->type = PEER;
			reqPeer->peer.readPipe = pipe1;
			reqPeer->peer.writePipe = pipe2;
		}

		// We honor the connection request
		reqNode->admitted = 1;

		// We signal the new condition to the waiter
		kernel_signal(&(reqNode->cv));

		return peerID;
	}
	return -1;
}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	// Grab the FCB of the FID given
	FCB *fcb = get_fcb(sock);
	int timedOut; // Variable so we don't wait for the connection endlessly

	if (fcb == NULL) // FCB doesn't exist
		return -1;
	if (fcb->streamfunc != &socket_file_ops) // Not connected to the socket functions
		return -1;
	if (port <= NOPORT || port > MAX_PORT) // Port out of bounds
		return -1;

	// Grab the socket of the FCB
	socketCB *peer = fcb->streamobj;
	// Get the Lsocket of the port
	socketCB *listener = PORT_MAP[port];

	if (peer->type != UNBOUND) // Socket already in use
		return -1;
	if (listener == NULL) // Listener doesn't exist
		return -1;
	if (listener->type != LISTENER) // Socket is there but it's not a listener
		return -1;

	// Dynamically allocating reqNode
	qNode *node = (qNode *)xmalloc(sizeof(qNode));
	// Socket of reqNode points to the peer
	node->reqSock = peer;
	// We use the fid of the requester to connect the pipes
	node->fid = sock;
	// Initialize the queue
	rlnode_init(&node->node, node);
	// Listener not yet ready so we set it to 0
	node->admitted = 0;
	// Initializing the condition variable
	node->cv = COND_INIT;
	// Place the request node to the listener's list
	rlist_push_back(&(PORT_MAP[port]->listener.request_queue), &node->node);

	// Wake up the listener to make the connection
	kernel_broadcast(&(PORT_MAP[port]->listener.req));

	// While there's no response
	while (node->admitted == 0)
	{
		// We make sure there's a timeout at some point
		timedOut = kernel_timedwait(&node->cv, SCHED_PIPE, timeout);
		if (!timedOut) // Timeout occured
			return -1;
	}

	// Not yet needed
	node = NULL;
	return 0;
}

int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB *fcb = get_fcb(sock);
	if (fcb == NULL) // Bad FCB
		return -1;
	if (fcb->streamfunc != &socket_file_ops) // Wrong funcs
		return -1;
	if (how < 1 || how > 3) // Bad mode
		return -1;

	socketCB *cb = fcb->streamobj;

	if (cb != NULL && cb->type == PEER)
	{ // If the socket exists and there's a connection going on
		int r, w;
		switch (how) // Case on shutdown mode
		{
		case SHUTDOWN_READ: // We use pipe_reader_close() to close socket's reader_pipe
			return pipe_reader_close(cb->peer.readPipe);
			break;
		case SHUTDOWN_WRITE: // We use pipe_writer_close() to close socket’s writer_pipe
			return pipe_writer_close(cb->peer.writePipe);
			break;
		case SHUTDOWN_BOTH: // Close both of them
			r = pipe_reader_close(cb->peer.readPipe);
			w = pipe_writer_close(cb->peer.writePipe);
			if ((r + w) == 0) // Check if it was done properly
				return 0;
			else
				return -1;
			break;
		default: // Wrong value was inserted somehow
			fprintf(stderr, "%s\n", "Unknown shutdown_mode.\n");
			return -1;
		}
	}
	return -1;
}
