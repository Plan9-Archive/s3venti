/* Copyright (c) 2008 Richard Bilson */
#include "stdinc.h"
#include "dat.h"
#include "fns.h"
#include "whack.h"

static char*
doS3write(S3Con *c, VtReq *r)
{
	S3Vhdr hdr;
	S3Req req;
	S3Resp resp;
	char *err = nil;
	uchar *block;
	int wlen;

	hdr.blocktype = r->tx.blocktype;
	hdr.size = packetsize(r->tx.data);

	packetsha1(r->tx.data, r->rx.score);

	memset(&req, 0, sizeof(req));
	req.method = "PUT";
	req.resource = smprint("/%U/%V", config.bucket, r->rx.score);
	req.content = vtmalloc(HdrSize + AESbsize + hdr.size);

	/* compress */
	block = vtmalloc(hdr.size);
	packetcopy(r->tx.data, block, 0, hdr.size);
	wlen = whackblock(req.content+HdrSize+AESbsize, block, hdr.size);
	if(wlen > 0 && wlen < hdr.size) {
		//fprint(2, "dos3write compress %V\n", r->rx.score);
		hdr.codec = BlockECompress;
		hdr.csize = wlen;
	} else {
		if(wlen > hdr.size) {
			fprint(2, "whack error: dsize=%d size=%d\n", wlen, hdr.size);
			abort();
		}
		hdr.codec = BlockENone;
		hdr.csize = hdr.size;
		memmove(req.content+HdrSize+AESbsize, block, hdr.size);
	}
	req.clen = HdrSize + AESbsize + hdr.csize;

	/* encrypt */
	if(config.key){
		encryptblock(req.content+HdrSize, hdr.csize, config.key);
	}

	/* write */
	vhdrpack(&hdr, req.content);
	//sha1(req.content+HdrSize+AESbsize, hdr.csize, r->rx.score, nil);
	//req.resource = smprint("/rcbilson-ventitest/%V", r->rx.score);
	if(err = S3request(c, &req, &resp)) {
		err = vtstrdup(err);
		goto cleanup;
	}
	if(resp.result[0] != '2'){
		err = vtstrdup(resp.result);
		S3responsediscard(&resp);
		goto cleanup;
	}
cleanup:
	vtfree(req.content);
	vtfree(block);
	free(req.resource);
	return err;
}

static char*
doS3read(S3Con *c, VtReq *r)
{
	S3Vhdr hdr;
	S3Req req;
	S3Resp resp;
	char *err = nil;
	uchar rawhdr[HdrSize], sha1[VtScoreSize], *buf, *ubuf;
	long len;
	Unwhack uw;
	int nunc;

	memset(&req, 0, sizeof(req));
	req.method = "GET";
	req.resource = smprint("/%U/%V", config.bucket, r->tx.score);
	if(err = S3request(c, &req, &resp)) {
		err = vtstrdup(err);
		goto cleanup;
	}
	if(memcmp(resp.result, "404", 3) == 0) {
		r->rx.error = vtstrdup("no such block");
		r->rx.msgtype = VtRerror;
		goto cleanup2;
	}
	if(resp.result[0] != '2'){
		err = vtstrdup(resp.result);
		goto cleanup2;
	}
	len = 0;
	do {
		long n = S3response(&resp, rawhdr, HdrSize - len);
		if(n < 0) {
			err = vtstrdup("EOF on response");
			goto cleanup;
		}
		len += n;
	} while( len < HdrSize );
	vhdrunpack(&hdr, rawhdr);
	if(hdr.blocktype != r->tx.blocktype) {
		r->rx.error = vtstrdup("type mismatch");
		r->rx.msgtype = VtRerror;
		goto cleanup;
	}
	if(hdr.size > r->tx.count) {
		r->rx.error = vtstrdup("too big");
		r->rx.msgtype = VtRerror;
		goto cleanup;
	}
	buf = vtmalloc(AESbsize + hdr.csize);
	len = 0;
	do {
		long n = S3response(&resp, buf, AESbsize + hdr.csize - len);
		if(n <= 0) {
			err = vtstrdup("EOF on response");
			vtfree(buf);
			goto cleanup;
		}
		len += n;
	} while(len < AESbsize + hdr.csize);

	/* decrypt */
	if(config.key)
		decryptblock(buf, hdr.csize, config.key);

	/* decompress */
	ubuf = vtmalloc(hdr.size);
	switch(hdr.codec){
	case BlockECompress:
		//fprint(2, "dos3read decompress %V\n", r->tx.score);
		unwhackinit(&uw);
		nunc = unwhack(&uw, ubuf, hdr.size, buf+AESbsize, hdr.csize);
		if(nunc != hdr.size){
			if(nunc < 0) {
				char *msg = smprint("decompression failed: %s", uw.err);
				r->rx.error = vtstrdup(msg);
				r->rx.msgtype = VtRerror;
				free(msg);
			} else {
				char *msg = smprint("decompression gave partial block: %d/%d\n", nunc, hdr.size);
				r->rx.error = vtstrdup(msg);
				r->rx.msgtype = VtRerror;
				free(msg);
			}
			vtfree(buf);
			vtfree(ubuf);
			goto cleanup;
		}
		break;
	case BlockENone:
		if(hdr.csize != hdr.size){
			char *msg = smprint("loading clump: bad uncompressed size for uncompressed block %V", r->tx.score);
			r->rx.error = vtstrdup(msg);
			r->rx.msgtype = VtRerror;
			free(msg);
			vtfree(buf);
			vtfree(ubuf);
			goto cleanup;
		}
		memmove(ubuf, buf+AESbsize, hdr.size);
		break;
	default:
		r->rx.error = vtstrdup("unknown encoding in loadlump");
		r->rx.msgtype = VtRerror;
		vtfree(buf);
		vtfree(ubuf);
		goto cleanup;
	}
	vtfree(buf);
	r->rx.data = packetalloc();
	packetappend(r->rx.data, ubuf, hdr.size);
	packetsha1(r->rx.data, sha1);
	if(memcmp(sha1, r->tx.score, VtScoreSize) != 0) {
		r->rx.error = vtstrdup("score mismatch");
		r->rx.msgtype = VtRerror;
		packetfree(r->rx.data);
		vtfree(ubuf);
		goto cleanup2;
	}
	vtfree(ubuf);
	goto cleanup;
cleanup2:
	S3responsediscard(&resp);
cleanup:
	free(req.resource);
	return err;
}

int nretries = 2;

static void
S3retry(S3Con *c, VtReq *r, char *(*op)(S3Con *c, VtReq *r), int nretries)
{
	char *err;
	int ntries = 0;
	while(1) {
		err = op(c, r);
		if(!err || ntries++ >= nretries ) break;
		vtfree(err);
		//fprint(2, "RETRY\n");
		S3reopen(c);
	}
	if(err) {
		//fprint(2, "RETRIES FAILED\n");
		r->rx.error = err;
		r->rx.msgtype = VtRerror;
	}
}

void
S3writeblock(S3Con *c, VtReq *r)
{
	S3retry(c, r, doS3write, nretries);
}

void
S3readblock(S3Con *c, VtReq *r)
{
	S3retry(c, r, doS3read, nretries);
}

