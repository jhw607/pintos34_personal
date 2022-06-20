#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
void syscall_init (void);

/* ---- Project 2 : File Descriptor ---- */
/* 파일 사용 시 lock하여 상호배제 구현 */
struct lock filesys_lock;

struct file *find_file_by_fd(int fd);

#endif /* userprog/syscall.h */
