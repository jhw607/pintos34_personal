#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/* ---------- Project 2 ---------- */
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
/* ------------------------------- */

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int sys_number = f->R.rax;
	switch (sys_number){

		case SYS_HALT:			/* Halt the operating system. */
			 halt();
			 break;
		
		case SYS_EXIT:			/* Terminate this process. */
			exit(f->R.rdi);
			break;

		case SYS_FORK:			/* Clone current process. */
			fork(f->R.rdi, f);
			break;
			                    
		case SYS_EXEC:			/* Switch current process. */
			 if(exec(f->R.rdi) == -1)
			 		 exit(-1);
           	 break;
	
		case SYS_WAIT:			/* Wait for a child process to die. */
			 wait(f->R.rdi);
			 break; 

	    // case SYS_CREATE:		/* Create a file. */
		// 	 create(f->R.rdi, f->R.rsi);
		// 	 break;

		// case SYS_REMOVE:		/* Delete a file. */
		// 	 remove(f->R.rdi, f->R.rsi);
		// 	 break;

		// case SYS_OPEN:			/* Open a file. */
		// 	 open(f->R.rdi);
		// 	 break;	              
	               
        // case SYS_FILESIZE: 		/* Obtain a file's size. */
		// 	 filesize(f->R.rdi);
		// 	 break;

	    // case SYS_READ:			/* Read from a file. */
		// 	 read(f->R.rdi, f->R.rsi, f->R.rdx);
		// 	 break;
	
		case SYS_WRITE:			/* Write to a file. */
			 write(f->R.rdi, f->R.rsi, f->R.rdx);
			 break;
	                   
		// case SYS_SEEK:			/* Change position in a file. */
		// 	 seek(f->R.rdi, f->R.rdx);
		// 	 break;
	                  
        // case SYS_TELL:			/* Report current position in a file. */
		//   tell(f->R.rdi);
		// 	 break;
	                   
		// case SYS_CLOSE:			/* Close a file. */
		// 	 close(f->R.rdi);
		// 	 break;
		                 

	}

	
	printf ("system call!\n");
	thread_exit ();
}


/* ---- Project 2: User memory Access ----*/
// pml4_get_page(page map, addr(유저 가상주소)) : 유저 가상주소와 대응하는 물리주소를 확인하여 해당 물리주소와 연결된 커널 가상 주소를 반환하거나 만약 해당 물리주소가 가상 주소와 매핑되지 않은 영역이면 NULL 반환
void
check_address(void *addr)
{	
	struct thread *t = thread_current(); // 현재 스레드의 thread 구조체를 사용하기 위해서 t를 선언
	if (!is_user_vaddr(addr)||addr==NULL||pml4_get_page(t->pml4, addr)==NULL){
		// 해당 주소값이 유저 가장 주소에 해당하지 않고 or addr = Null or 유저 가상주소가 물리주소와 매핑되지 않은 영역

		exit(-1);
	}

}

/* project 2 : System Call */

void halt(void){

	power_off();
}


void exit(int status){

	struct thread *t = thread_current();
	t->exit_status = status; 
	printf ("%s: exit(%d)\n", t->name, status);
	thread_exit();

}

bool create(const char *file, unsigned inital_size){

	/* 성공 : True, 실패 : False */
	check_address(file);
	if(filesys_create(file, inital_size)){
		return true;
	}
	else	{	
		return false;
	}
}

bool remove(const char *file){

	/* 성공 : True, 실패 : False */
	check_address(file);
	return filesys_remove(file);

}

/* Project 2 : Process structure (fork) */

tid_t fork (const char *thread_name, struct intr_frame *f) {
	// check_address(thread_name);
	return process_fork(thread_name, f);// child thread namedl 들어온다
}

/* Project 2 : Process structure (exec, wait) */

tid_t exec(char *file_name){ // 현재 프로세스를 command line에서 지정된 인수를 전달하여 이름이 지정된 실행 파일로 변경

	check_address(file_name);
	int size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO); // memory setting -> 메모리의 값을 원하는 크기만큼 세팅
	if (fn_copy == NULL){
		exit(-1);
	}
	strlcpy (fn_copy, file_name, size); // file_name을 fn_copy에 문자열 복사하겠다.

	if (process_exec(fn_copy) == -1){ // load 실패시 success -1로 되어 return
		return -1;
	}
	NOT_REACHED();
	return 0;

}

int wait(tid_t pid)
{
	process_wait(pid);
}

/* Project 2 : File Descriptor */

static struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();
	if (fd < 0 || fd > FDCOUNT_LIMIT){
		return NULL;
	}
	return cur->fd_table[fd];
}

// int add_file_to_fdt(struct file *file)
// {
// 	struct thread *cur = thread_current();
// 	struct file **fdt = cur->fd_table;

// 	for (i = 2; i)
// 	cur->fd_table

// 	return fd;

// }

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();
	if (fd < 0 || fd > FDCOUNT_LIMIT){
		return NULL;
	}

	cur->fd_table[fd] = NULL;

}