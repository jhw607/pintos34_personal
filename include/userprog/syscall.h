#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* ---- Project 2 : File Descriptor ---- */
/* 파일 사용 시 lock하여 상호배제 구현 */
struct lock filesys_lock;

/* project3 mmap */
struct file *find_file_by_fd(int fd);

#endif /* userprog/syscall.h */
