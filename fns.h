#include "ventifns.h"

/*
 * sorted by 6;/^$/|sort -bd +1
 */
int		s3vconfig(char *file, S3Vconfig *config);
void		decryptblock(uchar *block, int size, uchar *key);
void		encryptblock(uchar *block, int size, uchar *key);
void		S3readblock(S3Con *c, VtReq *r);
void		S3writeblock(S3Con *c, VtReq *r);
void		vhdrpack(S3Vhdr *h, uchar *p);
void		vhdrunpack(S3Vhdr *h, uchar *p);

