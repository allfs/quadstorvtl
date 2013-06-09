/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 as published by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */

#include <linuxdefs.h>
#include "queue.h"
#include <ioctldefs.h>
#include <exportdefs.h>
#include <asm/ioctls.h>

/* Corelib interface definitions */

/* Socket related stuff */

static void sys_sock_data_ready(struct sock *sk, int count);
static void sys_sock_write_space(struct sock *sk);
static void sys_sock_state_change(struct sock *sk);

static int
sys_sock_has_read_data(sock_t *sys_sock)
{
	int avail = 0;
	int retval;

	retval = kernel_sock_ioctl(sys_sock->sock, SIOCINQ, (unsigned long)&avail);
	return (retval >= 0) ? avail : 0;
}

static int
sys_sock_has_write_space(sock_t *sys_sock)
{
	return (sk_stream_wspace(sys_sock->sock->sk) >= sk_stream_min_wspace(sys_sock->sock->sk));
}

static int
sys_sock_read(sock_t *sys_sock, void *buf, int len)
{
	struct msghdr msg;
	struct kvec iov;
	int retval;

	iov.iov_base = buf;
	iov.iov_len = len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	retval = kernel_recvmsg(sys_sock->sock, &msg, &iov, 1, len, MSG_DONTWAIT | MSG_NOSIGNAL);
	if (retval > 0)
		return retval;
	else if (retval == -EAGAIN || retval == -EINTR)
		return 0;
	else
		return retval;
}

static void
sys_sock_free(sock_t *sys_sock)
{
	sock_release(sys_sock->sock);
	kfree(sys_sock);
}

static void
sock_deactivate(sock_t *sys_sock)
{
	struct socket *sock = sys_sock->sock;

	write_lock_bh(&sock->sk->sk_callback_lock);

	/* Restore callbacks */
	if (sys_sock->state_change) {
		sock->sk->sk_state_change = sys_sock->state_change;
		sys_sock->state_change = NULL;
	}

	if (sys_sock->data_ready) {
		sock->sk->sk_data_ready = sys_sock->data_ready;
		sys_sock->data_ready = NULL;
	}

	if (sys_sock->write_space) {
		sock->sk->sk_write_space = sys_sock->write_space;
		sys_sock->write_space = NULL;
	}

	sock->sk->sk_user_data = NULL;
	write_unlock_bh(&sock->sk->sk_callback_lock);
}

static void
sys_sock_close(sock_t *sys_sock, int linger)
{
	struct socket *sock = sys_sock->sock;

	if (!linger)
		kernel_setsockopt(sock, SOL_SOCKET, SO_LINGER, (void *)&linger, sizeof(linger));

	sock_deactivate(sys_sock);
	if (sock->ops->shutdown)
		sock->ops->shutdown(sock, SEND_SHUTDOWN|RCV_SHUTDOWN);
}

static int
sys_sock_write_page(sock_t *sys_sock, pagestruct_t *page, int offset, int len)
{
	struct socket *sock = sys_sock->sock;
	ssize_t (*sendpage)(struct socket *, pagestruct_t *, int, size_t, int);
	int flags = MSG_DONTWAIT |  MSG_NOSIGNAL;
	int retval;

	sendpage = sock->ops->sendpage ? sock->ops->sendpage : sock_no_sendpage;
	retval = sendpage(sock, page, offset, len, flags); 
	if (retval > 0)
		return retval;
	else if (retval == -EAGAIN || retval == -EINTR)
		return 0;
	else
		return retval;
}

static int
sys_sock_write(sock_t *sys_sock, void *buf, int len)
{
	struct msghdr msg;
	struct kvec iov;
	int retval;

	iov.iov_base = buf;
	iov.iov_len = len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	retval = kernel_sendmsg(sys_sock->sock, &msg, &iov, 1, len);
	if (retval > 0)
		return retval;
	else if (retval == -EAGAIN || retval == -EINTR)
		return 0;
	else
		return retval;
}

static sock_t *
sys_sock_create(void *priv)
{
	sock_t *sys_sock;
	int retval;

	sys_sock = kzalloc(sizeof(*sys_sock), GFP_NOIO);
	if (unlikely(!sys_sock))
		return NULL;

	retval = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sys_sock->sock);
	if (unlikely(retval < 0)) {
		kfree(sys_sock);
		return NULL;
	}
	sys_sock->priv = priv;
	return sys_sock;
}

static void
sock_activate(sock_t *sys_sock)
{
	struct socket *sock = sys_sock->sock;

	write_lock_bh(&sock->sk->sk_callback_lock);
	sock->sk->sk_user_data = sys_sock;
	sock->sk->sk_allocation = GFP_NOFS;

	/* Save callbacks */
	sys_sock->state_change = sock->sk->sk_state_change;
	sys_sock->data_ready = sock->sk->sk_data_ready;
	sys_sock->write_space = sock->sk->sk_write_space;

	/* Set new callbacks */
	sock->sk->sk_state_change = sys_sock_state_change;
	sock->sk->sk_data_ready = sys_sock_data_ready;
	sock->sk->sk_write_space = sys_sock_write_space;
	write_unlock_bh(&sock->sk->sk_callback_lock);
}

static void
sys_sock_nopush(sock_t *sys_sock, int set)
{
	 kernel_setsockopt(sys_sock->sock, SOL_TCP, TCP_CORK, (void *)&set, sizeof(set));
}

