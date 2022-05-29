#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

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

		// case SYS_FORK:			/* Clone current process. */
		// 	 fork(f->R.rdi);
		// 	 break;
			                    
		// case SYS_EXEC:			/* Switch current process. */
		// 	 exec(f->R.rdi);
        //    	 break;
	
		// case SYS_WAIT:			/* Wait for a child process to die. */
		// 	 wait(f->R.rdi);
		// 	 break; 

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
	
		// case SYS_WRITE:			/* Write to a file. */
		// 	 write(f->R.rdi, f->R.rsi, f->R.rdx);
		// 	 break;
	                   
		// case SYS_SEEK:			/* Change position in a file. */
		// 	 seek(f->R.rdi, f->R.rdx);
		// 	 break;
	                  
        // case SYS_TELL:			/* Report current position in a file. */
		//      tell(f->R.rdi);
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

bool remove(const char *file, unsigned inital_size){

	/* 성공 : True, 실패 : False */
	check_address(file);
	if(filesys_remove(file, inital_size)){
		return true;
	}	
	else	{
		return false;
	}
}