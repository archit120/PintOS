#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdbool.h>

/* A directory. */
struct dir {
  struct inode* inode; /* Backing store. */
  off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1];     /* Null terminated file name. */
  bool in_use;                 /* In use or free? */
  bool is_dir;                 /* Is entry a directory or not? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt, block_sector_t parent_sector) {
  bool success = inode_create(sector, (entry_cnt + 2) * sizeof(struct dir_entry), true);
  if (!success)
    return false;

  // directory was created succesfully. Add entries for "." and ".." files

  struct dir* dir = dir_open(inode_open(sector));
  if (!dir_add(dir, ".", sector, true) || !dir_add(dir, "..", parent_sector, true)) {
    dir_close(dir);
    return false;
  }
  dir_close(dir);
  return true;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir* dir_open(struct inode* inode) {
  struct dir* dir = calloc(1, sizeof *dir);
  if (inode != NULL && dir != NULL) {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  } else {
    inode_close(inode);
    free(dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir* dir_open_root(void) {
  return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir* dir_reopen(struct dir* dir) {
  return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir* dir) {
  if (dir != NULL) {
    inode_close(dir->inode);
    free(dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode* dir_get_inode(struct dir* dir) {
  return dir->inode;
}

/* Extracts a file name part from SRC into PART. Returns len if successful, 0 at
end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char* src) {
  char* dst = part;
  const char* osrc = src;
  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  return src - osrc;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir* dir, const char* name, struct dir_entry* ep, off_t* ofsp) {
  struct dir_entry e;
  size_t ofs;

  // if passed directory is root then check name to figure out absolute path or relative path.
  // If absolute path continues or else switch to a different dir*
  // also if we swticehd directories here then we must close.
  bool close_on_return = false;
  if (inode_get_inumber(dir_get_inode(dir)) == ROOT_DIR_SECTOR && name[0] != '/') {
    //printf("dir swap!\n");
    close_on_return = true;
    dir = dir_open(inode_open(thread_current()->current_working_dir));
  }

  //printf("LOOKUP: %s \n", name);
  ASSERT(dir != NULL);
  ASSERT(name != NULL);
  char part[NAME_MAX + 1];
  int nlen = get_next_part(part, name);
  if (nlen == 0)
    part[0] = '.', part[1] = NULL;
  if (nlen == -1)
    return false;
  name += nlen;
  //printf("LOOKUP2: %s\n", part);

  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
    //printf("SEE FILE: %s\n", e.name);
    if (e.in_use && !strcmp(part, e.name)) {
      int nlen = get_next_part(part, name);
      if (nlen == 0) {
        bool sdot = e.name[0] == '.' && e.name[1] == 0;
        bool ddot = e.name[0] == '.' && e.name[1] == '.' && e.name[2] == 0;

        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        if (close_on_return)
          dir_close(dir);
        return true;
      }
      if (nlen > 0 && e.is_dir) {
        if (close_on_return)
          dir_close(dir);

        dir = dir_open(inode_open(e.inode_sector));
        bool ret = lookup(dir, name, ep, ofsp);
        dir_close(dir);
        return ret;
      }
    }
  }
  if (close_on_return)
    dir_close(dir);

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir* dir, const char* name, struct inode** inode) {
  struct dir_entry e;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  if (lookup(dir, name, &e, NULL))
    *inode = inode_open(e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

bool subdir_lookup(struct dir* dir, const char* name, struct inode** res, char* temp) {
  int len = strlen(name);
  int x = len - 1;
  while (name[x] != '/' && x > 0)
    x--;

  if (x == 0) {
    if (name[x] == '/')
      x++;
    for (int i = x; i <= len; i++)
      temp[i - x] = name[i];
    if (name[0] == '/')
      *res = inode_open(ROOT_DIR_SECTOR);
    else
      *res = inode_open(inode_get_inumber(dir->inode));
    return true;
  }

  int y = x;
  for (int i = 0; i < x; i++)
    temp[i] = name[i];
  temp[x] = NULL;
  bool sdot = temp[0] == '.' && temp[1] == 0;
  bool ddot = temp[0] == '.' && temp[1] == '.' && temp[2] == 0;

  struct inode* inode = NULL;
  //printf("ADD0: %s\n", temp);
  if (dir != NULL) {
    if (!dir_lookup(dir, temp, &inode)) {
      //printf("ADD1: %s\n", temp);

      dir_close(dir);
      return false;
    }
  } else {
    //printf("ADD2: %s\n", temp);

    return false;
  }
  if (sdot || ddot || temp[0] == 0)
    return false;

  *res = inode;
  if (name[x] == '/')
    x++;
  for (int i = x; i <= len; i++)
    temp[i - x] = name[i];

  return true;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector, bool is_dir) {
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen(name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup(dir, name, NULL, NULL))
    goto done;

  struct inode* inode;
  char temp[NAME_MAX + 1];
  //printf("SADDIT: %s\n", name);

  if (!subdir_lookup(dir, name, &inode, temp))
    goto done;
  dir = dir_open(inode);

  //printf("ADDIT: %s %s %d\n", name, temp, inode_get_inumber(dir->inode));
  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  e.is_dir = is_dir;
  strlcpy(e.name, temp, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
  dir_close(dir);
done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir* dir, const char* name) {
  struct dir_entry e;
  struct inode* inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
    goto done;

  if (e.inode_sector == thread_current()->current_working_dir)
    goto done;

  /* Open inode. */
  inode = inode_open(e.inode_sector);

  if (inode_is_dir(inode)) {
    off_t ofs2;
    // check if directory has any files except . and ..
    for (ofs2 = 0; inode_read_at(dir->inode, &e, sizeof e, ofs2) == sizeof e; ofs2 += sizeof e)
      if (e.in_use &&
          !(e.name[0] == '.' && (e.name[1] == '\0' || (e.name[1] == '.' && e.name[2] == '\0'))))
        goto done;
  }

  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove(inode);
  success = true;