static sock_t *
sys_sock_accept(sock_t *sys_sock, void *priv, int *error, uint32_t *ipaddr)
{
	sock_t *new_syssock;
	struct socket *sock = sys_sock->sock;
	struct socket *newsock;
	struct sockaddr_in *sinaddr;
	int disabled = 1;
	int retval;

	new_syssock = sys_sock_create(priv);
	newsock = new_syssock->sock;
	newsock->type = sock->type;
	newsock->ops = sock->ops;

	retval = sock->ops->accept(sock, newsock, O_NONBLOCK);
	if (retval != 0) {
		sys_sock_close(new_syssock, 1);
		sys_sock_free(new_syssock);
		return NULL;
	}

	sock_activate(new_syssock);
	if (newsock->ops->getname(newsock, (struct sockaddr *)&new_syssock->saddr, &new_syssock->saddr_len, 2)) {
		sys_sock_close(new_syssock, 1);
		sys_sock_free(new_syssock);
		return NULL;
        }

	if (newsock->sk->sk_state == TCP_ESTABLISHED)
		sys_sock_state_change(newsock->sk);

	kernel_setsockopt(newsock, SOL_TCP, TCP_NODELAY, (void *)&disabled, sizeof(disabled));
	sinaddr = (struct sockaddr_in *)(&new_syssock->saddr);
	*ipaddr = sinaddr->sin_addr.s_addr;
	return new_syssock;
}

static int
sys_sock_bind(sock_t *sys_sock, uint32_t addr, uint16_t port)
{
	struct socket *sock = sys_sock->sock;
	struct sockaddr_in saddr_in;
	int retval;
	int reuse = 1;

	memset(&saddr_in, 0, sizeof(saddr_in));
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = htons(port);
	saddr_in.sin_addr.s_addr = addr; 

	kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));
	retval = sock->ops->bind(sock, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
	if (retval < 0)
		return -1;

	retval = sock->ops->listen(sock, 1024);
	if (retval < 0)
		return -1;

	sock_activate(sys_sock);
	return 0;
}

