
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               proc.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "tty.h"
#include "console.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "global.h"
#include "proto.h"

PRIVATE void block(struct proc* p);
PRIVATE void unblock(struct proc* p);
PRIVATE int  msg_send(struct proc* current, int dest, MESSAGE* m);
PRIVATE int  msg_receive(struct proc* current, int src, MESSAGE* m);
PRIVATE int  deadlock(int src, int dest);
PRIVATE void if_go_wait();
PRIVATE void if_go_ready();
PRIVATE void ready_to_wait(int index);
PRIVATE void wait_to_ready(int index);
PRIVATE void ready_to_run();

PUBLIC	 struct proc*	 wait_proc_table;
PUBLIC  struct proc*     ready_proc_table;
PRIVATE  int w_count;
PRIVATE  int r_count;


PUBLIC void schedule()
{
	struct proc*	p;
	int		greatest_ticks = 0;

	while (!greatest_ticks) {
		for (p = &FIRST_PROC; p <= &LAST_PROC; p++) {
			if (p->p_flags == 0) {
				if (p->ticks > greatest_ticks) {
					greatest_ticks = p->ticks;
					p_proc_ready = p;
				}
			}
		}

		if (!greatest_ticks)
			for (p = &FIRST_PROC; p <= &LAST_PROC; p++)
				if (p->p_flags == 0)
					p->ticks = p->priority;
	}
}

PRIVATE void if_go_wait()
{
	struct proc* p = ready_proc_table;
	int i = 0;
	for (;i<r_count; p++,i++)
	{
		if (p->ticks >= 10)ready_to_wait(i);
	}
}

PRIVATE void if_go_ready()
{
	struct proc* p = wait_proc_table;
	int j = 0;
	for (; j<w_count; p++, j++)
	{
		if (p->ticks >= 0)wait_to_ready(j);
	}
}

PRIVATE void ready_to_wait(int index)
{
	struct proc* p = &proc_table[index];
	struct proc* q = wait_proc_table;
	int k = 0;
	for (; k < w_count;k++)q++;
	q = p;
	w_count++;
}

PRIVATE void wait_to_ready(int index)
{
	struct proc* p = &proc_table[index];
	struct proc* q = ready_proc_table;
	int l = 0;
	for (; l < r_count; l++)q++;
	q = p;
	r_count++;
}

PRIVATE void ready_to_run()
{
	struct proc*	p = ready_proc_table;
	int		greatest_ticks = 0;

	while (!greatest_ticks) {
		int m = 0;
		for (; m < r_count;m++) {
			if (p->p_flags == 0) {
				if (p->ticks > greatest_ticks) {
					greatest_ticks = p->ticks;
					p_proc_ready = p;
				}
				p++;
			}
		}
		if (!greatest_ticks){
			int n = 0;
			for (; n < r_count; n++, p++)
			if (p->p_flags == 0)
				p->ticks = p->priority;
		}
	}
}

PUBLIC int sys_sendrec(int function, int src_dest, MESSAGE* m, struct proc* p)
{
	assert(k_reenter == 0);	
	assert((src_dest >= 0 && src_dest < NR_TASKS + NR_PROCS) ||
	       src_dest == ANY ||
	       src_dest == INTERRUPT);

	int ret = 0;
	int caller = proc2pid(p);
	MESSAGE* mla = (MESSAGE*)va2la(caller, m);
	mla->source = caller;

	assert(mla->source != src_dest);


	if (function == SEND) {
		ret = msg_send(p, src_dest, m);
		if (ret != 0) return ret;
	}
	else if (function == RECEIVE) {
		ret = msg_receive(p, src_dest, m);
		if (ret != 0) return ret;
	}
	else {
		panic("{sys_sendrec} invalid function: " "%d (SEND:%d, RECEIVE:%d).", function, SEND, RECEIVE);
	}

	return 0;
}


