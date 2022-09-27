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
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
/* ---------- Project 3 ---------- */
#include "vm/vm.h"
/* ---------- Project 4 ---------- */
#include "filesys/directory.h"
#include "filesys/inode.h"
/* ------------------------------- */


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

const int STDIN = 1;
const int STDOUT = 2;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);

/* Syscall function */
void halt(void);
void exit(int status);
bool create(const char *file, unsigned inital_size);
bool remove(const char *file);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec(char *file_name);
int wait(tid_t pid);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

/* Syscall helper Functions */
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
static char **parse_directory(const char *name);
static struct dir *find_target_dir(const char **path);

/* project 3 */
void check_buf(void *addr);

/* project 4 */
static bool chdir(const char *dir);
static bool mkdir(const char *dir);
static bool readdir(int fd, char *name);
static bool isdir(int fd);
static int inumber(int fd);
static int symlink(const char *target, const char *linkpath);
static int mount(const char *path, int chan_no, int dev_no);
static int umount(const char *path);


void syscall_init(void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	
	/* 파일 사용 시 lock 초기화 */
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	thread_current()->rsp = f->rsp;
	int sys_number = f->R.rax;
	switch (sys_number){

		case SYS_HALT:			/* Halt the operating system. */
			halt();
			break;
		
		case SYS_EXIT:			/* Terminate this process. */
			exit(f->R.rdi);
			break;

		case SYS_FORK:			/* Clone current process. */
			f->R.rax = fork(f->R.rdi, f);
			break;
			                    
		case SYS_EXEC:			/* Switch current process. */
			 if(exec(f->R.rdi) == -1)
			 	exit(-1);
           	 break;
	
		case SYS_WAIT:			/* Wait for a child process to die. */
			 f->R.rax = wait(f->R.rdi);
			 break; 

	    case SYS_CREATE:		/* Create a file. */
			 f->R.rax = create(f->R.rdi, f->R.rsi);
			 break;

		case SYS_REMOVE:		/* Delete a file. */
			 f->R.rax = remove(f->R.rdi);
			 break;

		case SYS_OPEN:			/* Open a file. */
			 f->R.rax = open(f->R.rdi);
			 break;	              
	               
        case SYS_FILESIZE: 		/* Obtain a file's size. */
			 f->R.rax = filesize(f->R.rdi);
			 break;

	    case SYS_READ:			/* Read from a file. */
			 f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			 break;
	
		case SYS_WRITE:			/* Write to a file. */
			 f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			 break;
	                   
		case SYS_SEEK:			/* Change position in a file. */
			 seek(f->R.rdi, f->R.rsi);
			 break;
	                  
        case SYS_TELL:			/* Report current position in a file. */
			 f->R.rax = tell(f->R.rdi);
			 break;
	                   
		case SYS_CLOSE:			/* Close a file. */
			 close(f->R.rdi);
			 break;

		case SYS_MMAP:
			f->R.rax = mmap (f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;

		case SYS_MUNMAP:
			munmap (f->R.rdi);
			break;

		case SYS_CHDIR:
			f->R.rax = chdir((const char *)f->R.rdi);
			break;

		case SYS_MKDIR:
			f->R.rax = mkdir((const char *)f->R.rdi);
			break;

		case SYS_READDIR:
			f->R.rax = readdir((int)f->R.rdi, (char *)f->R.rsi);
			break;

		case SYS_ISDIR:
			f->R.rax = isdir((int)f->R.rdi);
			break;

		case SYS_INUMBER:
			f->R.rax = inumber((int)f->R.rdi);
			break;

		case SYS_SYMLINK:
			f->R.rax = symlink((const char *)f->R.rdi, (const char *)f->R.rsi);
			break;

		case SYS_MOUNT:
			f->R.rax = mount((const char *)f->R.rdi, (int)f->R.rsi, (int)f->R.rdx);
			break;

		case SYS_UMOUNT:
			f->R.rax = umount((const char *)f->R.rdi);
			break;

			
		default:
			// printf ("system call!\n");
			// thread_exit ();
			exit(-1);
			break;
		            
	}
	
}


/* ---- Project 2: User memory Access ----*/
// pml4_get_page(page map, addr(유저 가상주소)) : 유저 가상주소와 대응하는 물리주소를 확인하여 해당 물리주소와 연결된 커널 가상 주소를 반환하거나 만약 해당 물리주소가 가상 주소와 매핑되지 않은 영역이면 NULL 반환
// void
// check_address(void *addr)
// {	
// 	struct thread *t = thread_current(); // 현재 스레드의 thread 구조체를 사용하기 위해서 t를 선언
	
// 	}

// }

#ifdef VM
void
check_address(void *addr)
{	
	struct thread *t = thread_current(); // 현재 스레드의 thread 구조체를 사용하기 위해서 t를 선언
	if (!is_user_vaddr(addr)||addr==NULL){
		// 해당 주소값이 유저 가상 주소에 해당하지 않고 or addr = Null
		exit(-1);
	}
	
}
#else
void
check_address(void *addr)
{	
	struct thread *t = thread_current(); // 현재 스레드의 thread 구조체를 사용하기 위해서 t를 선언	
	if (!is_user_vaddr(addr)||addr==NULL||pml4_get_page(t->pml4, addr)==NULL){
		// 해당 주소값이 유저 가상 주소에 해당하지 않고 or addr = Null or 유저 가상주소가 물리주소와 매핑되지 않은 영역
		exit(-1);

}
#endif

void check_buf(void *addr){
	struct thread *t = thread_current();

	struct page *p = spt_find_page(&t->spt, addr);
	if(p!=NULL && !p->writable){
		// printf(">> p->writable : %d\n",p->writable);
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
	printf ("%s: exit(%d)\n", thread_name(), status);
	thread_exit();

}

// todo: 수정
bool create(const char *file, unsigned inital_size){

	check_address(file);
	struct thread *curr = thread_current();

	if (*file == NULL)
        return false;

    if (file[strlen(file) - 1] == '/')
        return false;

    char **path = parse_directory(file);
    struct dir *current_dir = curr->current_dir;
    struct dir *target_dir = find_target_dir(path);
    struct inode *target_inode;
    if (target_dir == NULL)
        return false;
    curr->current_dir = target_dir;

    int i;
    for (i = 1; path[i] != NULL; i++)
        ;
    char *file_name = path[i - 1];

    if (dir_lookup(target_dir, file_name, &target_inode)) {
        curr->current_dir = current_dir;
        dir_close(target_dir);
        return false;
    }

    lock_acquire(&filesys_lock);
    bool create_result = filesys_create(file_name, initial_size);
    lock_release(&filesys_lock);

    curr->current_dir = current_dir;
    dir_close(target_dir);

    struct list_elem *tmp;
    struct list_elem *next_tmp;
    char *prev_link = file;
    for (tmp = list_begin(&sym_list); tmp != list_end(&sym_list); tmp = next_tmp) {
        struct sym_map *tmp_sym = list_entry(tmp, struct sym_map, sym_elem);
        next_tmp = list_next(tmp);
        if (strcmp(prev_link, tmp_sym->target) == 0) {
            list_remove(tmp);
            sys_symlink(tmp_sym->target, tmp_sym->linkpath);
            prev_link = tmp_sym->linkpath;
            free(tmp_sym->target);
            free(tmp_sym);
        }
    }

    return create_result;


	/****************** before project 4 ******************/

	/* 성공 : True, 실패 : False */
	// check_address(file);
	// if(filesys_create(file, inital_size)){
	// 	return true;
	// }
	// else	{	
	// 	return false;
	// }
}

// todo: empty directory도 제거할 수 있도록 수정
// todo: . 이나 .. 을 제외한 어떠한 서브디렉토리도 없어야함
// todo: open되어있거나, current working directory로 쓰이고있는 디렉토리도 지울 수 있는지 정해야함
// todo: -> .이나 ..을 포함해 open시도, 삭제된 디렉토리 내에 새로운 파일 create하는 것들이 금지되어야함
bool remove(const char *file){
	
	check_address(file);
	struct thread *curr = thread_current();

    if (!strcmp(file, "/")) {
        return false;
    }

    char **path = parse_directory(file);
    struct dir *cur_dir = curr->cur_dir;
    struct dir *target_dir = find_target_dir(path);
    struct inode *target_inode;
    if (target_dir == NULL)
        return false;
    curr->cur_dir = target_dir;

    int i;
    for (i = 0; path[i] != NULL; i++)
        ;

    char *file_name = path[i - 1];

    if (!dir_lookup(target_dir, file_name, &target_inode)) {
        curr->cur_dir = cur_dir;
        dir_close(target_dir);
        return false;
    }

    if (cur_dir != NULL && dir_get_inode(cur_dir) == target_inode) {
        curr->cur_dir = cur_dir;
        return false;
    }

    if (cur_dir != NULL && dir_get_inode(get_parent_dir(cur_dir)) == target_inode) {
        curr->cur_dir = cur_dir;
        return false;
    }

    lock_acquire(&filesys_lock);
    bool remove_result = filesys_remove(file_name);
    lock_release(&filesys_lock);

    curr->cur_dir = cur_dir;
    dir_close(target_dir);
    return remove_result;
}


	/****************** before project 4 ******************/

	// /* 성공 : True, 실패 : False */
	// check_address(file);
	// return filesys_remove(file);

}

/* Project 2 : Process structure (fork) */

tid_t fork (const char *thread_name, struct intr_frame *f) {
	// check_address(thread_name);
	// lock 걸어야하나?
	return process_fork(thread_name, f);// child thread namedl 들어온다
	// lock 걸어야하나?
}

/* Project 2 : Process structure (exec, wait) */
// ? 클론한 파일에서는 size를 PGSIZE로 줌
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
	return process_wait(pid);
}

/* Project 2 : File Descriptor */

struct file *find_file_by_fd(int fd)
{
	if (fd < 0 || fd >= FDCOUNT_LIMIT){
		return NULL;
	}
	
	struct thread *cur = thread_current();
	
	return cur->fd_table[fd];
}

/* 
파일 객체(struct File)를 File Descriptor 테이블에 추가
현재 스레드의 fd_idx(file descriptor Table index 끝값) 반환
*/
int add_file_to_fdt(struct file *file)
{
    struct thread *cur = thread_current();
    struct file **fdt = cur->fd_table;

    // Find open spot from the front
    // fd 위치가 제한 범위 넘지않고, fd table의 인덱스 위치와 일치한다면
    while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
    {
        cur->fd_idx++;
    }

    // error - fd table full
    if (cur->fd_idx >= FDCOUNT_LIMIT){
        return -1;
	}
    fdt[cur->fd_idx] = file;
    return cur->fd_idx;
}

void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT){
		return;
	}

	cur->fd_table[fd] = NULL;

}
/*
open(file) -> filesys_open(file) -> file_open(inode) -> file
open 함수 실행 -> filesys_open을 통해서 file open 함수에 inode를 넣고 실행하여 file을 반환받음
inode는 우리가 입력한 파일 이름을 컴퓨터가 알고 있는 파일이름으로 바꾸는 과정
file_obj = file이 되고, 이를 현재 스레드 파일 디스크립터 테이블에 추가하여 관리할 수 있게함
*/
// todo: 디렉토리도 오픈할 수 있도록 수정 
int open(const char *file) // 파일 객체에 대한 파일 디스크립터 부여
{
	check_address(file);
	struct thread *curr = thread_current();

    if (*file == NULL)
        return -1;

    struct file_unioned *file_unioned = (struct file_unioned *)malloc(sizeof(struct file_unioned));
    if (!strcmp(file, "/")) {
        file_unioned->dir = dir_open_root();
        file_unioned->file = NULL;
        return process_add_file(file_unioned);
    }

    char **path = parse_directory(file);
    struct dir *cur_dir = curr->cur_dir;
    struct dir *target_dir = find_target_dir(path);
    struct inode *target_inode;
    if (target_dir == NULL)
        return -1;
    curr->cur_dir = target_dir;

    int i;
    for (i = 0; path[i] != NULL; i++)
        ;
    char *file_name = path[i - 1];

    void *f;
    if (dir_lookup(target_dir, file_name, &target_inode)) {
        if (inode_is_dir(target_inode)) {
            lock_acquire(&filesys_lock);
            f = dir_open(target_inode);
            if (get_parent_dir(f) == NULL)
                set_parent_dir(f, dir_open_root());
            lock_release(&filesys_lock);
            if (f == NULL) {
                goto open_error;
            }
            file_unioned->dir = (struct dir *)f;
            file_unioned->file = NULL;
        } else if (!inode_is_dir(target_inode) && file[strlen(file) - 1] == '/') {
            goto open_error;
        } else {
            lock_acquire(&filesys_lock);
            f = filesys_open(file_name);
            lock_release(&filesys_lock);

            if (f == NULL) {
                goto open_error;
            }

            f += 0x8000000000;
            file_unioned->file = (struct file *)f;
            file_unioned->dir = NULL;
        }
    } else {
        goto open_error;
    }

    curr->cur_dir = cur_dir;
    dir_close(target_dir);

    return process_add_file(file_unioned);

open_error:
    curr->cur_dir = cur_dir;
    dir_close(target_dir);
    return -1;


	/****************** before project 4 ******************/

	// check_address(file);
	// lock_acquire(&filesys_lock);

	// struct file *file_obj = filesys_open(file);

	// if (file_obj == NULL){
	// 	return -1;
	// }

	// int fd = add_file_to_fdt(file_obj); 

	// /* 파일 디스크립터가 가득 찬 경우 */
	// if(fd==-1){
	// 	file_close(file_obj);
	// }

	// lock_release(&filesys_lock);
	// return fd;

}

