#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/pte.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static struct lock filesys_lock;

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

bool check_memory(void* start, int size) {
  if (start + size > PHYS_BASE)
    return false;
  if (start < 0x08048000)
    return false;
  struct thread* t = thread_current();

  for (uintptr_t i = pd_no(start); i <= pd_no(start + size); i++) {
    uint32_t* pde = t->pagedir + i;
    // printf("%x %x\n", pde, t->pagedir);
    if (*pde == 0)
      return false;
  }
  return true;
}

bool check_memory_str(char* start) {
  int sz = 0;
  while (start[sz++])
    ;
  return check_memory(start, sz);
}

void bad_exit() {
  printf("%s: exit(-1)\n", &thread_current()->name);
  thread_exit();
}

struct file* fd_to_file(int fd) {
  /* data */
  struct file_descriptor* ds = NULL;
  struct list_elem* e;

  for (e = list_begin(&thread_current()->files_lst); e != list_end(&thread_current()->files_lst);
       e = list_next(e)) {
    ds = list_entry(e, struct file_descriptor, elem);
    if (ds->fd == fd)
      break;
  }

  if (ds == NULL)
    bad_exit();
  return ds->fp;
};

int file_add(struct file* fp) {
  if (fp == NULL)
    return -1;
  int fd = (thread_current()->file_allocd++);
  struct file_descriptor* ds = malloc(sizeof(struct file_descriptor));
  ds->fd = fd;
  ds->fp = fp;
  list_push_front(&thread_current()->files_lst, &ds->elem);
  return fd;
}

int write(int fd, const void* buffer, unsigned size) {
  if (fd == 1)
    return printf("%.*s", size, (const char*)buffer);

  return file_write(fd_to_file(fd), buffer, size);
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (!check_memory(args, 4)) {
    printf("%s: exit(-1)\n", &thread_current()->name);

    thread_exit();
  }

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    set_exit_code(args[1]);
    printf("%s: exit(%d)\n", &thread_current()->name, args[1]);
    thread_exit();
  } else if (args[0] == SYS_WRITE || args[0] == SYS_CREATE || args[0] == SYS_REMOVE ||
             args[0] == SYS_OPEN || args[0] == SYS_FILESIZE || args[0] == SYS_READ ||
             args[0] == SYS_SEEK || args[0] == SYS_TELL || args[0] == SYS_CLOSE) {
    // filesys is not thread-safe
    lock_acquire(&filesys_lock);

    switch (args[0]) {
      case SYS_WRITE:
        f->eax = write(args[1], args[2], args[3]);
        break;
      case SYS_CREATE:
        f->eax = filesys_create(args[1], args[2]);
        break;
      case SYS_OPEN:
        f->eax = file_add(filesys_open(args[1]));
        break;

      case SYS_FILESIZE:
        f->eax = file_length(fd_to_file(args[1]));
        // TODO
        break;

      case SYS_READ:
        f->eax = file_read(fd_to_file(args[1]), args[2], args[3]);
        break;

      case SYS_SEEK:
        file_seek(fd_to_file(args[1]), args[2]);
        break;

      case SYS_TELL:
        f->eax = file_tell(fd_to_file(args[1]));
        break;

      case SYS_CLOSE:
        file_close(fd_to_file(args[1]));
        break;

      case SYS_REMOVE:
        f->eax = filesys_remove(args[1]);
      default:
        break;
    }

    lock_release(&filesys_lock);
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  } else if (args[0] == SYS_EXEC) {
    f->eax = process_execute(args[1]);

  } else if (args[0] == SYS_WAIT) {
    f->eax = process_wait(args[1]);
  }
}
