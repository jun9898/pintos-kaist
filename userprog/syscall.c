#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "include/userprog/process.h"
#include "include/devices/input.h"
#include "include/lib/kernel/stdio.h"
#include "include/threads/synch.h"
#include "include/filesys/file.h"

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

struct lock file_lock;

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
	printf ("system call!\n");
	uintptr_t syscall_code = f->R.rax;

	switch(syscall_code) {

		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit((int) f->R.rdi);
			break;
		case SYS_FORK:
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT:
			break;
		case SYS_CREATE:
			create((char *) f->R.rdi, (unsigned int) f->R.rsi);
			break;
		case SYS_REMOVE:
			remove((char *) f->R.rdi);
			break;
		case SYS_OPEN:
			open((char *) f->R.rdi);
			break;
		case SYS_FILESIZE:
			filesize((int) f->R.rdi);
			break;
		case SYS_READ:
			read((int)f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case SYS_WRITE:
			write((int)f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case SYS_SEEK:
			seek((int)f->R.rdi,(unsigned)f->R.rsi);
			break;
		case SYS_TELL:
			tell((int)f->R.rdi);
			break;
		case SYS_CLOSE:
			close((int)f->R.rdi);
			break;
	}
}

void
halt(void){
	power_off();
}

void
exit(int status){
	struct thread *cur = thread_current();
	printf("%s: exit(%d)", cur->name, cur->status);
	thread_exit ();
}

bool create (const char *file, unsigned initial_size){
	return filesys_create(file, initial_size);
}

bool remove (const char *file){
	return filesys_remove(file);
}

int open(const char *file){
	struct file *f = filesys_open(file);
	if (f == NULL) {
        return -1; 
    }
	return process_add_file(f);
}

int filesize (int fd){
	struct file *f = process_get_file(fd);
	if (f == NULL) {
        return -1; 
    }
	return (int) file_length(f);
}

/* 열린 파일의 데이터를 읽는 시스템 콜 */
int read(int fd, void *buffer, unsigned size){
	lock_init (&file_lock);
	struct file *f = process_get_file(fd);

	if (f == NULL) return -1;

	// fd 값이 0 -> 입력
	if ( fd == 0 ){
		unsigned i;
		lock_acquire(&file_lock);
		for (i = 0; i < size; i++){
			((char *)buffer)[i] = input_getc();
			lock_release (&file_lock);
		}
	}
	// read
	lock_acquire(&file_lock);
	int res_read = (int)file_read(f, buffer, size);
	lock_release (&file_lock);
    return res_read;
}

int write(int fd, const void *buffer, unsigned size){
	// 열린 파일의 데이터를 기록 시스템 콜
	// 성공 시 기록한 데이터의 바이트 수를 반환, 실패시 -1 반환
	// buffer 기록 할 데이터를 저장한 버퍼의 주소 값
	// size 기록할 데이터 크기
	// fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력 (putbuf() 이용)
	lock_init (&file_lock);
	struct file *f = process_get_file(fd);
	
	if (f == NULL) return -1;

	// fd 값이 1 -> 출력
	if ( fd == 1){
		lock_acquire(&file_lock);
		putbuf(buffer, size);
		lock_release (&file_lock);
	}
	// write
	lock_acquire(&file_lock);
	int res_write = (int) file_write(f, buffer, size);
	lock_release (&file_lock);
	return res_write;
}
/* 파일의 위치(offset)를 이동하는 시스템 콜 */
void seek (int fd, unsigned position){
	struct file *f = process_get_file(fd);
	file_seek(f, position);
}

unsigned tell (int fd){
	struct file *f = process_get_file(fd);
	return (unsigned) file_tell(f);
}

void close(int fd){
	process_close_file(fd);
}