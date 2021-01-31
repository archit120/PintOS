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
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "threads/palloc.h"

static struct lock filesys_lock;

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void bad_exit(bool lock) {
  if (lock)
    lock_release(&filesys_lock);
  printf("%s: exit(-1)\n", &thread_current()->name);
  thread_exit();
}

bool check_memory(uint8_t* start, int size, bool lock) {
  if (start + size > PHYS_BASE)
    bad_exit(lock);
  if (start < 0x08048000)
    bad_exit(lock);
  struct thread* t = thread_current();
  uintptr_t target_pd_no = pd_no(start + size);
  uintptr_t target_pt_no = pt_no(start + size);
  uintptr_t current_pd_no = pd_no(start);
  uintptr_t current_pt_no = pt_no(start);

  for (; current_pd_no <= target_pd_no; current_pd_no++) {
    uint32_t* pde = t->pagedir + current_pd_no;
    if (*pde == 0)
      bad_exit(lock);
    uint32_t* pt = pde_get_pt(*pde);

    for (; (current_pt_no < 1024) &&
           ((current_pd_no < target_pd_no) || (current_pt_no <= target_pt_no));
         current_pt_no += 1) {
      // printf("pd: %d pg: %d \n", pd_no(current_pt), pg_no(current_pt));
      // printf("tpd: %d tpg: %d \n", target_pd_no, target_pg_no);

      if (!(pt[current_pt_no] & PTE_U || pt[current_pt_no] & PTE_P))
        bad_exit(lock); //page table does not exist or is not owned by user.
    }
    current_pt_no = 0;
  }
  return true;
}

void check_int(void* loc, bool lock) { check_memory(loc, 4, lock); }

bool check_memory_str(void* start_act, bool lock) {
  int sz = 0;
  check_int(start_act, lock);

  uint8_t* start = (*(uint8_t**)start_act);

  if (start < 0x08048000)
    bad_exit(lock);
  struct thread* t = thread_current();
  uintptr_t current_pd_no = pd_no(start);
  uintptr_t current_pt_no = pt_no(start);

  for (;; current_pd_no++) {
    uint32_t* pde = t->pagedir + current_pd_no;
    if (*pde == 0)
      bad_exit(lock);
    uint32_t* pt = pde_get_pt(*pde);
    for (; current_pt_no < 1024; current_pt_no += 1) {
      // printf("pd: %d pg: %d \n", pd_no(current_pt), pg_no(current_pt));
      // printf("tpd: %d tpg: %d \n", target_pd_no, target_pg_no);

      if (!(pt[current_pt_no] & PTE_U || pt[current_pt_no] & PTE_P))
        bad_exit(lock); //page table does not exist or is not owned by user.

      //current page exists so try looking for null character here.
      char* bckup = start;
      while ((uintptr_t)start >> 12 == (uintptr_t)bckup >> 12) // iterate only in same page
      {
        if (*start == 0)
          return true;
        start++;
      }
    }
  }
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
    bad_exit(true);
  if (ds->closed)
    bad_exit(true);
  return ds->fp;
}

void close_fd(int fd) {
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
    bad_exit(true);
  if (ds->closed)
    bad_exit(true);
  ds->closed = true;
  file_close(ds->fp);
}

int file_add(struct file* fp) {
  if (fp == NULL)
    return -1;
  int fd = (thread_current()->file_allocd++);
  struct file_descriptor* ds = malloc(sizeof(struct file_descriptor));
  ds->fd = fd;
  ds->fp = fp;
  ds->closed = false;
  list_push_front(&thread_current()->files_lst, &ds->elem);
  return fd;
}

int write(int fd, const void* buffer, unsigned size) {
  if (fd == 1)
    return printf("%.*s", size, (const char*)buffer);

  return file_write(fd_to_file(fd), buffer, size);
}

// int read(int fd, void* buffer, unsigned size) {
//   if()
// }
static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  check_int(args, false);
  if (args[0] == SYS_EXIT) {
    check_int(args + 1, false);
    f->eax = args[1];
    set_exit_code(args[1]);
    printf("%s: exit(%d)\n", &thread_current()->name, args[1]);
    thread_exit();
  } else if (args[0] == SYS_WRITE || args[0] == SYS_CREATE || args[0] == SYS_REMOVE ||
             args[0] == SYS_OPEN || args[0] == SYS_FILESIZE || args[0] == SYS_READ ||
             args[0] == SYS_SEEK || args[0] == SYS_TELL || args[0] == SYS_CLOSE ||
             args[0] == SYS_REMOVE || args[0] == SYS_INUMBER || args[0] == SYS_MKDIR ||
             args[0] == SYS_CHDIR || args[0] == SYS_ISDIR || args[0] == SYS_READDIR) {
    // filesys is not thread-safe
    lock_acquire(&filesys_lock);

    switch (args[0]) {
      case SYS_WRITE:
        check_int(args + 1, true);
        check_int(args + 3, true);
        check_memory_str(args + 2, true);
        f->eax = write(args[1], args[2], args[3]);
        break;
      case SYS_CREATE:
        check_memory_str(args + 1, true);
        check_int(args + 2, true);
        f->eax = filesys_create(args[1], args[2]);
        break;
      case SYS_OPEN:
        check_memory_str(args + 1, true);
        f->eax = file_add(filesys_open(args[1]));
        break;

      case SYS_FILESIZE:
        check_int(args + 1, true);
        f->eax = file_length(fd_to_file(args[1]));
        // TODO
        break;

      case SYS_READ:
        check_int(args + 1, true);
        check_int(args + 2, true);
        check_int(args + 3, true);

        check_memory(args[2], args[3], true);
        f->eax = file_read(fd_to_file(args[1]), args[2], args[3]);
        break;

      case SYS_SEEK:
        check_int(args + 1, true);
        check_int(args + 2, true);
        file_seek(fd_to_file(args[1]), args[2]);
        break;

      case SYS_TELL:
        check_int(args + 1, true);
        f->eax = file_tell(fd_to_file(args[1]));
        break;

      case SYS_CLOSE:
        check_int(args + 1, true);
        close_fd(args[1]);
        break;

      case SYS_REMOVE:
        check_memory_str(args + 1, true);
        f->eax = filesys_remove(args[1]);
        break;

      case SYS_INUMBER:
        check_int(args + 1, true);
        f->eax = inode_get_inumber(file_get_inode(fd_to_file(args[1])));
        break;

        // CASE SYS_CHDIR:
      case SYS_MKDIR:
        check_memory_str(args + 1, true);
        f->eax = mkdir(args[1]);
        break;

      case SYS_CHDIR:
        check_memory_str(args + 1, true);
        f->eax = chdir(args[1]);
        break;

      case SYS_ISDIR:
        check_int(args + 1, true);
        f->eax = inode_is_dir(file_get_inode(fd_to_file(args[1])));
        break;

      case SYS_READDIR:
        check_int(args + 1, true);
        check_int(args + 2, true);
        check_memory(args[2], 15, true);
        f->eax = userprog_readdir(fd_to_file(args[1]), args[2]);
        break;
      default:
        break;
    }

    lock_release(&filesys_lock);
  } else if (args[0] == SYS_PRACTICE) {
    check_int(args + 1, false);
    f->eax = args[1] + 1;
  } else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  } else if (args[0] == SYS_EXEC) {
    check_memory_str(args + 1, false);
    f->eax = process_execute(args[1]);

  } else if (args[0] == SYS_WAIT) {
    check_int(args + 1, false);
    f->eax = process_wait(args[1]);
  }
}
