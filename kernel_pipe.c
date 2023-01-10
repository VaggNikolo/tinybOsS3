#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_sched.h"
#include "kernel_cc.h"
#include "kernel_dev.h"
#include "kernel_streams.h"

file_ops reader_file_ops = {
	.Open = NULL,
	.Read = pipe_read,
	.Write = nothingConst,
	.Close = pipe_reader_close};

file_ops writer_file_ops = {
	.Open = NULL,
	.Read = nothing,
	.Write = pipe_write,
	.Close = pipe_writer_close};

int sys_Pipe(pipe_t *pipe)
{
	// We create local variables for FIDT, FCB and PipeCB
	Fid_t fid[2];
	FCB *fcb[2];
	pipe_cb *cb = (pipe_cb *)xmalloc(sizeof(pipe_cb));

	// We reserve 2 FCBs
	int reservedFCB = FCB_reserve(2, fid, fcb);
	// If it fails to reserve, it fails
	if (!reservedFCB)
	{
		return -1;
	}
	// Point the reader and writer of the pipe to the FIDs we created
	pipe->read = fid[0];
	pipe->write = fid[1];

	// Initialize the PipeCB content
	cb->pit = *pipe;
	cb->r_position = 0;
	cb->w_position = 0;
	cb->has_space = COND_INIT;
	cb->has_data = COND_INIT;

	// Common PipeCB for the read/write FCBs
	fcb[0]->streamobj = cb;
	fcb[1]->streamobj = cb;
	// But different file operations
	fcb[0]->streamfunc = &reader_file_ops;
	fcb[1]->streamfunc = &writer_file_ops;

	return 0;
}

int pipe_write(void *pipecb_t, const char *buf, unsigned int size)
{
	pipe_cb *pipeCB = (pipe_cb *)pipecb_t;

	// If the pipe isn't valid, it fails
	if (pipeCB == NULL)
	{
		return -1;
	}

	// If any end of the pipe is closed, it also fails
	if (pipeCB->pit.write == NOFILE || pipeCB->pit.read == NOFILE)
	{
		return -1;
	}

	int place = 0;

	// If pointer w_position reaches pointer r_position change CondVar has_space to sleep
	while ((pipeCB->w_position + 1) % PIPE_BUFFER_SIZE == pipeCB->r_position && pipeCB->pit.read != NOFILE)
	{
		kernel_wait(&(pipeCB->has_space), SCHED_PIPE);
	}

	// Here we write to the pipe
	while (place != size && place < PIPE_BUFFER_SIZE)
	{
		// If the writer tries to overwrite data that hasn't been read yet, we break out of the loop
		if ((pipeCB->w_position + 1) % PIPE_BUFFER_SIZE == pipeCB->r_position && pipeCB->pit.read != NOFILE)
			break;
		/*We write the character of the buffer we want to write in place "place"
		to the place where pointer w_position is on our pipe buffer*/
		pipeCB->buffer[pipeCB->w_position] = buf[place];
		// Increase the pointer w_position
		pipeCB->w_position = (pipeCB->w_position + 1) % PIPE_BUFFER_SIZE;
		// Increase our local counter
		place++;
	}
	// Let the others know we wrote something
	kernel_broadcast(&(pipeCB->has_data));
	return place;
}

int nothing(void *pipecb_t, char *buf, unsigned int size)
{
	return -1;
}

int nothingConst(void *pipecb_t, const char *buf, unsigned int size)
{
	return -1;
}

int pipe_read(void *pipecb_t, char *buf, unsigned int size)
{
	pipe_cb *pipeCB = (pipe_cb *)pipecb_t;

	// If the pipe isn't valid, it fails
	if (pipeCB == NULL)
	{
		return -1;
	}

	// If reader end of pipe is closed, it fails
	if (pipeCB->pit.read == NOFILE)
	{
		return -1;
	}

	int place = 0;

	// If the writer end is closed, read till the end of the data
	if (pipeCB->pit.write == NOFILE)
	{
		while (pipeCB->r_position < pipeCB->w_position)
		{
			if (place == size)
				return place;
			buf[place] = pipeCB->buffer[pipeCB->r_position];
			pipeCB->r_position = (pipeCB->r_position + 1) % PIPE_BUFFER_SIZE;
			place++;
		}
		return place;
	}

	// If pointer r_position reaches the place of pointer w_position change CondVar has_data to sleep
	while (pipeCB->r_position == pipeCB->w_position && pipeCB->pit.write != NOFILE)
	{
		kernel_wait(&(pipeCB->has_data), SCHED_PIPE);
	}

	// Here we read the pipe
	while (place != size && place < PIPE_BUFFER_SIZE)
	{
		// If the reader tries to read something not yet written, we break out of the loop
		if (pipeCB->r_position == pipeCB->w_position)
			break;
		buf[place] = pipeCB->buffer[pipeCB->r_position];
		pipeCB->r_position = (pipeCB->r_position + 1) % PIPE_BUFFER_SIZE;
		place++;
	}
	kernel_broadcast(&(pipeCB->has_space));
	return place;
}

int pipe_writer_close(void *_pipecb)
{
	pipe_cb *pipeCB = (pipe_cb *)_pipecb;

	if (pipeCB != NULL)
	{
		pipeCB->pit.write = NOFILE;

		if (pipeCB->pit.read == NOFILE)
		{
			pipeCB = NULL;
			free(pipeCB);
		}
		else
		{
			kernel_broadcast(&(pipeCB->has_data));
		}
		return 0;
	}
	return -1;
}

int pipe_reader_close(void *_pipecb)
{
	pipe_cb *pipeCB = (pipe_cb *)_pipecb;

	if (pipeCB != NULL)
	{
		pipeCB->pit.read = NOFILE;

		if (pipeCB->pit.write == NOFILE)
		{
			pipeCB = NULL;
			free(pipeCB);
		}
		else
		{
			kernel_broadcast(&(pipeCB->has_space));
		}
		return 0;
	}

	return -1;
}
