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
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

typedef int pid_t;

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
// ! 새 프로그램을 실행시킬 새 커널 스레드를 만든다.
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid; 
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	// todo: file_name 문자열을 파싱,  첫 번째 토큰을 thread_create() 함수에 스레드 이름으로 전달, strtok_r 함수 사용(char *save_ptr 선언 후) 스레드 이름을 전달하는 것은 스레드 생성 함수에서 이름을 전달하는 것과 동일하다.

	// ! --- add ---
	char *save_ptr;
	strtok_r (file_name, " ", &save_ptr);

	// ? save_ptr은 strtok_r이 동일 문자 (fn_copy)를 계속 스캔하기 위해 필요한 저장된 정보를 가르킴
	// ? char *strtok_r(char *string, const char *seps, char **lasts);
	// ! --- end ---

	/* Create a new thread to execute FILE_NAME. */
	// printf(">>> im in process_create_initd!\n"); // debugging
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy); // file_name, fn_copy 정확히 정리하기!! , fn_copy자리에 들어가는 값이 손상받으면 인자를 못받아온다 
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
	// printf(">>> im in initd (start)!\n"); // debugging
	process_init (); // 프로세스 초기화

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	// printf(">>> im in initd (last)!\n"); // debugging

	NOT_REACHED ();
}


// TODO: 유저 스택에 파싱된 토큰을 저장하는 함수 구현
/* 
 * 유저 스택에 프로그램 이름과 인자들을 저장하는 함수
 * parse: 프로그램 이름과 인자가 저장되어 있는 메모리 공간,
 * count: 인자의 개수
 * rsp: 스택 포인터를 가리키는 주소
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
			memset(if_->rsp, 0, sizeof(char **)); // 왜 넣어줘야하지? 이유 알아보기
		}else{
			memcpy(if_->rsp, &argu_addr[i] , sizeof(char **));
		}
	}

	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(void *));

	// 64bit 리눅스에서 argument 전달시 첫 번째 인자 rdi, 두 번째 인자 rsi
	if_->R.rdi = argv_cnt;		// 첫 번째 인자는 arg의 개수
	if_->R.rsi = if_->rsp + 8;	// 두 번째 인자는 리턴 주소 바로 위 argv 시작점

}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *curr = thread_current ();
	
	pid_t pid = thread_create (name, PRI_DEFAULT, __do_fork, curr);		// fork

	if (pid == TID_ERROR) return TID_ERROR;								// gitbook: if the child process fail to duplicate the resource, the fork () call of parent should return the TID_ERROR.
	
	struct thread *child = get_child_process (pid);						// child thread 검색
	sema_down (&child->fork_sema);										// fork_sema가 up될 때까지 대기

	if (child->exit_status == -1) return TID_ERROR;						// 자식이 이상 종료하면 에러 리턴

	return pid;															// child process의 pid를 리턴
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr (va)) return true; // ! 수정 : 이유? 커널 스레드의 정보는 복사하지 않음? gitbook찾아보기

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) return false;				// 페이지가 매핑되어있지 않은 경우

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page (PAL_USER | PAL_ZERO);	// user pool에 페이지를 가져오고, 0으로 초기화
	if (newpage == NULL) return false;					// 할당되지 않은 경우

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, PGSIZE);				// 새 페이지에 부모 페이지를 복제
	writable = is_writable (pte);						// 부모 페이지의 쓰기 가능 여부 저장

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;									// 페이지 mapping이 안된 경우
	}
	return true;
}
#endif
// ! project 2 - extra
struct MapElem
{
	uintptr_t key;
	uintptr_t value;
};

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	parent_if = &parent->parent_if;							// 복사해둔 부모의 if
	memcpy (&if_, parent_if, sizeof (struct intr_frame));	// 자식에게 줄 새 if에 parent_if복사
	if_.R.rax = 0;											// gitbook: In child process, the return value should be 0.

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// multi-oom) Failed to duplicate
	if (parent->fd_idx == FDCOUNT_LIMIT)
		goto error;

	// Project2-extra) multiple fds sharing same file - use associative map (e.g. dict, hashmap) to duplicate these relationships
	// other test-cases like multi-oom don't need this feature
	const int MAPLEN = 10;
	struct MapElem map[10]; // key - parent's struct file * , value - child's newly created struct file *
	int dup_count = 0;		// index for filling map

	for (int i = 0; i < FDCOUNT_LIMIT; i++) {
		struct file *file = parent->file_descriptor_table[i];
		if (file == NULL)
			continue;

		// If 'file' is already duplicated in child, don't duplicate again but share it
		bool found = false;
		// Project2-extra) linear search on key-pair array
		for (int j = 0; j < MAPLEN; j++) {
			if (map[j].key == file) {
				found = true;
				current->file_descriptor_table[i] = map[j].value;
				break;
			}
		}
		if (!found) {
			struct file *new_file;
			if (file > 2)
				new_file = file_duplicate(file);
			else
				new_file = file;

			current->file_descriptor_table[i] = new_file;
			// project2-extra
			if (dup_count < MAPLEN) {
				map[dup_count].key = file;
				map[dup_count++].value = new_file;
			}
		}
	}
	current->fd_idx = parent->fd_idx;
	current->running = file_duplicate (parent->running);	// 실행 파일 복사
	sema_up (&current->fork_sema);		// fork 완료 -> 자식의 fork_sema를 up해서 부모의 대기를 해제

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	// thread_exit ();
	// 복제 실패 상황 : error 상황 처리
	current->exit_status = TID_ERROR;
	sema_up (&current->fork_sema);
	exit (TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	// printf(">>> im in process_exec (start)!\n"); // debugging
	
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	// ! clean up에서 spt 삭제해버림 -> 다시 생성 필요 (initd에서 가져옴)
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	/* argument parsing */
	char *argv[30];
	int argc = 0;

	char *token, *save_ptr;
	token = strtok_r(file_name, " ", &save_ptr);
	while (token != NULL)
	{
		/* 공백 기준으로 명령어 나누어 리스트로 조합 */
		argv[argc] = token;
		token = strtok_r(NULL, " ", &save_ptr);
		argc++;
	}
	
	/* And then load the binary */
	success = load (file_name, &_if);
	
	/* If load failed, quit. */
	if (!success)
	{
		palloc_free_page(file_name);
		return -1;
	}

	argument_stack(&_if, argc, argv);

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* Start switched process. */
	// printf(">>> im in process_exec (last: before do_iret)!\n"); // debugging
	do_iret (&_if);
	NOT_REACHED ();
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
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *child = get_child_process (child_tid);	// 해당 자식 프로세스 검색
	if (child == NULL) return -1;									// running thread에 해당 child_tid의 자식 thread가 없으면 이상 종료

	sema_down (&child->wait_sema);			// 자식이 종료될때까지 부모는 대기함

	// 종료한 상태
	int exit_status = child->exit_status;	// 자식의 종료 상태를 리턴
	list_remove (&child->child_elem);		// 자식 리스트에서 삭제

	sema_up (&child->free_sema);			// 자식이 종료되어, child_list에서 삭제되고 종료상태도 얻었으므로, 자식의 삭제 완료 표시

	return exit_status;
}

