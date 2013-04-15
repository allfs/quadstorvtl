#ifndef QS_ASMDEFS_H_
#define QS_ASMDEFS_H_	1

/* queue helpers */
#define LIST_REMOVE_INIT(elm, field) do {			\
	if (elm->field.le_prev || elm->field.le_next) {		\
		LIST_REMOVE(elm, field);			\
		elm->field.le_prev = NULL;			\
		elm->field.le_next = NULL;			\
	}							\
} while (0)

#define atomic_clear_bit_short(b, p) atomic_clear_short(((volatile short *)p) + (b >> 5), 1 << (b & 0x1f))
#define atomic_set_bit_short(b, p) atomic_set_short(((volatile short *)p) + (b >> 5), 1 << (b & 0x1f))
#define atomic_test_bit_short(b, p)						\
({									\
	int __ret;							\
	__ret = ((volatile short *)p)[b >> 5] & (1 << (b & 0x1f));	\
	__ret;								\
})


#define atomic_clear_bit(b, p) atomic_clear_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f))

#define atomic_set_bit(b, p) atomic_set_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f))

#define atomic_test_bit(b, p)						\
({									\
	int __ret;							\
	__ret = ((volatile int *)p)[b >> 5] & (1 << (b & 0x1f));	\
	__ret;								\
})

#define min_t(type, x, y) ({				\
	type X = (x);					\
	type Y = (y);					\
	X < Y ? X: Y; })

#define max_t(type, x, y) ({				\
	type X = (x);					\
	type Y = (y);					\
	X > Y ? X: Y; })

typedef struct {
	volatile unsigned int val;
} atomic_t;

#define atomic_read(v)		((v)->val)
#define atomic_set(v, i)	((v)->val = (i))

#define atomic_add(i, v)	atomic_add_int(&(v)->val, (i))
#define atomic_inc(v)		atomic_add_int(&(v)->val, 1)
#define atomic_dec(v)		atomic_subtract_int(&(v)->val, 1)
#define atomic_sub(i, v)	atomic_subtract_int(&(v)->val, (i))
#define atomic_dec_and_test(v)	(atomic_fetchadd_int(&(v)->val, -1) == 1)

typedef struct {
	volatile unsigned long val;
} atomic64_t;

#define atomic64_read(v)		((v)->val)
#define atomic64_set(v, i)	((v)->val = (i))

#define atomic64_add(i, v)		atomic_add_long(&(v)->val, (i))
#define atomic64_inc(v)			atomic_add_long(&(v)->val, 1)
#define atomic64_dec(v)			atomic_subtract_long(&(v)->val, 1)
#define atomic64_sub(i, v)		atomic_subtract_long(&(v)->val, (i))
#define atomic64_dec_and_test(v)	(atomic_fetchadd_long(&(v)->val, -1) == 1)

typedef struct {
	volatile unsigned short val;
} atomic16_t;

#define atomic16_read(v)		((v)->val)
#define atomic16_set(v, i)	((v)->val = (i))

#define atomic16_add(i, v)		atomic_add_short(&(v)->val, (i))
#define atomic16_inc(v)			atomic_add_short(&(v)->val, 1)
#define atomic16_dec(v)			atomic_subtract_short(&(v)->val, 1)
#define atomic16_sub(i, v)		atomic_subtract_short(&(v)->val, (i))
#define atomic16_dec_and_test(v)	(atomic_fetchadd_short(&(v)->val, -1) == 1)


#define likely(x)  __builtin_expect((x),1)
#define unlikely(x)  __builtin_expect((x),0)

#endif
