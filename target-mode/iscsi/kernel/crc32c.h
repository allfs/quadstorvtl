#ifndef BSD_CRC32C_H_
#define BSD_CRC32C_H_  1

#include "ietbsd.h"

struct chksum_ctx {
	u32 crc;
};

void chksum_init(struct chksum_ctx *mctx);
int chksum_setkey(struct chksum_ctx *mctx, u8 *key, unsigned int keylen, u32 *flags);
void chksum_update(struct chksum_ctx *mctx, const u8 *data, unsigned int length);
void chksum_final(struct chksum_ctx *mctx, u8 *out);

#endif