struct thread*
get_child_process (int pid) {
    struct thread *cur = thread_current();
    struct list *child_list = &cur->child_list;

    if (list_empty(child_list)) return NULL;
    for(struct list_elem *e = list_begin(child_list); e !=list_end(child_list); e=list_next(e)){
        struct thread *child = list_entry(e, struct thread, child_elem);
        if(child->tid == pid) return child;
    }
    return NULL;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// printf ("im in process_exit! (start)\n"); // debugging
	// Todo: 파일 닫기, 페이지 반환, 실행 파일 닫기
	for (int i = 0; i < FDCOUNT_LIMIT; i++)
	{
		close(i);
	}

	/* thread_create에서 할당한 페이지 할당 해제 */
	palloc_free_multiple(curr->file_descriptor_table, FDT_PAGES); 

	/* 현재 프로세스가 실행중인 파일 종료 */
	file_close(curr->running);

	process_cleanup ();

	sema_up (&curr->wait_sema);		// up할 때까지 부모 프로세스가 자식이 종료된 것을 기다리게 됨
	sema_down (&curr->free_sema);	// 자식의 종료 + 상태반환, 자식 리스트 처리 등을 대기
	// printf ("im in process_exit! (last)\n"); // debugging
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

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
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

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	
	t->running = file;

	file_deny_write(file);

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
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
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
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
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

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	// todo 2 : 매개 변수(aux) 형변환
	struct lazy_load_segment_aux *temp = (struct lazy_load_segment_aux *) aux;
	// todo 2: memory에 올릴 파일을 찾아서
	file_seek (temp->file, temp->ofs);
	// todo 2: 파일을 읽어오기
	if (file_read (temp->file, page->frame->kva, temp->page_read_bytes) != (int) temp->page_read_bytes) {
		palloc_free_page (page->frame->kva); // ? 실패시 조치 ?
		return false;
	}
	memset (page->frame->kva + temp->page_read_bytes, 0, temp->page_zero_bytes);	// copy
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
	ASSERT (pg_ofs (upage) == 0);	// ! va가 페이지의 처음 주소임이 보장됨 !
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		// todo 2: load_segment의 매개변수 설정
		struct lazy_load_segment_aux temp = {
			.page_read_bytes = page_read_bytes,	// 읽을 바이트 수
			.page_zero_bytes = page_zero_bytes,	// 0 바이트 수
			.file = file,						// 읽을 파일 포인터
			.ofs = ofs							// 파일의 읽을 위치(오프셋)
		};
		aux = &temp;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += PGSIZE;	// 다음 페이지를 위해 오프셋 넘기기
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	// ! (gitbook) You might need to provide the way to identify the stack. You can use the auxillary markers in vm_type of vm/vm.h (e.g. VM_MARKER_0) to mark the page.
	if (vm_alloc_page (VM_ANON | VM_MARKER_0, stack_bottom, 1) && vm_claim_page (stack_bottom)){ // ? writable = 1이어야하는 이유? stack이라?
		if_->rsp = USER_STACK;
		success = true;
	}
	return success;
}
#endif /* VM */
