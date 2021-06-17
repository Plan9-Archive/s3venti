/* Copyright (c) 2008 Richard Bilson */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <libsec.h>
//#include <mp.h>
#include <httpd.h>
#include "aws.h"

int chattyS3 = 0;

static int
Bprintv(Biobuf *b, char *fmt, ...)
{
	char buf[4*1024];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	if(chattyS3)
		fprint(2, "%s", buf);
	return Bprint(b, "%s", buf);
}

char *estrdup(char *s);

static int
authhdr(Biobuf *bout, char *op, char *host, char *contentmd5, char *contenttype, char *canonrsrc)
{
	uchar digest[SHA1dlen];
	char *datestr = smprint("%Z", time(0));
	char *query = strchr(canonrsrc, '?');
	static char *lasthost;
	static char *keyid;
	static char *key;
	static int keylen;

	if(!lasthost || strcmp(host, lasthost) != 0){
		UserPasswd *p = auth_getuserpasswd(nil, "proto=pass service=aws role=client server=%s", host);
		if(!p)
			sysfatal("authentication failed: %r");
		if(lasthost){
			free(lasthost);
			free(keyid);
			free(key);
		}
		lasthost = estrdup(host);
		keyid = estrdup(p->user);
		key = estrdup(p->passwd);
		keylen = strlen(key);
	}
	DigestState *s = hmac_sha1((uchar*)op, strlen(op), (uchar*)key, keylen, nil, nil);
	hmac_sha1((uchar*)"\n", 1, (uchar*)key, keylen, nil, s);
	if(contentmd5)
		hmac_sha1((uchar*)contentmd5, strlen(contentmd5), (uchar*)key, keylen, nil, s);
	hmac_sha1((uchar*)"\n", 1, (uchar*)key, keylen, nil, s);
	if(contenttype)
		hmac_sha1((uchar*)contenttype, strlen(contenttype), (uchar*)key, keylen, nil, s);
	hmac_sha1((uchar*)"\n", 1, (uchar*)key, keylen, nil, s);
	hmac_sha1((uchar*)datestr, strlen(datestr), (uchar*)key, keylen, nil, s);
	hmac_sha1((uchar*)"\n", 1, (uchar*)key, keylen, nil, s);
	if(query)
		hmac_sha1((uchar*)canonrsrc, query - canonrsrc, (uchar*)key, keylen, digest, s);
	else
		hmac_sha1((uchar*)canonrsrc, strlen(canonrsrc), (uchar*)key, keylen, digest, s);

	if(Bprintv(bout, "%s %s HTTP/1.1\r\n", op, canonrsrc) < 0)
		return 0;
	if(contentmd5 && contentmd5[0])
		if(Bprintv(bout, "Content-MD5: %s\r\n", contentmd5) < 0)
			return 0;
	if(contenttype && contenttype[0])
		if(Bprintv(bout, "Content-Type: %s\r\n", contenttype) < 0)
			return 0;
	if(Bprintv(bout, "Date: %s\r\n", datestr) < 0)
		return 0;
	if(Bprintv(bout, "Authorization: AWS %s:%.*[\r\n", keyid, SHA1dlen, digest) < 0)
		return 0;
	return 1;
}

char *
S3open(S3Con *c, char *host, char *port)
{
	if(host)
		c->host = host;
	else
		c->host = "s3.amazonaws.com";
	if(port)
		c->port = port;
	else
		c->port = "http";
	c->fd = dial(netmkaddr(c->host, "net", c->port), 0, 0, 0);
	if(c->fd < 0)
		return("can't contact s3 host");
	return nil;
}

char *
S3close(S3Con *c)
{
	close(c->fd);
	return nil;
}

char *
S3reopen(S3Con *c)
{
	S3close(c);
	c->fd = dial(netmkaddr(c->host, "net", c->port), 0, 0, 0);
	if(c->fd < 0)
		return("can't contact s3 host");
	return nil;
}

static int
docopy(Biobuf *bin, Biobuf *bout, vlong clen)
{
	while(clen < 0 || clen-- > 0) {
		int c = Bgetc(bin);
		if(c < 0)
			break;
		if(Bputc(bout, c) < 0)
			return 0;
	}
	return 1;
}

static char *
doheader(Biobuf *bin, int *chunk, vlong *clen)
{
	while(1) {
		int dealloc = 0;
		char *line = Brdline(bin, '\n');
		int len = Blinelen(bin);
		if(line == nil) {
			line = Brdstr(bin, '\n', 0);
			len = strlen(line);
			dealloc = 1;
		}
		if(line == nil)
			return "premature end of headers";
		if(chattyS3)
			write(2, line, len);

		if(len == 1 || (len == 2 && line[0] == '\r'))
			break;
		else if(cistrncmp(line, "content-length: ", strlen("content-length: ")) == 0)
			*clen = atoll(line+strlen("content-length: "));
		else if(cistrncmp(line, "transfer-encoding: chunked", strlen("transfer-encoding: chunked")) == 0)
			*chunk = 1;

		if(dealloc)
			free(line);
	}
	return nil;
}

