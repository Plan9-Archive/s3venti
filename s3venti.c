/* Copyright (c) 2008 Richard Bilson */
/* Portions copyright (c) 2004 Russ Cox */
#include "stdinc.h"
#include "dat.h"
#include "fns.h"

int verbose;

void
usage(void)
{
	fprint(2, "usage: s3/venti [-v] [-a address] [-b bucket] [-c configfile]\n");
	threadexitsall("usage");
}

static void
dowrite(S3Con *c, VtReq *r)
{
	S3writeblock(c, r);
	if(config.logfd >= 0)
		fprint(config.logfd, "write %V to s3\n", r->rx.score);
}

static Lump*
lcache(VtReq *r)
{
	u32int n;
	Lump *u;

	if(scorecmp(r->tx.score, zeroscore) == 0) {
		r->rx.data = packetalloc();
		return nil;
	}
	u = lookuplump(r->tx.score, r->tx.blocktype);
	if(u->data != nil){
		if(config.logfd >= 0)
			fprint(config.logfd, "read %V found in cache\n", r->tx.score);
		n = packetsize(u->data);
		if(n > r->tx.count){
			r->rx.error = vtstrdup("too big");;
			r->rx.msgtype = VtRerror;
			putlump(u);
		}
		r->rx.data = packetdup(u->data, 0, n);
		putlump(u);
		return nil;
	}
	return u;
}

static void
doread(S3Con *c, VtReq *r)
{
	Lump *u;

	u = lcache(r);
	if(!u)
		return;

	/* call dcache */

	if(config.logfd >= 0)
		fprint(config.logfd, "read %V from s3\n", r->tx.score);
	S3readblock(c, r);
	if(r->rx.msgtype != VtRerror)
		insertlump(u, packetdup(r->rx.data, 0, packetsize(r->rx.data)));
	putlump(u);
}

static void
checkmagic(S3Con *c)
{
	S3Req req;
	S3Resp resp;
	uchar block[AESbsize + magiclen];
	char *err;
	int len;

	memset(&req, 0, sizeof(req));
	req.method = "GET";
	req.resource = smprint("/%U/magic", config.bucket);
	if(err = S3request(c, &req, &resp)) {
		sysfatal("checkmagic request: %s", err);
	}
	if(memcmp(resp.result, "404", 3) == 0) {
		sysfatal("no magic found in %s; not an s3venti arena", config.bucket);
	}
	if(resp.result[0] != '2'){
		err = vtstrdup(resp.result);
		fprint(2, "s3 server returned %s", resp.result);
		S3responsefd(&resp, 2);
		threadexitsall("s3 error");
	}
	len = 0;
	do {
		long n = S3response(&resp, block, AESbsize + magiclen - len);
		if(n <= 0)
			sysfatal("checkmagic: EOF on response");
		len += n;
	} while(len < AESbsize + magiclen);
	
	if(config.key)
		decryptblock(block, magiclen, config.key);

	if(memcmp(block+AESbsize, magic, magiclen) != 0)
		sysfatal("checkmagic failed; bad passphrase?");
}

S3Vconfig config;

void
threadmain(int argc, char **argv)
{
	VtReq *r;
	VtSrv *srv;
	char *configfile, *address, *bucket, *logfile;
	S3Con c;

	fmtinstall('V', vtscorefmt);
	fmtinstall('F', vtfcallfmt);
	fmtinstall('Z', hdatefmt);
	fmtinstall('H', httpfmt);
	fmtinstall('U', hurlfmt);
	fmtinstall('[', encodefmt);
	
	address = nil;
	bucket = nil;
	configfile = "s3venti.conf";
	logfile = nil;

	ARGBEGIN{
	case 'v':
		verbose++;
		break;
	case 'a':
		address = EARGF(usage());
		break;
	case 'b':
		bucket = EARGF(usage());
		break;
	case 'c':
		configfile = EARGF(usage());
		break;
	case 'l':
		logfile = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(s3vconfig(configfile, &config) < 0)
		sysfatal("can't init server");

	if(bucket)
		config.bucket = bucket;
	if(!config.bucket)
		sysfatal("no bucket specified");

	if(logfile){
		config.logfd = create(logfile, OWRITE|OAPPEND, 600);
		if(config.logfd < 0)
			sysfatal("can't open log file %s for writing", logfile);
	}

	if(!address){
		if(config.vaddr)
			address = config.vaddr;
		else
			address = "tcp!*!venti";
	}

	if(config.mem == 0xffffffffUL)
		config.mem = 1 * 1024 * 1024;
	if(0) fprint(2, "initialize %d bytes of lump cache for %d lumps\n",
		config.mem, config.mem / (8 * 1024));
	initlumpcache(config.mem, config.mem / (8 * 1024));

	chattyS3 = verbose > 1;
	S3open(&c, config.s3host, config.s3port);
	checkmagic(&c);

	srv = vtlisten(address);
	if(srv == nil)
		sysfatal("vtlisten %s: %r", address);

	while((r = vtgetreq(srv)) != nil){
		r->rx.msgtype = r->tx.msgtype+1;
		if(verbose)
			fprint(2, "<- %F\n", &r->tx);
		switch(r->tx.msgtype){
		case VtTping:
			break;
		case VtTgoodbye:
			break;
		case VtTread:
			doread(&c, r);
			break;
		case VtTwrite:
			dowrite(&c, r);
			break;
		case VtTsync:
			break;
		}
		if(verbose)
			fprint(2, "-> %F\n", &r->rx);
		vtrespond(r);
	}

	S3close(&c);

	threadexitsall(nil);
}
