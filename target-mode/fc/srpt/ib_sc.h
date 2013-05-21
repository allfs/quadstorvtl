#ifndef QS_IB_SC_H_
#define QS_IB_SC_H_
#include "ib_srpt.h"

int target_put_sess_cmd(struct se_session *se_sess, struct se_cmd *se_cmd);
void transport_generic_free_cmd(struct se_cmd *cmd);
void target_cmd_send_failed(struct se_cmd *se_cmd);
void target_cmd_recv_failed(struct se_cmd *se_cmd);
void target_execute_cmd(struct se_cmd *cmd);
int target_submit_cmd(struct se_cmd *se_cmd, struct se_session *se_sess, unsigned char *cdb, unsigned char *sense, u32 unpacked_lun, u32 data_length, int task_attr, int data_dir, unsigned int tag);
int target_submit_tmr(struct se_cmd *se_cmd, struct se_session *se_sess, uint32_t unpacked_lun, unsigned char tm_type, unsigned int tag);
void target_wait_for_sess_cmds(struct se_session *se_sess, int wait_for_tasks);
void transport_deregister_session(struct se_session *se_sess);
struct se_session *transport_init_session(struct srpt_rdma_ch *ch);
int ib_sc_fail_ctio(struct se_cmd *se_cmd, uint8_t asc);
int ib_sc_fail_notify(struct se_cmd *se_cmd, int function);

struct fcbridge* fcbridge_new(void *ha, uint32_t id);
void fcbridge_exit(struct fcbridge *fcbridge);

#endif