char *
S3request(S3Con *c, S3Req *r, S3Resp *resp)
{
	Biobuf bout;
	int rcode;
	char *err;
	resp->clen = -1;
	resp->chunk = 0;
	Binit(&resp->bin, c->fd, OREAD);
	Binit(&bout, c->fd, OWRITE);
	if(!authhdr(&bout, r->method, c->host, r->cmd5, r->ctype, r->resource))
		return "S3request: send failure1";
	if(Bprintv(&bout, "Host: %s\r\n", c->host) < 0)
		return "S3request: send failure2";
	if(Bprintv(&bout, "Content-Length: %d\r\n", r->clen) < 0)
		return "S3request: send failure3";
	if(Bprintv(&bout, "\r\n") < 0)
		return "S3request: send failure4";
	if(r->content) {
		int len = r->clen;
		while(len > 0) {
			int n = Bwrite(&bout, r->content, len);
			if(n<0)
				return "S3request: send failure5";
			len -= n;
		}
	} else if(r->cfd >= 0) {
		int i;
		//fprint(2, "sending content from fd %d\n", r->cfd);
		Biobuf bcontent;
		Binit(&bcontent, r->cfd, OREAD);
		for(i = 0; i < r->clen; i++) {
			int c = Bgetc(&bcontent);
			if(c < 0)
				break;
			if(Bputc(&bout, c) < 0)
				return "S3request: send failure6";
		}
		Bterm(&bcontent);
		//fprint(2, "sent %d bytes\n", i);
	}
	if(Bterm(&bout))
		return("S3request: send failure7");
	resp->httpver = Brdstr(&resp->bin, '\n', 1);
	if(!resp->httpver)
		return "S3request: no response";
	if(chattyS3)
		fprint(2, "%s\n", resp->httpver);
	resp->result = strchr(resp->httpver, ' ');
	if(!resp->result)
		return "S3request: malformed response";
	resp->result++;
	rcode = atoi(resp->result);
	if(rcode == 204)
		resp->clen = 0;
	err = doheader(&resp->bin, &resp->chunk, &resp->clen);
	if(err)
		return err;
	return nil;
}

char *
nextchunk(Biobuf *bin, vlong *clen)
{
	char *p, *line = Brdline(bin, '\n');
	if(!line)
		return "malformed chunk from server";
	*clen = 0;
	for(p = line; *p != '\r'; p++)
		if(*p >= '0' && *p <= '9')
			*clen = *clen * 16 + *p - '0';
		else if(*p >= 'a' && *p <= 'f')
			*clen = *clen * 16 + *p - 'a' + 10;
		else if(*p >= 'A' && *p <= 'F')
			*clen = *clen * 16 + *p - 'A' + 10;
		else
			return "malformed chunk from server";
	return nil;
}

long
S3response(S3Resp *resp, uchar buf[], long size)
{
	long nread;

	if(size <= 0)
		return -1;

	if(resp->chunk) {
		if(resp->clen == 0)
			Brdline(&resp->bin, '\n');
		if(resp->clen == -1) {
			//fprint(2, "begin chunked transfer encoding\n");
			resp->clen = 0;
		}
		if(resp->clen == 0) {
			if(nextchunk(&resp->bin, &resp->clen)) {
				nread = -1;
				goto cleanup;
			}
			if(resp->clen == 0) {
				if(doheader(&resp->bin, &resp->chunk, &resp->clen)) {
					nread = -1;
					goto cleanup;
				}
				return 0;
			}
		}
	}
	//fprint(2, "copying chunk of %lld bytes into space for %ld bytes\n", resp->clen, size);
	nread = Bread(&resp->bin, buf, resp->clen >= size ? size : resp->clen);
	resp->clen -= nread;
	if(resp->clen == 0)
cleanup:
	if(resp->httpver) {
		free(resp->httpver);
		resp->httpver = nil;
	}
	return nread;
}

char*
S3responsefd(S3Resp *resp, int fd)
{
	char *err = nil;
	Biobuf bprint;
	Binit(&bprint, fd, OWRITE);
	if(resp->chunk) {
		//fprint(2, "begin chunked transfer encoding\n");
		while(1) {
			if(err = nextchunk(&resp->bin, &resp->clen))
				goto cleanup;
			//fprint(2, "copying chunk of %lld bytes\n", resp->clen);
			if(resp->clen == 0)
				break;
			docopy(&resp->bin, &bprint, resp->clen);
			Brdline(&resp->bin, '\n');
		}
		err = doheader(&resp->bin, &resp->chunk, &resp->clen);
		if(err) goto cleanup;;
	}else if(resp->clen >= 0){
		if(!docopy(&resp->bin, &bprint, resp->clen))
			err = "S3responsefd: receive failed";
	}else{
		if(!docopy(&resp->bin, &bprint, -1))
			err = "S3responsefd: receive failed";
	}
cleanup:
	Bterm(&bprint);
	Bterm(&resp->bin);
	if(resp->httpver) {
		free(resp->httpver);
		resp->httpver = nil;
	}
	return err;
}

char*
S3responsediscard(S3Resp *resp)
{
	int fd = open("/dev/null", OWRITE);
	char *err = S3responsefd(resp, fd);
	close(fd);
	return err;
}