/* 파일이 열려있다면 바이트 반환, 없다면 -1 반환 */
// todo: 수정
int filesize(int fd)
{
    void *f = process_get_file(fd);

    if (f == NULL)
        return -1;
    f += 0x8000000000;

    struct file_unioned *file = (struct file_unioned *)f;

    if (file->file == NULL)
        return -1;

    lock_acquire(&filesys_lock);
    int length_result = (int)file_length(file->file);
    lock_release(&filesys_lock);
    return length_result;



	/****************** before project 4 ******************/

	// struct file *file_obj = find_file_by_fd(fd);
	

	// if (file_obj == NULL){
	// 	return -1;
	// }
	// return file_length(file_obj);

}

/*
열린 파일의 데이터를 읽는 시스템 콜
- 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 
- 파일 디스크립터를 이용하여 파일 객체 검색
- 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에 저장 후, 버퍼의 저장한 크기를 리턴 (input_getc() 이용)
- 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기만큼 저장 후 읽은 바이트 수를 리턴

참고 :
NULL : 널 포인터 0x00000000 : 값이 없다, 비어 있다 의미 : pointer(void * ) 0
0 : 정수 0 : int
-> 포인터에서는 NULL, 0 같이 쓰인다!
*/

int read(int fd, void *buffer, unsigned size)
{	
	
	check_address(buffer);
	check_address(buffer+size-1);
	check_buf(buffer);
	int read_count; // 글자수 카운트 용(for문 사용하기 위해)
	struct thread *cur = thread_current();
	struct file *file_obj = find_file_by_fd(fd);
	unsigned char *buf = buffer;

	if (file_obj == NULL)
		return -1;
	
	/* STDIN일 때 */
	if(file_obj == STDIN)
	{
		char key;
		for (int read_count = 0;read_count < size; read_count++){
			key = input_getc();
			*buf++ = key;
			if (key == '\0'){
				break;
			}
		}
		
	}
	/* STDOUT일 때 : -1 반환 */
	else if (file_obj == STDOUT)
	{
		return -1;
	}
	
	else {
			lock_acquire(&filesys_lock);
			read_count = file_read(file_obj,buffer, size); // 여기서만 lock을 이용하는 이유?, 키보드 입력 받을 때는 왜 안하는지?
			lock_release(&filesys_lock);
	}

	
	return read_count;


}

