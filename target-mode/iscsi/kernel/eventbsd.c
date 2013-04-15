#include "iscsi.h" 

static struct selinfo kpoll_select;
spinlock_t ring_lock;
#define KCOMM_PAGES (IET_MMAP_SIZE >> PAGE_SHIFT)
#define KCOMM_MSGS_PER_PAGE (PAGE_SIZE / sizeof(struct iet_event))
#define KCOMM_MAX_MSGS (KCOMM_PAGES * KCOMM_MSGS_PER_PAGE)
 
pagestruct_t *kern_bufs[KCOMM_PAGES];
int kring_idx;
atomic_t pending_read_msgs;

static struct iet_event *
kcomm_next_msg(int idx)
{
	int page_idx;
	int page_offset;

	page_idx = idx / KCOMM_MSGS_PER_PAGE;
	page_offset = idx % KCOMM_MSGS_PER_PAGE;

	return (struct iet_event *)((uint8_t *)(page_address(kern_bufs[page_idx])) + (sizeof(struct iet_event) * page_offset));
}

int event_send(u32 tid, u64 sid, u32 cid, u32 state, int atomic)
{
	struct iet_event *send_msg;

	spin_lock(&ring_lock);
	send_msg = kcomm_next_msg(kring_idx);
	if (send_msg->busy)
	{
		spin_unlock(&ring_lock);
		return -1;
	}
	kring_idx++;
	if (kring_idx == KCOMM_MAX_MSGS)
		kring_idx = 0;
	memset(send_msg, 0, sizeof(*send_msg));
	send_msg->busy = 1;
	send_msg->tid = tid;
	send_msg->sid = sid;
	send_msg->cid = cid;
	send_msg->state = state;
	atomic_inc(&pending_read_msgs);
	spin_unlock(&ring_lock);
	mb();
	flush_dcache_page(PHYS_TO_VM_PAGE(vtophys((vm_offset_t)send_msg)));
	selwakeup(&kpoll_select);
	return 0;
}

void
iet_mmap_exit(void)
{
	int i;

	for (i = 0; i < KCOMM_PAGES; i++)
	{
		if (!kern_bufs[i])
			break;
		page_free(kern_bufs[i]);
	}

}

int
iet_mmap_init(void)
{
	int i;

	spin_lock_initt(&ring_lock, "iet ring");
	for (i = 0; i < KCOMM_PAGES; i++)
	{
		kern_bufs[i] = page_alloc(VM_ALLOC_ZERO);
		if (unlikely(!kern_bufs[i])) {
			iet_mmap_exit();
			return -1;
		}
	}
	return 0;
}

int iet_poll(struct cdev *dev, int poll_events, struct thread *td)
{
	unsigned int ret = 0;

	spin_lock(&ring_lock);
	if ((poll_events & (POLLRDNORM | POLLIN)) != 0) {
		if (atomic_read(&pending_read_msgs) > 0) {
			atomic_dec(&pending_read_msgs);
			ret |= POLLIN | POLLRDNORM;
		}
	}
	spin_unlock(&ring_lock);

	if (ret == 0) {
		if (poll_events & (POLLIN | POLLRDNORM))
			selrecord(td, &kpoll_select);
	}

	return (ret);
}

#if __FreeBSD_version >= 900006
int iet_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr, int nprot, vm_memattr_t *memattr __unused)
#else
int iet_mmap(struct cdev *dev, vm_offset_t offset, vm_paddr_t *paddr, int nprot)
#endif
{
	int page_idx;

	page_idx = offset/PAGE_SIZE;
	*paddr = (vm_paddr_t)vtophys(page_address(kern_bufs[page_idx]));
	return 0;
}
