#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* project 2 : Argument Passing */
// static void argument_stack(int argc_cnt, char **argv_list, void **stp)
static void argument_stack(struct intr_frame *if_, int argv_cnt, char **argv_list);

/* project 2 : Process Structure */
struct thread * get_child(int pid);

/* project 2 : Denying Write to Executable */
// struct lock deny_write_lock;

/* project 3 */
struct aux_lazy_load {

	struct file *file;
	off_t ofs;
	size_t read_bytes;
	size_t zero_bytes;

	// for vm_file
	uintptr_t mmap_addr;
	// int mmap_page_cnt;
};

bool setup_stack (struct intr_frame *if_);
bool lazy_load_segment (struct page *page, void *aux);


#endif /* userprog/process.h */