PUBLIC int ldt_seg_linear(struct proc* p, int idx)
{
	struct descriptor * d = &p->ldts[idx];
	return d->base_high << 24 | d->base_mid << 16 | d->base_low;
}


PUBLIC void* va2la(int pid, void* va)
{
	struct proc* p = &proc_table[pid];

	u32 seg_base = ldt_seg_linear(p, INDEX_LDT_RW);
	u32 la = seg_base + (u32)va;

	if (pid < NR_TASKS + NR_NATIVE_PROCS) {assert(la == (u32)va);}

	return (void*)la;
}


PUBLIC void reset_msg(MESSAGE* p)
{
	memset(p, 0, sizeof(MESSAGE));
}


PRIVATE void block(struct proc* p)
{
	assert(p->p_flags);
	schedule();
}


PRIVATE void unblock(struct proc* p)
{
	assert(p->p_flags == 0);
}


PRIVATE int deadlock(int src, int dest)
{
	struct proc* p = proc_table + dest;
	while (1) {
		if (p->p_flags & SENDING) {
			if (p->p_sendto == src) {
				/* print the chain */
				p = proc_table + dest;
				printl("=_=%s", p->name);
				do {
					assert(p->p_msg);
					p = proc_table + p->p_sendto;
					printl("->%s", p->name);
				} while (p != proc_table + src);
				printl("=_=");

				return 1;
			}
			p = proc_table + p->p_sendto;
		}
		else {
			break;
		}
	}
	return 0;
}


PRIVATE int msg_send(struct proc* current, int dest, MESSAGE* m)
{
	struct proc* sender = current;
	struct proc* p_dest = proc_table + dest; /* proc dest */

	assert(proc2pid(sender) != dest);

	/* check for deadlock here */
	if (deadlock(proc2pid(sender), dest)) {panic(">>DEADLOCK<< %s->%s", sender->name, p_dest->name);}

	if ((p_dest->p_flags & RECEIVING) && /* dest is waiting for the msg */
	    (p_dest->p_recvfrom == proc2pid(sender) ||
	     p_dest->p_recvfrom == ANY)) {
		assert(p_dest->p_msg);
		assert(m);

		phys_copy(va2la(dest, p_dest->p_msg),
			  va2la(proc2pid(sender), m),
			  sizeof(MESSAGE));
		p_dest->p_msg = 0;
		p_dest->p_flags &= ~RECEIVING; /* dest has received the msg */
		p_dest->p_recvfrom = NO_TASK;
		unblock(p_dest);

		assert(p_dest->p_flags == 0);
		assert(p_dest->p_msg == 0);
		assert(p_dest->p_recvfrom == NO_TASK);
		assert(p_dest->p_sendto == NO_TASK);
		assert(sender->p_flags == 0);
		assert(sender->p_msg == 0);
		assert(sender->p_recvfrom == NO_TASK);
		assert(sender->p_sendto == NO_TASK);
	}
	else { 
		sender->p_flags |= SENDING;
		assert(sender->p_flags == SENDING);
		sender->p_sendto = dest;
		sender->p_msg = m;

		//添加到待发送队列
		struct proc * p;
		if (p_dest->q_sending) {
			p = p_dest->q_sending;
			while (p->next_sending)
				p = p->next_sending;
			p->next_sending = sender;
		}
		else {
			p_dest->q_sending = sender;
		}
		sender->next_sending = 0;

		block(sender);

		assert(sender->p_flags == SENDING);
		assert(sender->p_msg != 0);
		assert(sender->p_recvfrom == NO_TASK);
		assert(sender->p_sendto == dest);
	}

	return 0;
}



