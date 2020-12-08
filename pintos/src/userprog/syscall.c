#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static struct lock filesys_lock;

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

int write(int fd, const void* buffer, unsigned size) {
  if (fd == 1)
    return printf("%.*s", size, (const char*)buffer);
  return -1;
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

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
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

      default:
        break;
    }

    lock_release(&filesys_lock);
  } else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }
}
