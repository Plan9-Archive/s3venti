#include "stdinc.h"
#include "dat.h"
#include "fns.h"

static int
numok(char *s)
{
	char *p;

	strtoull(s, &p, 0);
	if(p == s)
		return -1;
	if(*p == 0)
		return 0;
	if(p[1] == 0 && strchr("MmGgKk", *p))
		return 0;
	return 0;
}

/*
 * configs	:
 *		| configs config
 * config	: "mem" num
 *		| "queuewrites"
 *		| "addr" address
 *		| "passphrase" string
 *		| "bucket" name
 *		| "s3host" address
 *
 * '#' and \n delimit comments
 */
enum
{
	MaxArgs	= 2
};
int
s3vconfig(char *file, S3Vconfig *config)
{
	IFile f;
	char *s, *line, *flds[MaxArgs + 1];
	int i, ok;

	if(readifile(&f, file) < 0)
		return -1;
	memset(config, 0, sizeof *config);
	config->mem = 0xFFFFFFFFUL;
	config->logfd = -1;
	ok = -1;
	line = nil;
	for(;;){
		s = ifileline(&f);
		if(s == nil){
			ok = 0;
			break;
		}
		line = estrdup(s);
		i = tokenize(s, flds, MaxArgs + 1);
		if(i == 2 && strcmp(flds[0], "s3host") == 0){
			if(config->s3host != nil){
				seterr(EAdmin, "duplicate s3host in config file %s", file);
				break;
			}
			config->s3host = estrdup(flds[1]);
		}else if(i == 2 && strcmp(flds[0], "s3port") == 0){
			if(config->s3port != nil){
				seterr(EAdmin, "duplicate s3port in config file %s", file);
				break;
			}
			config->s3port = estrdup(flds[1]);
		}else if(i == 2 && strcmp(flds[0], "bucket") == 0){
			if(config->bucket != nil){
				seterr(EAdmin, "duplicate bucket in config file %s", file);
				break;
			}
			config->bucket = estrdup(flds[1]);
		}else if(i == 2 && strcmp(flds[0], "passphrase") == 0){
			DigestState *dstate;
			if(config->key != nil){
				seterr(EAdmin, "duplicate passphrase in config file %s", file);
				break;
			}
			config->key = vtmalloc(SHA1dlen);
			dstate = sha1((uchar*)"s3venti", 7, nil, nil);
			sha1((uchar*)flds[1], strlen(flds[1]), config->key, dstate);
		}else if(i == 2 && strcmp(flds[0], "mem") == 0){
			if(numok(flds[1]) < 0){
				seterr(EAdmin, "illegal size %s in config file %s",
					flds[1], file);
				break;
			}
			if(config->mem != 0xFFFFFFFFUL){
				seterr(EAdmin, "duplicate mem lines in config file %s", file);
				break;
			}
			config->mem = unittoull(flds[1]);
		}else if(i == 1 && strcmp(flds[0], "queuewrites") == 0){
			config->queuewrites = 1;
		}else if(i == 2 && strcmp(flds[0], "addr") == 0){
			if(config->vaddr){
				seterr(EAdmin, "duplicate addr lines in configuration file %s", file);
				break;
			}
			config->vaddr = estrdup(flds[1]);
		}else{
			seterr(EAdmin, "illegal line '%s' in configuration file %s", line, file);
			break;
		}
		free(line);
		line = nil;
	}
	free(line);
	freeifile(&f);
	return ok;
}