int write(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	// check_buf(buffer);
	int read_count; // 글자수 카운트 용(for문 사용하기 위해)
	struct file *file_obj = find_file_by_fd(fd);
	unsigned char *buf = buffer;

	if (file_obj == NULL)
		return -1;
	
	/* STDOUT일 때 */
	if(file_obj == STDOUT)
	{
		putbuf(buffer, size); // fd값이 1일 때, 버퍼에 저장된 데이터를 화면에 출력(putbuf()이용)
		read_count = size;
		
	}
	/* STDIN일 때 : -1 반환 */
	else if (file_obj == STDIN)
	{
		
		return -1;
	}
	
	else {
			lock_acquire(&filesys_lock);
			read_count = file_write(file_obj,buffer, size);
			lock_release(&filesys_lock);
	}
	return read_count;

}

/* 파일의 위치(offset)를 이동하는 함수 */
// ? lock 걸어야 하나?
void seek(int fd, unsigned position)
{
	
	struct file *file_obj = find_file_by_fd(fd);
	if (fd < 2)
		return;
	// fileobj->pos = position;
	file_seek(file_obj, position);	
}



/* 파일의 시작점부터 현재 위치까지의 offset을 반환 */
// ? lock 필요?
unsigned tell(int fd)
{
	struct file *file_obj = find_file_by_fd(fd);
	if (fd < 2){
	   return;
	}
	return file_tell(file_obj);
}

