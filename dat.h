#include "ventidat.h"

typedef struct S3Vhdr		S3Vhdr;
typedef struct S3Vconfig	S3Vconfig;

enum {
	HdrSize = 8*2 + 16*2,
	BlockENone = 0, BlockECompress = 1,
};

/*
 * results of parsing and initializing a config file
 */
struct S3Vconfig
{
	u32int	mem;
	int	queuewrites;
	char*	vaddr;
	uchar*	key;
	char*	bucket;
	char*	s3host;
	char*	s3port;
	char*	dcache;
	int	logfd;
};

/*
 * the header of a block stored to S3
 */
struct S3Vhdr {
	uchar blocktype;
	uchar codec;
	ushort size;
	ushort csize;
};

extern S3Vconfig config;
extern char magic[];
extern int magiclen;
