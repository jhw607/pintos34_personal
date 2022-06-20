#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
/* project 2 */
#include "userprog/process.h"
#include "vm/vm.h"
#ifdef VM
#include "vm/vm.h"
#include "threads/malloc.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();

	/* project 2 : Denying Write to Executable */
	/* 파일 사용 시 lock 초기화 */
	// lock_init(&deny_write_lock);
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) { // command line에서 받은 arguments를 통해 실행하고자 하는 파일에 대한 process를 만드는 과정의 시작
	char *fn_copy;
	tid_t tid;
	char *save_ptr;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0); // memory setting -> 메모리의 값을 원하는 크기만큼 세팅
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE); // file_name을 fn_copy에 문자열 복사하겠다.

	/* -------------- Project 2 ---------------*/
	/* 첫번째 공백 전까지의 문자열 파싱*/
	file_name = strtok_r (file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	// 주의사항) 1) 인자 file_name은 command line에서 file의 이름만 parsing한 것이어야함
	//         2) 인자 fn_copy는 command line 전체를 넣어줘야함
	//         -> 만약 이렇게 하지 않으면 stack에 앞 부분만 담기기 때문에 문제가 발생한다!!
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy); // file_name을 이름으로 하고 PRI_DEFAULT를 우선순위 값으로 가지는 새로운 스레드 생성
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();
	
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	// printf("fork start\n");
	struct thread *parent = thread_current();
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame)); // 현재 스레드의 intr_frame 구조체, intr_frame 구조체 바로 받아오기 &, 무슨 차이지?
																
	
	// printf("thread_create start\n");
	tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, parent); // 순서 물어보기
	// printf("thread_create end\n");
	
	if (pid == TID_ERROR){
		return TID_ERROR;
	}

	/* project 2 : Process Structure */
	struct thread *child = get_child(pid);
	// printf("sema_down start\n");
	sema_down(&child->fork_sema); 
	// printf("sema_down end\n");

	// fork 오류나서 추가한 부분(debug)
	if (child->exit_status == -1)
		return TID_ERROR;


	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) { //부모 page table을 복제하기 위해 page table을 생성
													 //왜 복제 ? : 자식 프로세스가 생성되면 부모 프로세스가 가진 것과 동일한 페이지 테이블 생성
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)){
		return true; // return false ends pml4_for_each, which is undesirable - just return true to pass this kernel va
	}
	// 부모 페이지가 kernel 페이지를 가리키면 (User -> Kernel을 가리키는 것이 되므로 바로 false로 리턴해야한다)

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va); 
	if (parent_page == NULL){
		return false;
	}
	// pml4_get_page를 통해서 물리 주소에 매핑되어있는 가상주소를 가져온다(parent) -> 0이면 false를 return
	// va와 parent_page의 차이가 무엇일까? 둘다 가상 주소 아닌가?

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	newpage = palloc_get_page (PAL_USER | PAL_ZERO); // PAL_USER가 오는 이유?
	if (newpage == NULL){ 
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	
	memcpy(newpage, parent_page, PGSIZE); // parent_page는 가상주소이고, 이것을 newpage에 복사 _ SIZE는 4KB(할당해준 공간도 4KB)
	writable = is_writable(pte); // pte가 읽고/쓰기가 가능한지 확인


	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) { // 정확히 함수 인자들이 이해가 안감
		// va = 부모 가상 주소, newpage = 부모 가상 주소를 복사했지만 자식 주소, current->pml4 = 자식 물리주소겠지 
		// va의 역할은 뭐지 ? 
		/* 6. TODO: if fail to insert page, do error handling. */
		// printf("Failed to map user virtual page to given physical frame\n"); // #ifdef DEBUG
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	// printf("do_fork start\n");
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* process_fork에서 복사 해두었던 intr_frame */
	/* debug */
	parent_if = &parent->parent_if;


	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; // fork 함수의 결과로 자식 프로세스는 0을 반환해야한다.

	/* 2. Duplicate PT */
	//current가 child, pml4는 물리페이지 테이블을 만든다.
	current->pml4 = pml4_create(); 
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	// 부모 pml4를 duplicate_pte함수에 적용 -> 다 복제하겠다.
	// pml4_for_each->pdp_for_each->pgdir_for_each->pt_for_each(함수 타고 들어가면 나옴)
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->fd_idx >= FDCOUNT_LIMIT)
		goto error;
	
	current->fd_table[0] = parent->fd_table[0]; // stdin
	current->fd_table[1] = parent->fd_table[1]; // stdout

	for (int i = 2 ; i < FDCOUNT_LIMIT ;i++ ){
		struct file *f = parent->fd_table[i];
		if (f == NULL){
			continue;
		}
		current->fd_table[i] = file_duplicate(f);
	}

	current->fd_idx = parent->fd_idx;
	// if child loaded successfully, wake up parent in process_fork
	sema_up(&current->fork_sema);
	// process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR);
	// thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

