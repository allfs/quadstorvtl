#ifndef BSDDEFS_H_
#define BSDDEFS_H_  1

#define SOL_TCP		IPPROTO_TCP
#define TCP_CORK	TCP_NOPUSH

#define s6_addr8  __u6_addr.__u6_addr8
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32

static inline char *
strchrnul (const char *s, int c_in)
{
	char c = c_in;
	while (*s && (*s != c))
		s++;

	return (char *) s;
}

#endif