void close(int fd)
{
	if (fd <= 1) return;
	struct file *file_obj = find_file_by_fd(fd);
	
	
	if (file_obj == NULL)
	{
		return;
	}

	remove_file_from_fdt(fd);


}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	struct thread *curr = thread_current ();
	if (is_kernel_vaddr (addr)) return NULL;
	// todo : the file descriptors representing console input and output are not mappable.
	if (fd < 2) return NULL;
	// todo : A call to mmap may fail if the file opened as fd has a length of zero bytes.
	if (filesize (fd) <= 0) return NULL;
	// todo : It must fail if addr is not page-aligned 
	if (pg_ofs (addr) != 0) return NULL;
	// printf("	pg_ofs (addr) != 0 : %d\n", pg_ofs (addr));
	if (pg_ofs (offset) != 0) return NULL;
	// printf("	pg_ofs (offset) != 0 : %d\n", pg_ofs (offset));
	// todo : if addr is 0, it must fail, because some Pintos code assumes virtual page 0 is not mapped.
	if (addr == 0) return NULL;
	// todo : Your mmap should also fail when length is zero.
	if ((long)length <= 0) return NULL;
	// // todo : length가 page align인지 확인
	// if (length % PGSIZE != 0) return NULL;
	struct file *file = find_file_by_fd (fd);
	if (file == NULL) return NULL;
	// printf ("	im in mmap! %p\n", addr);
	// printf("	fd : %d\n", fd);
	return do_mmap (addr, length, writable, file, offset);
}

