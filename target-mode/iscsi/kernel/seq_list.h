/*
 * seq_list_* API lifted from the kernel's seq_file.c for backward compatibility
 * with kernels < 2.6.23
 */

#ifndef __SEQ_LIST_H
#define __SEQ_LIST_H

struct list_head *seq_list_start(struct list_head *head, loff_t pos);
struct list_head *seq_list_start_head(struct list_head *head, loff_t pos);
struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *ppos);

#endif