done:
  inode_close(inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]) {
  struct dir_entry e;

  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
    dir->pos += sizeof e;
    if (e.in_use) {
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool userprog_readdir(struct file* file, char name[NAME_MAX + 1]) {
  if (!inode_is_dir(file_get_inode(file)))
    return false;
  struct dir_entry e;

  while (inode_read_at(file_get_inode(file), &e, sizeof e, file->pos) == sizeof e) {

    file->pos += sizeof e;
    bool sdot = e.name[0] == '.' && e.name[1] == 0;
    bool ddot = e.name[0] == '.' && e.name[1] == '.' && e.name[2] == 0;
    if (e.in_use && !(sdot || ddot)) {
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
// bool thread_dir_lookup(const char* name, struct inode** inode) {
//   struct dir_entry e;
//   struct dir* dir = dir_open_root();
//   ASSERT(dir != NULL);
//   ASSERT(name != NULL);

//   if (lookup(dir, name, &e, NULL))
//     *inode = inode_open(e.inode_sector);
//   else
//     *inode = NULL;

//   return *inode != NULL;
// }

bool mkdir(const char* name) {
  struct inode* inode;
  char temp[NAME_MAX + 1];
  struct dir* dir = dir_open_root();
  if (!subdir_lookup(dir, name, &inode, temp))
    return false;
  dir_close(dir);
  //printf("Dir found: %d", inode_get_inumber(inode));
  dir = dir_open(inode);
  block_sector_t inode_sector = 0;
  //printf("ADD0: %s\n", temp);
  if (!free_map_allocate(1, &inode_sector))
    return false;
  //printf("ADDIT mk: %d\n", inode_sector);
  bool suc = dir_add(dir, temp, inode_sector, true);
  if (suc) {
    //printf("NOw populating directory\n", inode_sector);
    suc = dir_create(inode_sector, 2, inode_get_inumber(inode));
  }
  dir_close(dir);
  return suc;
}

bool chdir(const char* name) {
  struct dir* dir = dir_open_root();
  struct inode* inode = NULL;
  bool success = false;
  if (dir != NULL) {
    if (dir_lookup(dir, name, &inode)) {
      thread_current()->current_working_dir = inode_get_inumber(inode);
      //printf("working dir to %d\n",inode_get_inumber(inode) );
      inode_close(inode);
      success = true;
    }
  }
  dir_close(dir);
  return success;
}