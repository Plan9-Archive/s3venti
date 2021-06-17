/* Copyright (c) 2008 Richard Bilson */
#include "stdinc.h"
#include "dat.h"
#include "fns.h"

void
encryptblock(uchar *block, int size, uchar *key)
{
	AESstate aes;
#ifdef __linux__
	int fd = open("/dev/urandom", OREAD);
	read(fd, block, AESbsize);
	close(fd);
#else
	genrandom(block, AESbsize);
#endif
	setupAESstate(&aes, key, 16, block);
	aesCBCencrypt(block+AESbsize, size, &aes);
}

void
decryptblock(uchar *block, int size, uchar *key)
{
	AESstate aes;
	setupAESstate(&aes, key, 16, block);
	aesCBCdecrypt(block+AESbsize, size, &aes);
}