void munmap (void *addr) {
	do_munmap (addr);
} 

/* 경로 분석 함수
 * name을 분석하여 파일, 디렉토리의 이름을 배열로 저장 */
static char **parse_directory(const char *name){
	// 저장할 메모리 할당받고
	char *name_copied = (char *)malloc((strlen(name) + 1) * sizeof(char));
    char **directories = (char **)calloc(128, sizeof(char *));
    // 복사해놓고
	strlcpy(name_copied, name, strlen(name) + 1);
    directories[0] = name_copied;

    int argc = 1;
    char *tmp;
	// 첫 위치 자르고
    char *token = strtok_r(name_copied, "/", &tmp);
    while (token != NULL) {
		// 안나올때까지
        directories[argc] = token + 0x8000000000;
        argc++;
		// 잘라서 담기
        token = strtok_r(NULL, "/", &tmp);
    }

	// 경로상의 이름들을 배열로 리턴
    return directories;
}

static struct dir *find_target_dir(const char **path){
    struct dir *curr_dir;
    struct inode *target_inode;
	struct thread *curr = thread_current();
	// 절대경로 or 상대경로에 따른 디렉토리 정보 저장
    if (path[0][0] == '/' || curr->cur_dir == NULL) {
        curr_dir = dir_open_root();				// 절대경로인 경우
    } else {
        curr_dir = dir_reopen(curr->cur_dir);	// 상대경로인 경우
    }

    int j;
    for (j = 0; path[j] != NULL; j++)
        ;

	// 현재 path[j]는 NULL이고, 그 전까지 i를 반복
    for (int i = 1; i < j - 1; i++) {
		// curr_dir에서 path[i]를 검색하고 찾으면 inode를 target_inode에 저장(true) 아니면 NULL 저장(false)
		// 갱신되는 curr_dir에서 즉, 하위 디렉토리에서 다음 디렉토리를 찾음
        if (dir_lookup(curr_dir, path[i], &target_inode)) {
            if (!inode_is_dir(target_inode)) {	// inode가 디렉토리가 아닌 경우 	
                return NULL;					// NULL 반환
            }
            dir_close(curr_dir);				// curr_dir에 들어있던 거 닫고 (디렉토리 정보를 메모리에서 해지)
            curr_dir = dir_open(target_inode);	// target_inode의 디렉토리 정보를 다시 저장
        } else
            return NULL;
    }