PRIVATE void who_wana_recv(struct proc* p_who_wanna_recv)
{
	assert(p_who_wanna_recv->p_flags == 0);
	assert(p_who_wanna_recv->p_msg == 0);
	assert(p_who_wanna_recv->p_recvfrom == NO_TASK);
	assert(p_who_wanna_recv->p_sendto == NO_TASK);
	assert(p_who_wanna_recv->q_sending != 0);
}
PRIVATE void proc_from(struct proc* p_from)
{
	assert(p_from->p_flags == SENDING);
	assert(p_from->p_msg != 0);
	assert(p_from->p_recvfrom == NO_TASK);
}
PRIVATE int msg_receive(struct proc* current, int src, MESSAGE* m)
{
	struct proc* p_who_wanna_recv = current;
	struct proc* p_from = 0; 
	struct proc* prev = 0;
	int copyok = 0;

	assert(proc2pid(p_who_wanna_recv) != src);

	if ((p_who_wanna_recv->has_int_msg) &&
	    ((src == ANY) || (src == INTERRUPT))) {

		MESSAGE msg;
		reset_msg(&msg);
		msg.source = INTERRUPT;
		msg.type = HARD_INT;

		assert(m);

		phys_copy(va2la(proc2pid(p_who_wanna_recv), m), &msg,sizeof(MESSAGE));

		p_who_wanna_recv->has_int_msg = 0;

		assert(p_who_wanna_recv->p_flags == 0);
		assert(p_who_wanna_recv->p_msg == 0);
		assert(p_who_wanna_recv->p_sendto == NO_TASK);
		assert(p_who_wanna_recv->has_int_msg == 0);

		return 0;
	}


	if (src == ANY) {
		
		if (p_who_wanna_recv->q_sending) {
			p_from = p_who_wanna_recv->q_sending;
			copyok = 1;

			who_wana_recv(p_who_wanna_recv);
			proc_from(p_from);
			assert(p_from->p_sendto == proc2pid(p_who_wanna_recv));
		}
	}
	else if (src >= 0 && src < NR_TASKS + NR_PROCS) {

		p_from = &proc_table[src];

		if ((p_from->p_flags & SENDING) &&
		    (p_from->p_sendto == proc2pid(p_who_wanna_recv))) {

			copyok = 1;

			struct proc* p = p_who_wanna_recv->q_sending;

			assert(p);
			while (p) {
				assert(p_from->p_flags & SENDING);

				if (proc2pid(p) == src) break;

				prev = p;
				p = p->next_sending;
			}

			who_wana_recv(p_who_wanna_recv);
			proc_from(p_from);
			assert(p_from->p_sendto == proc2pid(p_who_wanna_recv));
		}
	}

	if (copyok) {
		
		if (p_from == p_who_wanna_recv->q_sending) { 
			assert(prev == 0);
			p_who_wanna_recv->q_sending = p_from->next_sending;
			p_from->next_sending = 0;
		}
		else {
			assert(prev);
			prev->next_sending = p_from->next_sending;
			p_from->next_sending = 0;
		}

		assert(m);
		assert(p_from->p_msg);

		//复制消息
		phys_copy(va2la(proc2pid(p_who_wanna_recv), m),va2la(proc2pid(p_from), p_from->p_msg),sizeof(MESSAGE));

		p_from->p_msg = 0;
		p_from->p_sendto = NO_TASK;
		p_from->p_flags &= ~SENDING;
		unblock(p_from);
	}
	else {
		p_who_wanna_recv->p_flags |= RECEIVING;

		p_who_wanna_recv->p_msg = m;
		p_who_wanna_recv->p_recvfrom = src;
		block(p_who_wanna_recv);

		assert(p_who_wanna_recv->p_flags == RECEIVING);
		assert(p_who_wanna_recv->p_msg != 0);
		assert(p_who_wanna_recv->p_recvfrom != NO_TASK);
		assert(p_who_wanna_recv->p_sendto == NO_TASK);
		assert(p_who_wanna_recv->has_int_msg == 0);
	}

	return 0;
}