static int
sys_sock_connect(sock_t *sys_sock, uint32_t addr, uint32_t local_addr, uint16_t port)
{
	struct socket *sock = sys_sock->sock;
	struct sockaddr_in saddr_in;
	int disabled = 1;
	int retval;

	if (!local_addr || (local_addr == addr))
		goto skip_bind;

	memset(&saddr_in, 0, sizeof(saddr_in));
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_addr.s_addr = local_addr; 

	retval = sock->ops->bind(sock, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
	if (retval < 0)
		return -1;

skip_bind:
	memset(&saddr_in, 0, sizeof(saddr_in));
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = htons(port);
	saddr_in.sin_addr.s_addr = addr; 
	sock_activate(sys_sock);
	retval = sock->ops->connect(sock, (struct sockaddr *)&saddr_in, sizeof(saddr_in), O_NONBLOCK);
	if (retval != 0 && retval != -EINPROGRESS)
		return -1;

	kernel_setsockopt(sock, SOL_TCP, TCP_NODELAY, (void *)&disabled, sizeof(disabled));
	return 0;
}

static void
debug_warn(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static void
debug_print(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static void
debug_info(char *fmt, ...) { }

static void
debug_check(void)
{
#ifdef WARN_ON
	WARN_ON(1);
#else
	BUG();
#endif
}

unsigned long
__msecs_to_ticks(unsigned long msecs)
{
	return msecs_to_jiffies(msecs);
}

unsigned long
__ticks_to_msecs(unsigned long ticks)
{
	return jiffies_to_msecs(ticks);
}

static uint32_t
get_ticks(void)
{
	return jiffies;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
int
bdev_unmap_support(iodev_t *iodev)
{
	struct request_queue *q = bdev_get_queue(iodev);

	if (blk_queue_discard(q))
		return 1;
	else
		return 0;
}
#else
int
bdev_unmap_support(iodev_t *iodev)
{
	return 0;
}
#endif

iodev_t*
open_block_device(const char *devpath, uint64_t *size, uint32_t *sector_size, int *error)
{
	iodev_t *b_dev;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	b_dev = blkdev_get_by_path(devpath, FMODE_READ | FMODE_WRITE, THIS_MODULE);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
	b_dev = open_bdev_exclusive(devpath, FMODE_READ | FMODE_WRITE, THIS_MODULE);
#else
	b_dev = open_bdev_excl(devpath, 0, THIS_MODULE);
#endif
	if (unlikely(IS_ERR(b_dev)))
	{
		DEBUG_WARN_NEW("Unable to open dev %s err is %ld\n", devpath, PTR_ERR(b_dev));
		*error = -1;
		return NULL;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	*sector_size = bdev_hardsect_size(b_dev);
#else
	*sector_size = bdev_logical_block_size(b_dev);
#endif
	*size = b_dev->bd_inode->i_size;
	return b_dev;
}

static void
close_block_device(iodev_t *b_dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
	int flags = FMODE_READ | FMODE_WRITE;
#endif 

	if (!b_dev)
		return;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	blkdev_put(b_dev, flags);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
	close_bdev_exclusive(b_dev, flags);
#else
	close_bdev_excl(b_dev);
#endif  
}

/* 
 *Note corelib doesn't not understand GFP_* flags. flags > 1 means zeroed page
 */
static pagestruct_t*
vm_pg_alloc(allocflags_t aflags)
{
	pagestruct_t *pp;
	int flags = GFP_NOIO | (aflags ? __GFP_ZERO : 0);

	pp = alloc_page(flags);
	return pp;
}

static void
vm_pg_free(pagestruct_t *pp)
{
	__free_page(pp);
}

static void*
vm_pg_address(pagestruct_t *pp)
{
	return (void *)(page_address(pp));
}

static void
vm_pg_ref(pagestruct_t *pp)
{
	get_page(pp);
}

static void
vm_pg_unref(pagestruct_t *pp)
{
	put_page(pp);
}

void *
vm_pg_map(pagestruct_t **pp, int pg_count)
{
	return vmap(pp, pg_count, VM_MAP, PAGE_KERNEL);
}

void
vm_pg_unmap(void *maddr, int pg_count)
{
	vunmap(maddr);
}

static void*
uma_zcreate(const char *name, size_t size)
{
	void *cachep;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	cachep = slab_cache_create(name, size, 0, 0, NULL, NULL);
#else
	cachep = slab_cache_create(name, size, 0, 0, NULL);
#endif
	return cachep;
}

static void
uma_zdestroy(const char *name, void *cachep)
{
	slab_cache_destroy(cachep);
}

static void*
uma_zalloc(uma_t *cachep, allocflags_t aflags, size_t len)
{
	int flags = (aflags & Q_NOWAIT_INTR) ? GFP_ATOMIC : GFP_NOIO;
	int wait = (aflags & Q_WAITOK) ? 1 : 0;
	void *ret;

	while (!(ret = kmem_cache_alloc(cachep, flags)) && wait)
		msleep(1);

	if (ret && (aflags & Q_ZERO))
		memset(ret, 0, len);
	return ret;
}

static void
uma_zfree(uma_t *cachep, void *ptr)
{
	kmem_cache_free(cachep, ptr);
} 

static void*
__zalloc(size_t size, int type, allocflags_t aflags)
{
	int flags = (aflags & Q_NOWAIT_INTR) ? GFP_ATOMIC : GFP_NOIO;
	int wait = (aflags & Q_WAITOK) ? 1 : 0;
	void *ret;

	while (!(ret = kzalloc(size, flags)) && wait)
		msleep(1);
	return ret;
}

static void*
__malloc(size_t size, int type, allocflags_t aflags)
{
	int flags = (aflags & Q_NOWAIT) ? GFP_ATOMIC : GFP_NOIO;
	int wait = (aflags & Q_WAITOK) ? 1 : 0;
	void *ret;

	while (!(ret = kmalloc(size, flags)) && wait)
		msleep(1);
	return ret;
}

static void
__free(void *ptr)
{
	kfree(ptr);
}

uma_t *mtx_cache;
uma_t *sx_cache;
uma_t *cv_cache;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
uma_t *tpriv_cache;
#endif
uma_t *bpriv_cache;

static mtx_t *
mtx_alloc(const char *name)
{
	mtx_t *mtx;

	while (!(mtx = kmem_cache_alloc(mtx_cache, GFP_NOIO)))
		msleep(1);

	spin_lock_init(mtx);
	return mtx;
}

static void
mtx_free(mtx_t *mtx)
{
	kmem_cache_free(mtx_cache, mtx);
}

static void
__mtx_lock(mtx_t *mtx)
{
	mtx_lock(mtx);
}

static void
__mtx_lock_intr(mtx_t *mtx, void *data)
{
	unsigned long *flags = data;
	mtx_lock_irqsave(mtx, *flags);
}

static void
__mtx_unlock(mtx_t *mtx)
{
	mtx_unlock(mtx);
}

static void
__mtx_unlock_intr(mtx_t *mtx, void *data)
{
	unsigned long *flags = data;
	mtx_unlock_irqrestore(mtx, *flags);
}

static sx_t *
shx_alloc(const char *name)
{
	sx_t *sx;

	while (!(sx = kmem_cache_alloc(sx_cache, GFP_NOIO)))
		msleep(1);

	mutex_init(sx);
	return sx;
}

static void
shx_free(sx_t *sx)
{
	kmem_cache_free(sx_cache, sx);
}

static void
shx_xlock(sx_t *sx)
{
	mutex_lock(sx);
}

static void
shx_xunlock(sx_t *sx)
{
	mutex_unlock(sx);
}

static void
shx_slock(sx_t *sx)
{
	mutex_lock(sx);
}

static void
shx_sunlock(sx_t *sx)
{
	mutex_unlock(sx);
}

static int
shx_xlocked(sx_t *sx)
{
	return mutex_is_locked(sx);
}

static cv_t *
cv_alloc(const char *name)
{
	cv_t *cv;

	while (!(cv = kmem_cache_alloc(cv_cache, GFP_NOIO)))
		msleep(1);

	init_waitqueue_head(cv);
	return cv;
}

static void
cv_free(cv_t *cv)
{
	kmem_cache_free(cv_cache, cv);
}

static void
cv_wait(cv_t *cv, mtx_t *mtx, void *data, int intr)
{
	unsigned long *flags = data;
	DEFINE_WAIT(wait);

	add_wait_queue_exclusive(cv, &wait);
	if (!intr)
		set_current_state(TASK_UNINTERRUPTIBLE);
	else
		set_current_state(TASK_INTERRUPTIBLE);
	mtx_unlock_irqrestore(mtx, *flags);
	schedule();
	mtx_lock_irqsave(mtx, *flags);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(cv, &wait);
}

static long 
cv_timedwait(cv_t *cv, mtx_t *mtx, void *data, int timo)
{
	unsigned long *flags = data;
	DEFINE_WAIT(wait);
	long ret;

	add_wait_queue_exclusive(cv, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	mtx_unlock_irqrestore(mtx, *flags);
	ret = schedule_timeout(msecs_to_jiffies(timo));
	mtx_lock_irqsave(mtx, *flags);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(cv, &wait);
	return ret;
}

static void
cv_wait_sig(cv_t *cv, mtx_t *mtx, int intr)
{
	DEFINE_WAIT(wait);

	add_wait_queue_exclusive(cv, &wait);
	if (!intr)
		set_current_state(TASK_UNINTERRUPTIBLE);
	else
		set_current_state(TASK_INTERRUPTIBLE);
	mtx_unlock(mtx);
	schedule();
	mtx_lock(mtx);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(cv, &wait);
}

static void
wakeup(cv_t *cv, mtx_t *mtx)
{
	unsigned long flags;

	mtx_lock_irqsave(mtx, flags);
	wake_up_all(cv);
	mtx_unlock_irqrestore(mtx, flags);
}

static void
wakeup_nointr(cv_t *cv, mtx_t *mtx)
{
	mtx_lock(mtx);
	wake_up_all(cv);
	mtx_unlock(mtx);
}

static void
wakeup_compl(cv_t *cv, mtx_t *mtx, int *done)
{
	unsigned long flags;

	mtx_lock_irqsave(mtx, flags);
	*done = 1;
	wake_up_all(cv);
	mtx_unlock_irqrestore(mtx, flags);
}

static void
wakeup_one(cv_t *cv, mtx_t *mtx)
{
	unsigned long flags;

	mtx_lock_irqsave(mtx, flags);
	wake_up(cv);
	mtx_unlock_irqrestore(mtx, flags);
}

static void
wakeup_one_nointr(cv_t *cv, mtx_t *mtx)
{
	mtx_lock(mtx);
	wake_up(cv);
	mtx_unlock(mtx);
}

static void
wakeup_one_compl(cv_t *cv, mtx_t *mtx, int *done)
{
	unsigned long flags;

	mtx_lock_irqsave(mtx, flags);
	*done = 1;
	wake_up(cv);
	mtx_unlock_irqrestore(mtx, flags);
}

static void
wakeup_one_unlocked(cv_t *cv)
{
	wake_up(cv);
}

static void
wakeup_unlocked(cv_t *cv)
{
	wake_up_all(cv);
}

static uint64_t
get_availmem(void)
{
	struct sysinfo si;

	si_meminfo(&si);
	return (si.totalram * si.mem_unit);
}

static void
bdev_marker(iodev_t *b_dev, struct tpriv *tpriv)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	tpriv->data = kmem_cache_alloc(tpriv_cache, GFP_NOIO);
	if (tpriv->data)
		blk_start_plug(tpriv->data);
#endif
}

static void
bdev_start(iodev_t *b_dev, struct tpriv *tpriv)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	if (tpriv->data) {
		blk_finish_plug(tpriv->data);
		kmem_cache_free(tpriv_cache, tpriv->data);
		tpriv->data = NULL;
	}
#else
	struct request_queue *bdev_q = bdev_get_queue(b_dev);

	if (bdev_q && bdev_q->unplug_fn)
		bdev_q->unplug_fn(bdev_q);
#endif
}

static void
__pause(const char *msg, int timo)
{
	msleep(timo);
}

static void
__qs_printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static int
__sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsprintf(buf, fmt, args);
	va_end(args);
	return ret;
}

static int
__snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return ret;
}

static int
__kernel_thread_check(int *flags, int bit)
{
	return kthread_should_stop();
}

static void
sched_prio(int prio)
{
	int set_prio = -15;

	switch (prio) {
	case QS_PRIO_SWP:
		set_prio = -20;
		break;
	case QS_PRIO_INOD:
		set_prio = -19;
		break;
	default:
		DEBUG_BUG_ON(1);
	}
	set_user_nice(current, set_prio);
}

static int
__kernel_thread_stop(kproc_t *task, int *flags, void *chan, int bit)
{
	return kthread_stop(task);
}

static int
get_cpu_count(void)
{
	return num_online_cpus();
}

struct bio_priv {
	void *priv;
	void (*end_bio_func)(bio_t *bio, int err);
};

/*
 * Generic linux end bio callback, which means core lib is independent of which 
 * OS or which version of OS we are running on
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
int static bio_end_bio(bio_t *bio, uint32_t bytes_done, int err)
#else
void static bio_end_bio(bio_t *bio, int err)
#endif
{
	struct bio_priv *bpriv = (struct bio_priv *)(bio->bi_private);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	if (bio->bi_size)
		return 1;
#endif

	(*bpriv->end_bio_func)(bio, err);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	return 0;
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
int
bio_unmap(iodev_t *iodev, void *cp, uint64_t start_sector, uint32_t blocks, uint32_t shift, void (*end_bio_func)(bio_t *, int), void *priv)
{
	int err;
	int diff = (shift - 9);

	if (diff) {
		start_sector <<= diff;
		blocks <<= diff;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
	err = blkdev_issue_discard(iodev, start_sector, blocks, GFP_NOIO, DISCARD_FL_WAIT);
#else
	err = blkdev_issue_discard(iodev, start_sector, blocks, GFP_NOIO, 0);
#endif
	if (err)
		return -1;
	else
		return 1;
}
#else
int
bio_unmap(iodev_t *iodev, void *cp, uint64_t start_sector, uint32_t blocks, uint32_t shift, void (*end_bio_func)(bio_t *, int), void *priv)
{
	return -1;
}
#endif

void
g_destroy_bio(bio_t *bio)
{
	kmem_cache_free(bpriv_cache, bio->bi_private);
	bio_put(bio);
}

static void
bio_set_command(bio_t *bio, int cmd)
{
	switch (cmd) {
	case QS_IO_READ:
		bio->bi_rw = READ;
		break;
	case QS_IO_WRITE:
		bio->bi_rw = WRITE;
		break;
	default:
		DEBUG_BUG_ON(1);
	}
}

bio_t *
g_new_bio(iodev_t *iodev, void (*end_bio_func)(bio_t *, int), void *consumer, uint64_t bi_sector, int bio_vec_count, int rw)
{
	struct bio_priv *bpriv;
	struct bio *bio;

	bio = bio_alloc(M_NOWAIT, bio_vec_count);
	if (unlikely(!bio))
		return NULL;

	bpriv = kmem_cache_alloc(bpriv_cache, GFP_NOIO);
	if (unlikely(!bpriv)) {
		bio_put(bio);
		return NULL;
	}

	bio->bi_sector = bi_sector;
	bio->bi_bdev = iodev;
	bpriv->priv = consumer;
	bpriv->end_bio_func = end_bio_func;
	bio->bi_end_io = bio_end_bio;
	bio->bi_private = bpriv;
	bio_set_command(bio, rw);
	return bio;
}

static void
bio_free_pages(bio_t *bio)
{
	struct bio_vec *bvec;
	int j;

	bio_for_each_segment(bvec, bio, j) {
		put_page(bvec->bv_page);
	}
}

static void 
bio_free_page(bio_t *bio)
{
	struct bio_vec *bvec;

	bvec = bio_iovec_idx(bio, 0);
	put_page(bvec->bv_page);
}

static int
bio_get_command(bio_t *bio)
{
	if (bio->bi_rw == READ)
		return QS_IO_READ;
	else
		return QS_IO_WRITE;
}

static int
bio_get_length(bio_t *bio)
{
	return bio->bi_size;
}

static void* 
bio_get_caller(bio_t *bio)
{
	struct bio_priv *bpriv = (struct bio_priv *)(bio->bi_private);
	return bpriv->priv;
}

static iodev_t*
send_bio(bio_t *bio)
{
	iodev_t *iodev = bio->bi_bdev;
	generic_make_request(bio);
	return iodev;
}

static iodev_t *
bio_get_iodev(bio_t *bio)
{
	return bio->bi_bdev;
}

static uint64_t
bio_get_start_sector(bio_t *bio)
{
	return bio->bi_sector;
}

static uint32_t
bio_get_max_pages(iodev_t *iodev)
{
	return bio_get_nr_vecs(iodev);
}

static uint32_t 
bio_get_nr_sectors(bio_t *bio)
{
	return bio_sectors(bio);
}

static void
processor_yield(void)
{
	yield();
}

static int
kproc_create(void *fn, void *data, kproc_t **task, const char namefmt[], ...)
{
	va_list args;
	int ret;
	kproc_t *tsk;
	char name[64];

	va_start(args, namefmt);
	vsprintf(name, namefmt, args);
	va_end(args);

	tsk = kthread_run(fn, data, name);
	if (IS_ERR(tsk)) {
		ret = -1;
	}
	else {
		*task = tsk;
		ret = 0;
	}
	return ret;
}

static int
__copyout(void *kaddr, void *uaddr, size_t len)
{
	return copy_to_user(uaddr, kaddr, len);
}

static int
__copyin(void *uaddr, void *kaddr, size_t len)
{
	return copy_from_user(kaddr, uaddr, len);
}

static void
kern_panic(char *msg)
{
	panic(msg);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
static void
thread_start(struct tpriv *tpriv)
{
	if (current->io_context)
		put_io_context(current->io_context);

	if (!(current->io_context = ioc_task_link(tpriv->data)))
		tpriv->data = get_io_context(GFP_KERNEL, -1);

}

static void
thread_end(struct tpriv *tpriv)
{
}
#else
static void
thread_start(struct tpriv *tpriv)
{
	struct io_context *gioc = tpriv->data;

	get_io_context(GFP_KERNEL);

	if (!current->io_context)
		return;

	if (gioc)
		copy_io_context(&current->io_context, &gioc);
	else {
		tpriv->data = current->io_context;
	}
}

static void
thread_end(struct tpriv *tpriv)
{
	struct io_context *ioc = current->io_context;

	if (!ioc)
		return;

	task_lock(current);
	current->io_context = NULL;
	task_unlock(current);

	put_io_context(ioc);
}
#endif

static struct qs_kern_cbs kcbs = {
	.debug_warn		= debug_warn,
	.debug_info		= debug_info,
	.debug_print		= debug_print,
	.debug_check		= debug_check,
	.msecs_to_ticks		= __msecs_to_ticks,
	.ticks_to_msecs		= __ticks_to_msecs,
	.get_ticks		= get_ticks,
	.open_block_device	= open_block_device,
	.close_block_device	= close_block_device,
	.vm_pg_alloc		= vm_pg_alloc,
	.vm_pg_free		= vm_pg_free,
	.vm_pg_address		= vm_pg_address,
	.vm_pg_ref		= vm_pg_ref,
	.vm_pg_unref		= vm_pg_unref,
	.vm_pg_map		= vm_pg_map,
	.vm_pg_unmap		= vm_pg_unmap,
	.uma_zcreate		= uma_zcreate,
	.uma_zdestroy		= uma_zdestroy,
	.uma_zalloc		= uma_zalloc,
	.uma_zfree		= uma_zfree,
	.zalloc			= __zalloc,
	.malloc			= __malloc,
	.free			= __free,
	.get_availmem		= get_availmem,
	.mtx_alloc		= mtx_alloc,
	.mtx_free		= mtx_free,
	.mtx_lock		= __mtx_lock,
	.mtx_lock_intr		= __mtx_lock_intr,
	.mtx_unlock		= __mtx_unlock,
	.mtx_unlock_intr	= __mtx_unlock_intr,
	.shx_alloc		= shx_alloc,
	.shx_free		= shx_free,
	.shx_xlock		= shx_xlock,
	.shx_xunlock		= shx_xunlock,
	.shx_slock		= shx_slock,
	.shx_sunlock		= shx_sunlock,
	.shx_xlocked		= shx_xlocked,
	.bdev_start		= bdev_start,
	.bdev_marker		= bdev_marker,
	.cv_alloc		= cv_alloc,
	.cv_free		= cv_free,
	.cv_wait		= cv_wait,
	.cv_timedwait		= cv_timedwait,
	.cv_wait_sig		= cv_wait_sig,
	.wakeup			= wakeup,
	.wakeup_nointr		= wakeup_nointr,
	.wakeup_one		= wakeup_one,
	.wakeup_one_nointr	= wakeup_one_nointr,
	.wakeup_one_unlocked	= wakeup_one_unlocked,
	.wakeup_unlocked	= wakeup_unlocked,
	.wakeup_compl		= wakeup_compl,
	.wakeup_one_compl	= wakeup_one_compl,
	.pause			= __pause,
	.printf			= __qs_printf,
	.sprintf		= __sprintf,
	.snprintf		= __snprintf,
	.kernel_thread_check	= __kernel_thread_check,
	.kernel_thread_stop	= __kernel_thread_stop,
	.sched_prio		= sched_prio,
	.get_cpu_count		= get_cpu_count,
	.g_new_bio		= g_new_bio,
	.bio_free_pages		= bio_free_pages,
	.bio_add_page		= bio_add_page,
	.bio_free_page		= bio_free_page,
	.bio_set_command	= bio_set_command,
	.bio_get_command	= bio_get_command,
	.bio_get_caller		= bio_get_caller,
	.bio_get_length		= bio_get_length,
	.send_bio		= send_bio,
	.bio_get_iodev		= bio_get_iodev,
	.bio_get_start_sector	= bio_get_start_sector,
	.bio_get_nr_sectors	= bio_get_nr_sectors,
	.bio_get_max_pages	= bio_get_max_pages,
	.g_destroy_bio		= g_destroy_bio,
	.bio_unmap		= bio_unmap,
	.bdev_unmap_support	= bdev_unmap_support,
	.processor_yield	= processor_yield,
	.kproc_create		= kproc_create,
	.thread_start		= thread_start,
	.thread_end		= thread_end,
	.copyout		= __copyout,
	.copyin			= __copyin,
	.kern_panic		= kern_panic,
	.sock_create		= sys_sock_create,
	.sock_connect		= sys_sock_connect,
	.sock_read		= sys_sock_read,
	.sock_write		= sys_sock_write,
	.sock_write_page	= sys_sock_write_page,
	.sock_free		= sys_sock_free,
	.sock_close		= sys_sock_close,
	.sock_bind		= sys_sock_bind,
	.sock_accept		= sys_sock_accept,
	.sock_has_write_space	= sys_sock_has_write_space,
	.sock_has_read_data	= sys_sock_has_read_data,
	.sock_nopush		= sys_sock_nopush,
};

static void
sys_sock_data_ready(struct sock *sk, int count)
{
	sock_t *sys_sock = sk->sk_user_data;

	if (!sys_sock)
		return;

	(*kcbs.sock_read_avail)(sys_sock->priv); 
}

static void
sys_sock_write_space(struct sock *sk)
{
	sock_t *sys_sock = sk->sk_user_data;

	if (!sys_sock)
		return;

	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk))
		(*kcbs.sock_write_avail)(sys_sock->priv);
}

static void
sys_sock_state_change(struct sock *sk)
{
	sock_t *sys_sock = sk->sk_user_data;
	int newstate;

	if (!sys_sock)
		return;

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		newstate = SOCK_STATE_CONNECTED;
		break;
	default:
		newstate = SOCK_STATE_CLOSED;
	}
	(*kcbs.sock_state_change)(sys_sock->priv, newstate);
}

sx_t ioctl_lock;
static int coremod_open(vnode_t *i, struct file *f);
static int coremod_release(vnode_t *i, struct file *f);
static ssize_t coremod_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
static long coremod_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
static int coremod_ioctl(vnode_t *i, struct file *f, uint32_t cmd, unsigned long arg);
#endif

static struct file_operations coremod_fops = {
	.owner = THIS_MODULE,
	.open = coremod_open,
	.release = coremod_release,
	.read = coremod_read,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	.compat_ioctl = coremod_ioctl,
	.unlocked_ioctl = coremod_ioctl,
#else
	.ioctl = coremod_ioctl,
#endif
};

static int
coremod_open(vnode_t *i, struct file *f)
{
	return 0;
}

static int
coremod_release(vnode_t *i, struct file *f)
{
	return 0;
}

static ssize_t
coremod_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
static long coremod_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
static int coremod_ioctl(vnode_t *i, struct file *f, uint32_t cmd, unsigned long arg)
#endif
{
	void __user *userp = (void __user *)arg;
	int err = 0;
	int retval = 0;
	struct bdev_info *bdev_info;
	struct mdaemon_info mdaemon_info;
	struct group_conf *group_conf;
	struct vdeviceinfo *deviceinfo;
	struct vcartridge *vcartridge;
	struct fc_rule_config fc_rule_config;

	/* Check the capabilities of the user */
	if (!capable(CAP_SYS_ADMIN))
	{
		return (-EPERM);
	}

	if (_IOC_TYPE (cmd) != TL_MAGIC)
	{
		return -ENOTTY;
	}

	if (_IOC_DIR (cmd) & _IOC_READ)
	{
		err = !access_ok (VERIFY_READ, userp, _IOC_SIZE (cmd));
	}

	if (_IOC_DIR (cmd) & _IOC_WRITE)
	{
		err = !access_ok (VERIFY_WRITE, userp, _IOC_SIZE (cmd));
	}

	if (err)
	{
		return -EPERM;
	}

	sx_xlock(&ioctl_lock);
	switch (cmd) {
	case TLTARGIOCDAEMONSETINFO:
		if ((retval = copyin(userp, &mdaemon_info, sizeof(mdaemon_info))) != 0)
			break;
		(*kcbs.mdaemon_set_info)(&mdaemon_info);
		break;
	case TLTARGIOCADDFCRULE:
	case TLTARGIOCREMOVEFCRULE:
		if ((retval = copyin(userp, &fc_rule_config, sizeof(fc_rule_config))) != 0)
			break;
		if (cmd == TLTARGIOCADDFCRULE)
			retval = (*kcbs.target_add_fc_rule)(&fc_rule_config);
		else if (cmd == TLTARGIOCREMOVEFCRULE)
			retval = (*kcbs.target_remove_fc_rule)(&fc_rule_config);
		else
			retval = -1;
		break;
	case TLTARGIOCNEWBLKDEV:
	case TLTARGIOCDELBLKDEV:
	case TLTARGIOCGETBLKDEV:
	case TLTARGIOCUNMAPCONFIG:
		bdev_info = malloc(sizeof(struct bdev_info), M_QUADSTOR, M_NOWAIT);
		if (!bdev_info) {
			retval = -ENOMEM;
			break;
		}

		if ((retval = copyin(userp, bdev_info, sizeof(struct bdev_info))) != 0) {
			free(bdev_info, M_QUADSTOR);
			break;
		}

		if (cmd == TLTARGIOCNEWBLKDEV)
			retval = (*kcbs.bdev_add_new)(bdev_info);
		else if (cmd == TLTARGIOCDELBLKDEV)
			retval = (*kcbs.bdev_remove)(bdev_info);
		else if (cmd == TLTARGIOCGETBLKDEV)
			retval = (*kcbs.bdev_get_info)(bdev_info);
		else if (cmd == TLTARGIOCUNMAPCONFIG)
			retval = (*kcbs.bdev_unmap_config)(bdev_info);
		if (retval == 0)
			retval = copyout(bdev_info, userp, sizeof(struct bdev_info));
		else
			err = copyout(bdev_info, userp, sizeof(struct bdev_info));
		free(bdev_info, M_QUADSTOR);
		break;
	case TLTARGIOCNEWDEVICE:
	case TLTARGIOCDELETEDEVICE:
	case TLTARGIOCMODDEVICE:
	case TLTARGIOCGETDEVICEINFO:
	case TLTARGIOCLOADDRIVE:
		deviceinfo = malloc(sizeof(*deviceinfo), M_QUADSTOR, M_WAITOK);
		if (!deviceinfo) {
			retval = -ENOMEM;
			break;
		}

		if ((retval = copyin(userp, deviceinfo, sizeof(*deviceinfo))) != 0) {
			free(deviceinfo, M_QUADSTOR);
			break;
		}

		if (cmd == TLTARGIOCNEWDEVICE)
			retval = (*kcbs.vdevice_new)(deviceinfo);
		else if (cmd == TLTARGIOCDELETEDEVICE)
			retval = (*kcbs.vdevice_delete)(deviceinfo);
		else if (cmd == TLTARGIOCMODDEVICE)
			retval = (*kcbs.vdevice_modify)(deviceinfo);
		else if (cmd == TLTARGIOCGETDEVICEINFO)
			retval = (*kcbs.vdevice_info)(deviceinfo);
		else if (cmd == TLTARGIOCLOADDRIVE)
			retval = (*kcbs.vdevice_load)(deviceinfo);

		if (retval == 0)
			retval = copyout(deviceinfo, userp, sizeof(*deviceinfo));
		else
			err = copyout(deviceinfo, userp, sizeof(*deviceinfo));
		free(deviceinfo, M_QUADSTOR);
		break;
	case TLTARGIOCNEWVCARTRIDGE:
	case TLTARGIOCLOADVCARTRIDGE:
	case TLTARGIOCDELETEVCARTRIDGE:
	case TLTARGIOCGETVCARTRIDGEINFO:
		vcartridge = malloc(sizeof(*vcartridge), M_QUADSTOR, M_WAITOK);
		if (!vcartridge) {
			retval = -ENOMEM;
			break;
		}

		if ((retval = copyin(userp, vcartridge, sizeof(*vcartridge))) != 0) {
			free(vcartridge, M_QUADSTOR);
			break;
		}

		if (cmd == TLTARGIOCNEWVCARTRIDGE)
			retval = (*kcbs.vcartridge_new)(vcartridge);
		else if (cmd == TLTARGIOCLOADVCARTRIDGE)
			retval = (*kcbs.vcartridge_load)(vcartridge);
		else if (cmd == TLTARGIOCDELETEVCARTRIDGE)
			retval = (*kcbs.vcartridge_delete)(vcartridge);
		else if (cmd == TLTARGIOCGETVCARTRIDGEINFO)
			retval = (*kcbs.vcartridge_info)(vcartridge);
		if (retval == 0)
			retval = copyout(vcartridge, userp, sizeof(*vcartridge));
		else
			err = copyout(vcartridge, userp, sizeof(*vcartridge));
		free(vcartridge, M_QUADSTOR);
		break;
	case TLTARGIOCCHECKDISKS:
		retval = (*kcbs.coremod_check_disks)();
		break;
	case TLTARGIOCLOADDONE:
		retval = (*kcbs.coremod_load_done)();
		break;
	case TLTARGIOCUNLOAD:
		retval = (*kcbs.coremod_exit)();
		break;
	case TLTARGIOCADDGROUP:
	case TLTARGIOCDELETEGROUP:
	case TLTARGIOCRENAMEGROUP:
		group_conf = malloc(sizeof(*group_conf), M_QUADSTOR, M_WAITOK);
		if (!group_conf) {
			retval = -ENOMEM;
			break;
		}

		if ((retval = copyin(userp, group_conf, sizeof(*group_conf))) != 0) {
			free(group_conf, M_QUADSTOR);
			break;
		}

		if (cmd == TLTARGIOCADDGROUP)
			retval = (*kcbs.bdev_add_group)(group_conf);
		else if (cmd == TLTARGIOCDELETEGROUP)
			retval = (*kcbs.bdev_delete_group)(group_conf);
		else if (cmd == TLTARGIOCRENAMEGROUP)
			retval = (*kcbs.bdev_rename_group)(group_conf);
		free(group_conf, M_QUADSTOR);
		break;
	default:
		break;
	}
	sx_xunlock(&ioctl_lock);

	return retval;
}

static void
exit_caches(void)
{
	if (bpriv_cache)
		uma_zdestroy("bpriv_cache", bpriv_cache);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	if (tpriv_cache)
		uma_zdestroy("tpriv_cache", tpriv_cache);
#endif

	if (mtx_cache)
		uma_zdestroy("mtx_cache", mtx_cache);

	if (sx_cache)
		uma_zdestroy("sx_cache", sx_cache);

	if (cv_cache)
		uma_zdestroy("cv_cache", cv_cache);
}

static int
init_caches(void)
{
	int mtx_size;

	bpriv_cache = uma_zcreate("bpriv_cache", sizeof(struct bio_priv));
	if (unlikely(!bpriv_cache))
		return -1;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	tpriv_cache = uma_zcreate("tpriv_cache", sizeof(struct blk_plug));
	if (unlikely(!tpriv_cache))
		return -1;
#endif

	mtx_size = max_t(int, sizeof(mtx_t), sizeof(void *));

	mtx_cache = uma_zcreate("mtx_cache", mtx_size);
	if (unlikely(!mtx_cache))
		return -1;

	sx_cache = uma_zcreate("sx_cache", sizeof(sx_t));
	if (unlikely(!sx_cache))
		return -1;
 
	cv_cache = uma_zcreate("cv_cache", sizeof(cv_t));
	if (unlikely(!cv_cache))
		return -1;
	return 0; 
}

#define coremod	THIS_MODULE
int
device_register_interface(struct qs_interface_cbs *icbs)
{
	int retval;

	if (!module_reference(coremod)) {
		return -1;
	}

	retval = __device_register_interface(icbs);
	if (retval != 0)
		module_release(coremod);
	return retval;
}

void
device_unregister_interface(struct qs_interface_cbs *icbs)
{
	int retval;

	retval = __device_unregister_interface(icbs);
	if (retval == 0)
		module_release(coremod);
}

static int dev_major;

static int coremod_init(void)
{
	int retval;

	sx_init(&ioctl_lock, "core ioctl lck");

	retval = init_caches();
	if (unlikely(retval != 0)) {
		exit_caches();
		return -1;
	}

	retval = kern_interface_init(&kcbs);
	if (unlikely(retval != 0)) {
		exit_caches();
		return -1;
	}

	dev_major = register_chrdev(0, TL_DEV_NAME, &coremod_fops);
	if (unlikely(dev_major < 0)) {
		kern_interface_exit();
		exit_caches();
		return dev_major;
	}

	return 0; 
}

static void
coremod_exit(void)
{
	sx_xlock(&ioctl_lock);
	kern_interface_exit();
	sx_xunlock(&ioctl_lock);
	exit_caches();
	unregister_chrdev(dev_major, TL_DEV_NAME);
}

MODULE_AUTHOR("Shivaram Upadhyayula, QUADStor Systems");
MODULE_LICENSE("GPL");
module_init (coremod_init);
module_exit (coremod_exit);
EXPORT_SYMBOL(kern_interface_init);
EXPORT_SYMBOL(kern_interface_exit);
EXPORT_SYMBOL(device_register_interface);
EXPORT_SYMBOL(device_unregister_interface);