    return dir_reopen(curr_dir);
}


/* 
 * 현재 실행 중인 프로세스의 디렉토리를 dir로 변경
 * dir은 절대경로 or 상대경로 이며
 * 성공 시 true를, 실패 시 false를 반환 */
static bool chdir(const char *dir){
	// todo: dir을 copy해놓고
	// todo: 절대경로 or 상대경로 에 따라 앞부분 정해서
	// todo: dir 분석 후 디렉토리 반환
	// todo: dir에서 이름 검색해서 inode의 정보 저장

	check_address(dir);
	struct thread *curr = thread_current();

	if(*dir == NULL){									// 포인터가 NULL인 경우
		return false;
	}

	if(!strcmp(dir, "/")){								// root
		curr->cur_dir = dir_open_root();
		return true;
	}

	if(!strcmp(dir, ".")){								// 현재 디렉토리
		return true;
	}

	if(!strcmp(dir, "..")){								// 상위 디렉토리
		curr->cur_dir = get_parent_dir(curr->cur_dir);
		return true;
	}

	char **path = parse_directory(dir); 				// 경로에 포함된 디렉토리의 배열
	struct dir *target_dir = find_target_dir(path);		// path 끝에 있는 디렉토리 open
	struct inode *target_inode;
	if(target_dir == NULL)
		return false;
	
	int i;
	for(i=1; path[i]!=NULL; i++){
		;
	}
	char *file_name = path[i-1];
	// target_dir에서 file_name 검색해서 있으면 target_inode 갱신하고 true 반환
	if(!dir_lookup(target_dir, file_name, &target_inode)){
		dir_close(target_dir);
		return false;
	}

	// target_inode로 open해서 현재 디렉토리로 설정
	curr->cur_dir = dir_open(target_inode);
	// 상위디렉토리가 NULL이면 root로 설정
	if(get_parent_dir(curr->cur_dir) == NULL)
		set_parent_dir(curr->cur_dir, dir_open_root);
	return true;
}

/* 새로운 디렉토리를 생성 */
static bool mkdir(const char *dir){
	check_address(dir);

	if(*dir == NULL){
		return false;
	}

	struct thread *curr = thread_current();
	char **path = parse_directory(dir);					// 경로에 포함된 디렉토리의 배열 
	struct dir *curr_dir = curr->cur_dir;
	struct dir *target_dir = find_target_dir(path);		// path 끝에 있는 디렉토리 open
	struct inode *target_inode;

	if(target_dir == NULL){
		return false;
	}

	curr->cur_dir = target_dir;

	int i;
	for(i=1; path[i]!=NULL; i++){
		;
	}

	char *file_name = path[i-1];

	// target_dir에서 file_name 검색해서 있으면 target_inode 갱신하고 true 반환
	if(dir_lookup(target_dir, file_name, &target_inode)){	// 있으면 실패
		curr->cur_dir = curr_dir;
		dir_close(target_dir);
		return false;
	}

	disk_sector_t inode_sector = 0;	
	fat_allocate(1, &inode_sector);
	
	lock_acquire(&filesys_lock);
	bool create_result = dir_create(inode_sector, 16);		// 디렉토리 생성
	if(create_result){
		dir_add(target_dir, file_name, inode_sector);		// target_dir에 file_name 추가\
	}
	lock_release(&filesys_lock);

	curr->cur_dir = curr_dir;
	dir_close(target_dir);

	return create_result;
}

/* 파일의 fd로부터 디렉토리 엔트리를 읽음
 * 성공하면 name에 NULL로 종료된 파일 이름을 저장(true 반환)
 * 이는 READDIR_MAX_LEN + 1byte의 공간이 필요함
 * 디렉토리에 엔트릭가 없다면 false 반환
 ! '.'과 '..'은 반환될 수 없음
 * 만약 디렉토리가 열려있을 때 디렉토리를 변경하면, 
 * 일부 엔트리는 아예 읽을 수 없거나 중복되어 읽힐 수 있음
 * 그렇지 않은 경우, 각 디렉토리 엔트리는 
 * 순서에 상관없이 한 번 읽혀야 함 */