PUBLIC void lock_p(struct proc* p)
{
	p->p_msg->source = INTERRUPT;
	p->p_msg->type = HARD_INT;
	p->p_msg = 0;
	p->has_int_msg = 0;
	p->p_flags &= ~RECEIVING; 
	p->p_recvfrom = NO_TASK;
}

PUBLIC void inform_int(int task_nr)
{
	struct proc* p = proc_table + task_nr;

	if ((p->p_flags & RECEIVING) && 
	    ((p->p_recvfrom == INTERRUPT) || (p->p_recvfrom == ANY))) {
		lock_p(p);
		assert(p->p_flags == 0);
		unblock(p);

		assert(p->p_flags == 0);
		assert(p->p_msg == 0);
		assert(p->p_recvfrom == NO_TASK);
		assert(p->p_sendto == NO_TASK);
	}
	else {
		p->has_int_msg = 1;
	}
}


PUBLIC void dump_proc(struct proc* p)
{
	char info[STR_DEFAULT_LEN];
	int i;
	int text_color = MAKE_COLOR(GREEN, RED);

	int dump_len = sizeof(struct proc);

	out_byte(CRTC_ADDR_REG, START_ADDR_H);
	out_byte(CRTC_DATA_REG, 0);
	out_byte(CRTC_ADDR_REG, START_ADDR_L);
	out_byte(CRTC_DATA_REG, 0);

	sprintf(info, "byte dump of proc_table[%d]:\n", p - proc_table); disp_color_str(info, text_color);
	for (i = 0; i < dump_len; i++) {
		sprintf(info, "%x.", ((unsigned char *)p)[i]);
		disp_color_str(info, text_color);
	}


	disp_color_str("\n\n", text_color);
	sprintf(info, "ANY: 0x%x.\n", ANY); disp_color_str(info, text_color);
	sprintf(info, "NO_TASK: 0x%x.\n", NO_TASK); disp_color_str(info, text_color);
	disp_color_str("\n", text_color);

	sprintf(info, "ldt_sel: 0x%x.  ", p->ldt_sel); disp_color_str(info, text_color);
	sprintf(info, "ticks: 0x%x.  ", p->ticks); disp_color_str(info, text_color);
	sprintf(info, "priority: 0x%x.  ", p->priority); disp_color_str(info, text_color);
	/* sprintf(info, "pid: 0x%x.  ", p->pid); disp_color_str(info, text_color); */
	sprintf(info, "name: %s.  ", p->name); disp_color_str(info, text_color);
	disp_color_str("\n", text_color);
	sprintf(info, "p_flags: 0x%x.  ", p->p_flags); disp_color_str(info, text_color);
	sprintf(info, "p_recvfrom: 0x%x.  ", p->p_recvfrom); disp_color_str(info, text_color);
	sprintf(info, "p_sendto: 0x%x.  ", p->p_sendto); disp_color_str(info, text_color);
	/* sprintf(info, "nr_tty: 0x%x.  ", p->nr_tty); disp_color_str(info, text_color); */
	disp_color_str("\n", text_color);
	sprintf(info, "has_int_msg: 0x%x.  ", p->has_int_msg); disp_color_str(info, text_color);
}



PUBLIC void dump_msg(const char * title, MESSAGE* m)
{
	int packed = 0;
	printl("{%s}<0x%x>{%ssrc:%s(%d),%stype:%d,%s(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)%s}%s",  //, (0x%x, 0x%x, 0x%x)}",
	       title,
	       (int)m,
	       packed ? "" : "\n        ",
	       proc_table[m->source].name,
	       m->source,
	       packed ? " " : "\n        ",
	       m->type,
	       packed ? " " : "\n        ",
	       m->u.m3.m3i1,
	       m->u.m3.m3i2,
	       m->u.m3.m3i3,
	       m->u.m3.m3i4,
	       (int)m->u.m3.m3p1,
	       (int)m->u.m3.m3p2,
	       packed ? "" : "\n",
	       packed ? "" : "\n"/* , */
		);
}

