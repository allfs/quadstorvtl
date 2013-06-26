/*
 * Event notification code.
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 *
 * Some functions are based on audit code.
 */

#include <linux/version.h>
#include <net/tcp.h>
#include "iet_u.h"
#include "iscsi.h"
#include "iscsi_dbg.h"

static struct sock *nl;
static u32 ietd_pid;

static int event_recv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	u32 uid, pid, seq;
	char *data;

	pid  = NETLINK_CREDS(skb)->pid;
	uid  = NETLINK_CREDS(skb)->uid;
	seq  = nlh->nlmsg_seq;
	data = NLMSG_DATA(nlh);

	ietd_pid = pid;

	return 0;
}

static void event_recv_skb(struct sk_buff *skb)
{
	int err;
	struct nlmsghdr	*nlh;
	u32 rlen;

	while (skb->len >= NLMSG_SPACE(0)) {
		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) || skb->len < nlh->nlmsg_len)
			return;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		if ((err = event_recv_msg(skb, nlh))) {
			netlink_ack(skb, nlh, -err);
		} else if (nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);
		skb_pull(skb, rlen);
	}
	return;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
static void event_recv(struct sock *sk, int length)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
		event_recv_skb(skb);
		kfree_skb(skb);
	}
}
#endif

static int notify(void *data, int len, gfp_t gfp_mask)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	static u32 seq = 0;
	int payload = NLMSG_SPACE(len);

	if (!(skb = alloc_skb(payload, gfp_mask)))
		return -ENOMEM;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
	nlh = __nlmsg_put(skb, ietd_pid, seq++, NLMSG_DONE, payload - sizeof(*nlh), 0);
#else
	nlh = __nlmsg_put(skb, ietd_pid, seq++, NLMSG_DONE, payload - sizeof(*nlh));
#endif

	memcpy(NLMSG_DATA(nlh), data, len);

	return netlink_unicast(nl, skb, ietd_pid, 0);
}

int event_send(u32 tid, u64 sid, u32 cid, u32 state, int atomic)
{
	int err;
	struct iet_event event;

	event.tid = tid;
	event.sid = sid;
	event.cid = cid;
	event.state = state;

	err = notify(&event, sizeof(struct iet_event), M_NOWAIT);

	return err;
}

int event_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	nl = netlink_kernel_create(&init_net, NETLINK_IET, 1, event_recv_skb, NULL, THIS_MODULE);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
	nl = netlink_kernel_create(NETLINK_IET, 1, event_recv, NULL, THIS_MODULE);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
	nl = netlink_kernel_create(NETLINK_IET, 1, event_recv, THIS_MODULE);
#else
	nl = netlink_kernel_create(NETLINK_IET, event_recv);
#endif
	if (!nl)
		return -ENOMEM;
	else
		return 0;
}

void event_exit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	if (nl)
		netlink_kernel_release(nl);
#else
	if (nl)
		sock_release(nl->sk_socket);
#endif
}
