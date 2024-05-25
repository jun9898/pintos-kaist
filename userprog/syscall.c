#include "userprog/syscall.h"

#include <devices/input.h>
#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "lib/kernel/stdio.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void is_valid_addr(void *addr);
void halt(void);  // 0
void exit(int status);
bool create(const char *file, unsigned initial_size);
int open(const char *file);
bool remove(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
int exec(const char *cmd_line);

#define EOF -1
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

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
  lock_init(&file_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
  // TODO: Your implementation goes here.
  printf("system call!\n");
  uintptr_t syscall_code = f->R.rax;

  switch (syscall_code) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(f->R.rdi);
      break;
    // case SYS_FORK:
    // 	break;
    // case SYS_EXEC:
    // 	break;
    // // case SYS_WAIT:
    // 	wait(f->R.rdi);
    // 	break;
    case SYS_CREATE:
      check_address(f->R.rdi);
      f->R.rax = create(f->R.rdi, f->R.rsi);
      break;
    case SYS_REMOVE:
      check_address(f->R.rdi);
      f->R.rax = remove(f->R.rdi);
      break;
    case SYS_OPEN:
      check_address(f->R.rdi);
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
  }
}

void check_address(void *addr) {
  struct thread *curr_thread = thread_current();
  if (addr == NULL || is_kernel_vaddr(addr) ||
      pml4_get_page(curr_thread->pml4, addr) == NULL) {
    exit(-1);
  }
}

void halt(void) { power_off(); }

void exit(int status) {
  struct thread *cur = thread_current();
  printf("%s: exit(%d)", cur->name, cur->status);
  thread_exit();
}
int fork(const char *thread_name) { return; }
int exec(const char *file) { return; }
int wait(int t) { return process_wait(t); }

bool create(const char *file, unsigned initial_size) {
  if (file == NULL) exit(-1);
  return filesys_create(file, initial_size);
}

bool remove(const char *file) { return filesys_remove(file); }

int open(const char *file) {
  struct file *tmp;
  int fd = -1;
  tmp = filesys_open(file);
  if (tmp == NULL) {
    return -1;
  }
  fd = process_add_file(tmp);

  if (fd == -1) {
    file_close(tmp);  // file.c
  }
  return fd;
}

int filesize(int fd) {
  struct file *f = process_get_file(fd);
  if (f == NULL) {
    return -1;
  }
  return (int)file_length(f);
}

/* 열린 파일의 데이터를 읽는 시스템 콜 */
int read(int fd, void *buffer, unsigned size) {
  // fd 값이 0 -> 입력
  int bytes_read = 0;
  if (fd == 0) {
    unsigned i;
    // lock_acquire(&file_lock);
    for (i = 0; i < size; i++) {
      int c = input_getc();
      if (c == EOF) break;
      ((char *)buffer)[i] = (char)c;
      // lock_release(&file_lock);
    }
    return bytes_read;
  }
  // read
  struct file *f = process_get_file(fd);

  if (f == NULL) return -1;

  // lock_acquire(&file_lock);
  bytes_read = file_read(f, buffer, size);
  // lock_release(&file_lock);
  return bytes_read;
}

int write(int fd, const void *buffer, unsigned size) {
  // 열린 파일의 데이터를 기록 시스템 콜
  // 성공 시 기록한 데이터의 바이트 수를 반환, 실패시 -1 반환
  // buffer 기록 할 데이터를 저장한 버퍼의 주소 값
  // size 기록할 데이터 크기
  // fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력 (putbuf() 이용)
  int writes = 0;
  // fd 값이 1 -> 출력
  if (fd == 1) {
    // lock_acquire(&file_lock);
    putbuf(buffer, size);
    return size;
    // lock_release(&file_lock);
  } else if (fd < 2) {
    return -1;
  }
  // write
  struct file *f = process_get_file(fd);

  if (f == NULL) return -1;
  // lock_acquire(&file_lock);
  writes = file_write(f, buffer, size);
  // lock_release(&file_lock);
  return writes;
}
/* 파일의 위치(offset)를 이동하는 시스템 콜 */
void seek(int fd, unsigned position) {
  if (fd < 2) return;
  struct file *f = process_get_file(fd);
  if (f == NULL) return;

  file_seek(f, position);
}

unsigned tell(int fd) {
  struct file *f = process_get_file(fd);
  return file_tell(f);
}

void close(int fd) {
  struct file *f = process_get_file(fd);
  if (f == NULL) return;
  process_close_file(fd);
}