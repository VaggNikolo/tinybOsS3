#include "tinyos.h"
#include "util.h"
#include "kernel_dev.h"

/*
Every PCB has an FIDT list inside it, with the max number of FIDs being 16.
Each FID is connected with one and only FCB. The FCB is then connected to
the PipeCB, which can be common with other FCBs.

We need 2 FIDs and 2 FCBs, which are initialized to be pointing to the pipe
readers and writers.

We have a buffer in our pipe, containing our data, which is a bounded (cyclic) byte buffer.
Its max size is by choice 4096 Bytes, the default page size of Linux :)
*/

#define PIPE_BUFFER_SIZE 4096

typedef struct pipe_control_block
{
	pipe_t pit;
	char buffer[PIPE_BUFFER_SIZE];
	uint r_position;
	uint w_position;
	CondVar has_space;
	CondVar has_data;
} pipe_cb;

int nothing(void *this, char *buf, unsigned int size);

int nothingConst(void *this, const char *buf, unsigned int size);

int pipe_read(void *this, char *buf, unsigned int size);

int pipe_write(void *this, const char *buf, unsigned int size);

int pipe_reader_close(void *_pipecb);

int pipe_writer_close(void *_pipecb);
