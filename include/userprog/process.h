#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct file_metadata {
    struct file *file;
    uint32_t read_byte;
    uint32_t zero_byte;
    off_t offset;
};


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

#endif /* userprog/process.h */
