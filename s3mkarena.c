#include "stdinc.h"
#include "dat.h"
#include "fns.h"

void
usage(void)
{
	fprint(2, "usage: s3mkarena [-v] [config]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *bucket, *configfile, *err;
	S3Vconfig config;
	S3Con c;
	S3Req req;
	S3Resp resp;

	fmtinstall('Z', hdatefmt);
	fmtinstall('H', httpfmt);
	fmtinstall('U', hurlfmt);
	fmtinstall('[', encodefmt);

	bucket = nil;
	configfile = nil;

	ARGBEGIN{
	case 'v':
		chattyS3++;
		break;
	case 'b':
		bucket = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(!argv)
		configfile = *argv;
	else
		configfile = "s3venti.conf";

	if(s3vconfig(configfile, &config) < 0)
		sysfatal("can't process configuration file");

	if(bucket)
		config.bucket = bucket;
	if(!config.bucket)
		sysfatal("no bucket specified");

	if(err = S3open(&c, config.s3host, config.s3port))
		sysfatal("s3mkarena %s\n", err);

	memset(&req, 0, sizeof(req));
	req.cfd = -1;
	req.method = "PUT";
	req.resource = smprint("/%U", config.bucket);
	if(err = S3request(&c, &req, &resp)) {
		sysfatal( "s3mkarena: create bucket failed: %s", err );
	}
	if(resp.result[0] != '2'){
		fprint(2, "s3mkarena: s3 server reported %s\n", resp.result );
		S3responsefd(&resp, 2);
		threadexitsall("request failed");
	}

	req.resource = smprint("/%U/magic", config.bucket);
	req.clen = magiclen+AESbsize;
	req.content = vtmalloc(req.clen);
	memcpy(req.content+AESbsize, magic, magiclen);
	if(config.key){
		encryptblock(req.content, magiclen, config.key);
	}

	if(err = S3request(&c, &req, &resp)) {
		sysfatal( "magic number failed: %s", err );
	}
	if(resp.result[0] != '2'){
		fprint(2, "s3mkarena: s3 server reported %s\n", resp.result );
		S3responsefd(&resp, 2);
		threadexitsall("request failed");
	}

	S3close(&c);

	threadexitsall(nil);
}
