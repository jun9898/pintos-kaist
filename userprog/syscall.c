#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(char *addr);

// File synch
struct lock filesys_lock;

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

	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
#ifdef VM
	thread_current()->rsp = f->rsp;
#endif
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	// case SYS_FORK:
	// 	f->R.rax = fork(f->R.rdi, f);
	// 	break;
	// case SYS_EXEC:
	// 	f->R.rax = exec(f->R.rdi);
	// 	break;
	// case SYS_WAIT:
	// 	f->R.rax = wait(f->R.rdi);
	// 	break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	// case SYS_MMAP:
	// 	f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
	// 	break;
	// case SYS_MUNMAP:
	// 	munmap(f->R.rdi);
	// 	break;
	}
}


// Implements
void
check_address(char *addr) {
	struct thread* cur = thread_current();
	if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL) exit(-1);
}

void
halt(void) {
	power_off();
}

void
exit(int status) {
	struct thread *cur = thread_current();
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

bool 
create(const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

bool
remove(const char *file) {
	check_address(file);
	return filesys_remove(file);
}

int 
open (const char *file) {
    check_address(file);
    struct file *_file = filesys_open(file);

    if (_file == NULL)
        return -1;

    int fd = process_add_file(_file);

    if (fd == -1)
        file_close(_file);

    return fd;
}

int 
filesize (int fd) {
	struct file *file = process_get_file(fd);
	return file_length(file);
}

int
read (int fd, void *buffer, unsigned size) {
	int count;
	check_address(buffer);
	unsigned char *bufp = buffer;

	if (fd == 0) {
		char key;
		for (int i = 0; i < size; count++) {
				key  = input_getc();
				*bufp++ = key;
			if (key == '\0') 
				break;
		}
		count += 1;
	} else {
		lock_acquire(&filesys_lock);
		struct file* file = process_get_file(fd);
		count = file_read(file, buffer, size);
	}
	
	lock_release(&filesys_lock);
	return count;
}

int
write (int fd, const void *buffer, unsigned size) {
	int count;
	check_address(buffer);
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	struct file* file = process_get_file(fd);
	if (file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}
	
	count = file_write(file, buffer, size);
	
	lock_release(&filesys_lock);
	return count;
}

void seek(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_seek(file, position);
}

unsigned tell(int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	return file_tell(file);
}

void close(int fd)
{
	process_close_file(fd);
}

