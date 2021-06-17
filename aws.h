/* Copyright (c) 2008 Richard Bilson */
typedef struct S3Con S3Con;
typedef struct S3Req S3Req;
typedef struct S3Resp S3Resp;

struct S3Con {
	int fd;
	char *host;
	char *port;
};

struct S3Req {
	char *method;
	char *resource;
	int cfd;
	uchar *content;
	int clen;
	char *ctype;
	char *cmd5;
};

struct S3Resp {
	char *result;
	char *httpver;

	Biobuf bin;
	int chunk;
	vlong clen;
};

char *S3close(S3Con *c);
char *S3open(S3Con *c, char *host, char *port);
char *S3reopen(S3Con *c);
char *S3request(S3Con *c, S3Req *r, S3Resp *resp);
long S3response(S3Resp *resp, uchar buf[], long size);
char *S3responsefd(S3Resp *resp, int fd);
char* S3responsediscard(S3Resp *resp);

extern int chattyS3;
