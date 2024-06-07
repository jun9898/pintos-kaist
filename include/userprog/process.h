#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H


#include "threads/thread.h"



tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

void process_exit_file(void);
int process_add_file_(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);

struct lazy_load_arg{
    struct file *file;  // 내용이 담긴 파일 객체
    off_t ofs;          // ofs 이 페이지에서 읽기 시작할 위치
    uint32_t read_bytes;// read_bytes 이 페이지에서 읽어야 하는 바이트 수
    uint32_t zero_bytes;// zero_bytes 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수
};

#endif /* userprog/process.h */