/* process 실행 */
// 파싱되지 않은 f_name을 인자로 받아서 전체적으로 파싱해준다
int
process_exec (void *f_name) { 
	char *file_name = f_name;
	bool success;
	char *token, *save_ptr;
	int argc=0;
	char *argv[30];

	/* -------------- Project 2 ---------------*/
	/* Command Line 전체 Parsing */
	for (token = strtok_r (file_name, " ", &save_ptr); token != NULL; 
	token = strtok_r (NULL, " ", &save_ptr)){
		
		argv[argc] = token;
		argc++;

	}
   
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	/* And then load the binary */
	success = load (file_name, &_if);

	/* If load failed, quit. */
	if (!success)
	{
		palloc_free_page(file_name);
		return -1;
	}

	argument_stack(&_if, argc, argv);

	// argument_stack(argc, argv, _if.rsp);

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}



// TODO: 유저 스택에 파싱된 토큰을 저장하는 함수 구현
/* 
 * 유저 스택에 프로그램 이름과 인자들을 저장하는 함수
 * parse: 프로그램 이름과 인자가 저장되어 있는 메모리 공간,
 * count: 인자의 개수
 * esp: 스택 포인터를 가리키는 주소
*/

static void argument_stack(struct intr_frame *if_, int argv_cnt, char **argv_list) {
	int i;
	char *argu_addr[128];
	int argc_len;

	for (i = argv_cnt-1; i >= 0; i--){
		argc_len = strlen(argv_list[i]);
		if_->rsp = if_->rsp - (argc_len+1); 
		memcpy(if_->rsp, argv_list[i], (argc_len+1));
		argu_addr[i] = if_->rsp;
	}

	while (if_->rsp%8 != 0){
		if_->rsp--;
		memset(if_->rsp, 0, sizeof(uint8_t));
	}

	for (i = argv_cnt; i>=0; i--){
		if_->rsp = if_->rsp - 8;
		if (i == argv_cnt){
			memset(if_->rsp, 0, sizeof(char **));
		}else{
			memcpy(if_->rsp, &argu_addr[i] , sizeof(char **));
		}
	}

	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(void *));

	if_->R.rdi = argv_cnt;
	if_->R.rsi = if_->rsp + 8;	

}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {

	/*
	자식프로세스가 모두 종료될 때까지 대기(sleep state)
	자식프로세스가 올바르게 종료 됐는지 확인
	*/

	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */



	struct thread *child =get_child(child_tid);
    // 본인의 자식이 아닌경우(호출 프로세스의 하위 항목이 아닌 경우)
	// child_tid를 받고 부모 list에서 찾아봤는데 없는 경우
	if (child == NULL){
		return -1;
	}

	/*
	sema_down(struct semaphore *)
	세마포어의 value가 0일 경우 현재 스레드를 THREAD_BLOCK 상태로 변경 후 schedule() 호출
	sema_up(struct semaphore * )
	대리 리스트에 스레드가 존재하면 리스트 맨 처음에 위치한 스레드를 THREAD_READY 상태로 변경 후 schedule() 호출
	*/

	sema_down(&child->wait_sema);
	// 여기서는 parent가 잠드는 거고 -> sema_down 무한루프 돌고있음 -> 해제하려면 sema value가 증가해야함(어디서든 sema up을 해줘야함)
	// -> sema up은 process_exit에서 해준다

	// 여기서부터는 깨어났다.
	int exit_status = child->exit_status; // child exit status를 받았다.
	// 깨어나면 child의 exit_status를 얻는다.
    list_remove(&child->child_elem);
	// child를 부모 list에서 지운다
	sema_up(&child->free_sema);
	// child exit status를 받았음을 전달하는 sema
	// process_exit에서 자식은 종료 허락을 받기위해 기다리고 있고, 부모가 자식의 정보를 다 가져왔음을 알리면 자식은 정상 종료한다
	// 자세한 내용은 밑에 코드 주석에 달려있다.

	return exit_status;
	
	// while(1){

	// }
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	// close all opened files
	for (int i=0; i<FDCOUNT_LIMIT; i++)
	{
		close(i);
	}
    
	palloc_free_multiple(curr->fd_table, FDT_PAGES); 	// for multi-oom(메모리 누수)
	file_close(curr->running); 	// for rox- (실행중에 수정 못하도록)

	process_cleanup (); // pml4를 날림(이 함수를 call 한 thread의 pml4)

	sema_up(&curr->wait_sema); 	// 종료되었다고 기다리고 있는 부모 thread에게 signal 보냄-> sema_up에서 val을 올려줌
	sema_down(&curr->free_sema); // 부모에게 exit_status가 정확히 전달되었는지 확인(wait)
								 // why ? : 자식프로세스가 종료가 바로 되버리면(부모 프로세스 상관없이)
								 // process_wait에서 child 리스트의 elem 제거, exit status 전달받음 등등을 못받을 수 있다
								 // 오류가 발생할 수 있으므로 부모 프로세스가 자식 프로세스의 정보를 다 가져오기 전까지 살아 있도록 하는 장치
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF(실행파일) executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP(다음 실행할 명령어의 주소 보관함)
 * and its initial stack pointer into *RSP(스택의 꼭대기).
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* project 2 : Denying Write to Executable */
	/* lock 획득 */
	// lock_acquire(&deny_write_lock);

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {

		// lock_release(&deny_write_lock);
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	/* project 2 : Denying Write to Executable */
	/* 실행 중인 스레드 t의 running을 실행할 파일로 초기화*/
	t->running = file;

	/* 현재 오픈한 파일에 다른내용 쓰지 못하게 함 */
	file_deny_write(file);

	// lock_release(&deny_write_lock);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					// printf("load segment before\n");
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
					// printf("load segment after\n");

						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	// printf("setup_stack before\n");
	if (!setup_stack (if_))
	// printf("setup_stack after\n");
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
			thread_current()->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */


bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	// vm_alloc_page_with_initializer -> page만든 것을 전달해줌
	// page fault가 일어났을 떄, 불려야하는데 바로 실행되는 것 아닌가? -> page fault가 일어났을 때, 불려짐
	
	struct aux_lazy_load *aux_box = (struct aux_lazy_load *)aux;
	struct file *file = aux_box->file;
	off_t aux_ofs = aux_box->ofs;
	size_t page_read_bytes = aux_box->read_bytes;
	size_t page_zero_bytes = aux_box->zero_bytes;
	// printf("aux_ofs: %d\n page_read_bytes : %d\n page_zero_bytes : %d\n", aux_ofs, page_read_bytes, page_zero_bytes);
	// printf("lazy_load_segment start\n");
	file_seek(file, aux_ofs); 
	/* 이미 만들어져 있으니까 load만 하면된다 */
	/* Load this page */
	// 실패시 palloc free 하는것이 맞나?
	if (file_read(file, page->frame->kva, page_read_bytes) != (int) page_read_bytes){
		// palloc_free_page(page->frame->kva);
		return false;
	}

	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);	
	// printf("lazy_load_segment finish\n");
	free(aux);
	//return은 뭘로?, 안해도 되나?
	return true;

}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);
	// printf("load segment sssstart\n");
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct aux_lazy_load *aux_box = malloc(sizeof (struct aux_lazy_load));
		// *aux_box=(struct aux_lazy_load)
		// {
		// 		.file = file,
		// 		.ofs = ofs,
		// 		// .upage = upage,
		// 		.read_bytes = page_read_bytes,
		// 		.zero_bytes = page_zero_bytes,
		// 		// .writable = writable, 
		// };
		aux_box->file = file;
		aux_box->ofs = ofs;
		aux_box->read_bytes = page_read_bytes;
		aux_box->zero_bytes = page_zero_bytes;

		// printf("load segment start\n");
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux_box))
			return false;
		// printf("load segment finish\n");


		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}





/* Create a PAGE of stack at the USER_STACK. Return true on success. */
// 스택 페이지를 만들라는 것인데, 왜 만들어야하지? 이 페이지의 역할은 무엇?
// lazy load 할 필요가 없음, 스택을 식별하는 방법은 무엇일까?-> VM_MARKER_0
bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
	// printf("setup_stack start\n");
	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	if(vm_alloc_page(VM_ANON|VM_MARKER_0, stack_bottom, 1)) {
		success= vm_claim_page(stack_bottom);
		if (success) {
			if_->rsp = USER_STACK;
		}
	}
	return success;
}



#endif /* VM */


// /* Project 2 : Process Structure */

struct thread * get_child(int pid){

	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	struct list_elem *e;
	if (list_empty(child_list)) return NULL;
	for (e = list_begin (child_list); e != list_end (child_list); e = list_next (e)){
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid){
			return t; // 자식 스레드 반환
		}	
	}
	return NULL;
}

