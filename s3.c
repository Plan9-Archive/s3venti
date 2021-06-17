/* Copyright (c) 2008 Richard Bilson */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <httpd.h>
#include <thread.h>
#include "aws.h"

#define USE_MEM

void
usage(void)
{
	fprint(2, "usage:\n");
	fprint(2, "	s3 [-v] create bucket\n");
	fprint(2, "	s3 [-v] read bucket key\n");
	fprint(2, "	s3 [-v] write bucket key file\n");
	fprint(2, "	s3 [-v] rm bucket [key]\n");
	fprint(2, "	s3 [-v] ls [bucket] [pattern]\n");
	threadexitsall("usage");
}

char*
s3create(int argc, char **argv)
{
	S3Con c;
	S3Req r;
	S3Resp resp;
	char *err;
	if(argc != 1)
		usage();
	char *bucket = argv[0];
	if(err = S3open(&c, nil, nil))
		return err;

	memset(&r, 0, sizeof(r));
	r.cfd = -1;
	r.method = "PUT";
	r.resource = smprint("/%U", bucket);
	if(err = S3request(&c, &r, &resp))
		return err;
	if(err = S3responsefd(&resp, 1))
		return err;

	S3close(&c);

	return nil;
}

#ifdef XXX
char*
s3read(int argc, char **argv)
{
	S3Con c;
	S3Req r;
	S3Resp resp;
	char *err;
	memset(&r, 0, sizeof(r));

	if(argc != 2)
		usage();
	char *bucket = argv[0];
	char *key = argv[1];

	if(err = S3open(&c, nil, nil))
		return err;

	r.cfd = -1;
	r.method = "GET";
	r.resource = smprint("/%U/%U", bucket, key);
	if(err = S3request(&c, &r, &resp))
		return err;
	if(err = S3responsefd(&resp, 1))
		return err;

	S3close(&c);

	return nil;
}
#else
char*
s3read(int argc, char **argv)
{
	S3Con c;
	S3Req r;
	S3Resp resp;
	char *err;
	memset(&r, 0, sizeof(r));

	if(argc != 2)
		usage();
	char *bucket = argv[0];
	char *key = argv[1];

	if(err = S3open(&c, nil, nil))
		return err;

	r.cfd = -1;
	r.method = "GET";
	r.resource = smprint("/%U/%U", bucket, key);
	if(err = S3request(&c, &r, &resp))
		return err;
	while(1) {
		uchar buf[1024];
		long nread = S3response(&resp, buf, 1024);
		if(nread <= 0) break;
		write(1, buf, nread);
	}

	S3close(&c);

	return nil;
}
#endif

char*
s3write(int argc, char **argv)
{
	S3Con c;
	S3Req r;
	S3Resp resp;
	char *err;
	Dir *d;
	memset(&r, 0, sizeof(r));

	if(argc != 3)
		usage();
	char *bucket = argv[0];
	char *key = argv[1];
	char *file = argv[2];
	if((r.cfd = open(file, OREAD)) == -1)
		return "can't open input file for reading";
	if((d = dirfstat(r.cfd)) == nil)
		return "can't stat input file";

	if(err = S3open(&c, nil, nil))
		return err;

	r.clen = d->length;
#ifdef USE_MEM
	r.content = malloc(d->length);
	int n, len = r.clen;
	uchar *p = r.content;
	while(len > 0) {
		n = read(r.cfd, p, len);
		if(n < 0)
			return "read error";
		len -= n;
		p += n;
	}
	close(r.cfd);
	r.cfd = -1;
#endif
	r.method = "PUT";
	r.resource = smprint("/%U/%U", bucket, key);
	if(err = S3request(&c, &r, &resp))
		return err;
	if(err = S3responsefd(&resp, 1))
		return err;

	S3close(&c);

	return nil;
}

char *
dorm(S3Con *c, char *rsrc)
{
	S3Req r;
	S3Resp resp;
	char *err;
	memset(&r, 0, sizeof(r));
	r.cfd = -1;
	r.method = "DELETE";
	r.resource = rsrc;
	if(err = S3request(c, &r, &resp))
		return err;
	if(err = S3responsefd(&resp, 1))
		return err;
	return nil;
}

char*
s3rm(int argc, char **argv)
{
	S3Con c;
	char *err, *bucket, *rsrc;
	int i;
	if(argc < 1)
		usage();
	bucket = argv[0];

	if(err = S3open(&c, nil, nil))
		return err;

	if(argc == 1) {
		rsrc = smprint("/%U", bucket);
		if(err = dorm(&c, rsrc))
			return err;
	} else for(i = 1; i < argc; i++) {
		rsrc = smprint("/%U/%U", bucket, argv[i]);
		if(err = dorm(&c, rsrc))
			return err;
	}

	S3close(&c);

	return nil;
}

char*
s3ls(int argc, char **argv)
{
	S3Con c;
	S3Req r;
	S3Resp resp;
	char *err, *bucket = "", *prefix = nil;
	if(argc > 2)
		usage();
	if(argc > 0)
		bucket = argv[0];
	if(argc == 2)
		prefix = argv[1];

	if(err = S3open(&c, nil, nil))
		return err;

	memset(&r, 0, sizeof(r));
	r.cfd = -1;
	r.method = "GET";
	if(prefix)
		r.resource = smprint("/%U?prefix=%U", bucket, prefix);
	else
		r.resource = smprint("/%U", bucket);
	if(err = S3request(&c, &r, &resp))
		return err;
	if(err = S3responsefd(&resp, 1))
		return err;

	S3close(&c);

	return nil;
}

struct {
	char *cmd;
	char *(*fn)(int,char**);
} cmdtable[] = {
	{ "create", s3create },
	{ "read", s3read },
	{ "write", s3write },
	{ "rm", s3rm },
	{ "ls", s3ls },
};

void
threadmain(int argc, char **argv)
{
	char *(*fn)(int,char**) = nil;
	char *result;
	int i;
	fmtinstall('Z', hdatefmt);
	fmtinstall('H', httpfmt);
	fmtinstall('U', hurlfmt);
	fmtinstall('[', encodefmt);

	ARGBEGIN{
	case 'v':
		chattyS3++;
		break;
	default:
		usage();
	}ARGEND

	if(argc <1)
		usage();

	for(i = 0; i < nelem(cmdtable); i++)
		if(strcmp(argv[0], cmdtable[i].cmd) == 0)
			fn = cmdtable[i].fn;

	if(!fn)
		usage();

	result = fn(argc-1,&argv[1]);
	if(result)
		fprint(2, "%s\n", result);
	threadexitsall(result);
}