static bool readdir(int fd, char *name){
	check_address(name);
	
	// fd table에서 fd에 대한 파일 정보를 얻어옴
	struct file *target = find_file_by_fd(fd);
	if(target == NULL){
		return -1;
	}

	// fd의 file->inode가 디렉터리인지 검사
    if (!inode_is_dir(file_get_inode(target))) {
        return false;
	}

    // p_file을 dir자료구조로 포인팅
    struct dir *p_file = target;
    if (p_file->pos == 0) {
        dir_seek(p_file, 2 * sizeof(struct dir_entry));		// ".", ".." 제외
	}

    // 디렉터리의 엔트리에서 ".", ".." 이름을 제외한 파일이름을 name에 저장
    bool result = dir_readdir(p_file, name);

    return result;

	/****************** pintos-kaist ******************/
	
    // check_address(name);
    // void *f = process_get_file(fd);

    // if (f == NULL)
    //     return -1;

    // f += 0x8000000000;
    // struct file_unioned *file = (struct file_unioned *)f;

    // if (file->dir == NULL)
    //     return false;

    // return dir_readdir(file->dir, name);

}

/* fd가 디렉토리이면 true, 일반 파일이면 false를 반환 */
static bool isdir(int fd){

	struct file *target = find_file_by_fd(fd);

    if (target == NULL) {
        return false;
	}

    return inode_is_dir(file_get_inode(target));

	/****************** pintos-kaist ******************/

	// void *f = process_get_file(fd);

    // if (f == NULL)
    //     return -1;

    // f += 0x8000000000;
    // struct file_unioned *file = (struct file_unioned *)f;

    // return file->dir != NULL;
}

/* 디렉토리인지 파일인지 나타내는
 * fd와 연관된 inode번호(inode->sector)를 반환 */
static int inumber(int fd){
	struct file *target = find_file_by_fd(fd);

    if (target == NULL) {
        return false;
	}

    return inode_get_inumber(file_get_inode(target));

	/****************** pintos-kaist ******************/
    
	// void *f = process_get_file(fd);

    // if (f == NULL)
    //     return -1;

    // f += 0x8000000000;
    // struct file_unioned *file = (struct file_unioned *)f;

    // if (file->file != NULL)
    //     return (int)inode_get_inumber(file_get_inode(file->file));

    // else if (file->dir != NULL)
    //     return (int)inode_get_inumber(dir_get_inode(file->dir));

    // return -1;
}

/* linkpath에 대한 바로가기를 생성
 * 성공 시 0을, 실패 시 -1을 반환 */
static int symlink(const char *target, const char *linkpath){

    if (*target == NULL)
        return -1;

    if (*linkpath == NULL)
        return -1;

	// linkpath의 경로를 분석해서 배열로 변환
    char **link_path = parse_directory(linkpath);
    struct dir *link_target_dir = find_target_dir(link_path);
    if (link_target_dir == NULL)
        return -1;

    int i;
    for (i = 1; link_path[i] != NULL; i++)
        ;
    char *link_name = link_path[i - 1];

    char **path = parse_directory(target);
    struct dir *target_dir = find_target_dir(path);
    struct inode *target_inode;
    if (target_dir == NULL)
        return -1;

    for (i = 1; path[i] != NULL; i++)
        ;
    char *file_name = path[i - 1];

    if (!dir_lookup(target_dir, file_name, &target_inode)) {
        struct sym_map *sym_map = (struct sym_map *)malloc(sizeof(struct sym_map));
        char *target_cp = malloc(sizeof(strlen(target)) + 1);
        char *link_cp = malloc(sizeof(strlen(linkpath)) + 1);
        strlcpy(target_cp, target, strlen(target) + 1);
        strlcpy(link_cp, linkpath, strlen(linkpath) + 1);
        sym_map->target = target_cp;
        sym_map->linkpath = link_cp;
        list_push_back(&sym_list, &sym_map->sym_elem);
        dir_close(target_dir);
        return 0;
    }

    dir_add(link_target_dir, link_name, inode_get_inumber(target_inode));
    return 0;

}

static int mount(const char *path, int chan_no, int dev_no){

}

static int umount(const char *path){

}

