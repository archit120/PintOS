#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

struct singly_indirect_inode_disk {
  block_sector_t inode_sector[BLOCK_SECTOR_SIZE / sizeof(block_sector_t)];
};

struct doubly_indirect_inode_disk {
  block_sector_t singly_indirect_inode_sector[BLOCK_SECTOR_SIZE / sizeof(block_sector_t)];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  block_sector_t direct; /* First data sector. */
  off_t length;          /* File size in bytes. */
  block_sector_t single_indirect;
  block_sector_t double_indirect;
  uint32_t is_dir;
  unsigned magic;       /* Magic number. */
  uint32_t unused[122]; /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location of the inode. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */

  block_sector_t direct; /* First data sector. */

  block_sector_t single_indirect;
  block_sector_t double_indirect;

  uint32_t is_dir;

  off_t length; /* File size in bytes. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  // TODO: EXTEND
  sizeof(struct inode_disk);
  if (pos < inode->length) {
    if (pos < BLOCK_SECTOR_SIZE)
      return inode->direct + pos / BLOCK_SECTOR_SIZE;
    pos -= BLOCK_SECTOR_SIZE;
    uint8_t* temp = malloc(BLOCK_SECTOR_SIZE);
    // //printf("sepc: %d %d\n", inode->single_indirect, inode);

    if (pos < BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4) {
      int index = pos / BLOCK_SECTOR_SIZE;
      block_read(fs_device, inode->single_indirect, temp);
      block_sector_t ret = ((struct singly_indirect_inode_disk*)temp)->inode_sector[index];
      // for(int i = 0;i<80;i++)
      //   //printf("t %d %d\n", i, ((struct singly_indirect_inode_disk*)temp)->inode_sector[i]);
      free(temp);
      // //printf("%d from %d\n", pos, ret);
      return ret;
    }
    pos -= BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4;

    int index = pos / (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4);
    block_read(fs_device, inode->double_indirect, temp);
    block_sector_t singly_indirect_sector =
        ((struct doubly_indirect_inode_disk*)temp)->singly_indirect_inode_sector[index];
    pos -= index * (BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4);
    index = pos / BLOCK_SECTOR_SIZE;
    block_read(fs_device, singly_indirect_sector, temp);
    block_sector_t ret = ((struct singly_indirect_inode_disk*)temp)->inode_sector[index];
    free(temp);
    return ret;
  } else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

// block_sector_t find_inode_sector(inode* )

void direct_inode(off_t* sz_done, block_sector_t* res, off_t sz_demand, off_t sz_prev_alloc) {
  // //printf("prev: %d\n", sz_prev_alloc);

  if (sz_prev_alloc <= *sz_done) {
    static char zeros[BLOCK_SECTOR_SIZE];
    free_map_allocate(1, res);

    block_write(fs_device, *res, zeros);
  }
  *sz_done += BLOCK_SECTOR_SIZE;
}

void single_indirect_inode(off_t* sz_done, block_sector_t* res, off_t sz_demand,
                           off_t sz_prev_alloc, uint8_t alloc) {

  if (sz_demand <= BLOCK_SECTOR_SIZE || *sz_done >= sz_demand)
    return;

  struct singly_indirect_inode_disk* disk_inode;
  disk_inode = malloc(sizeof *disk_inode);

  if (alloc)
    free_map_allocate(1, res);
  else
    block_read(fs_device, *res, disk_inode);

  int i = 0;

  for (; i < BLOCK_SECTOR_SIZE / sizeof(block_sector_t) && *sz_done < sz_demand; i++) {
    direct_inode(sz_done, &disk_inode->inode_sector[i], sz_demand, sz_prev_alloc);
  }

  block_write(fs_device, *res, disk_inode);
  free(disk_inode);
}

void double_indirect_inode(off_t* sz_done, block_sector_t* res, off_t sz_demand,
                           off_t sz_prev_alloc) {
  if (sz_demand <= BLOCK_SECTOR_SIZE * (BLOCK_SECTOR_SIZE / 4 + 1) || *sz_done >= sz_demand)
    return;

  struct doubly_indirect_inode_disk* disk_inode;
  disk_inode = malloc(sizeof *disk_inode);

  if (sz_prev_alloc <= BLOCK_SECTOR_SIZE * (BLOCK_SECTOR_SIZE / 4 + 1))
    free_map_allocate(1, res);
  else
    block_read(fs_device, *res, disk_inode);

  int i = 0;

  for (; i < BLOCK_SECTOR_SIZE / sizeof(block_sector_t) && *sz_done < sz_demand; i++) {
    //printf("%d %d\n", sz_prev_alloc , BLOCK_SECTOR_SIZE + BLOCK_SECTOR_SIZE*BLOCK_SECTOR_SIZE/4*(i+1) + 1);
    single_indirect_inode(
        sz_done, &disk_inode->singly_indirect_inode_sector[i], sz_demand, sz_prev_alloc,
        sz_prev_alloc <
            BLOCK_SECTOR_SIZE + BLOCK_SECTOR_SIZE * BLOCK_SECTOR_SIZE / 4 * (i + 1) + 1);
  }

  block_write(fs_device, *res, disk_inode);
  free(disk_inode);
}

void inode_extend(block_sector_t sector, off_t sz) {
  struct inode_disk* disk_inode = NULL;
  disk_inode = calloc(1, sizeof *disk_inode);
  block_read(fs_device, sector, disk_inode);
  if (disk_inode->length < sz) {
    off_t done = 0;
    direct_inode(&done, &disk_inode->direct, sz, disk_inode->length);
    single_indirect_inode(&done, &disk_inode->single_indirect, sz, disk_inode->length,
                          disk_inode->length <= BLOCK_SECTOR_SIZE);
    double_indirect_inode(&done, &disk_inode->double_indirect, sz, disk_inode->length);
    disk_inode->length = sz;
    block_write(fs_device, sector, disk_inode);
  }
  free(disk_inode);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    disk_inode->length = 0;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_dir = is_dir;
    block_write(fs_device, sector, disk_inode);
    // if (free_map_allocate(1, &disk_inode->direct)) {
    //   // if (sectors > 0) {
    //   //   static char zeros[BLOCK_SECTOR_SIZE];
    //   //   size_t i;

    //   //   // for (i = 0; i < sectors; i++)
    //   //   block_write(fs_device, disk_inode->direct, zeros);

    //   //   sectors--;
    //   //   if (sectors > 0) {
    //   //     free_map_allocate(1, &disk_inode->single_indirect);
    //   //     int i = 0;
    //   //     int num_sectors = sectors <= BLOCK_SECTOR_SIZE / sizeof(block_sector_t)
    //   //                           ? sectors
    //   //                           : BLOCK_SECTOR_SIZE / sizeof(block_sector_t);
    //   //     // //printf("sepc: %d %d\n", disk_inode->single_indirect, disk_inode);
    //   //     for (; i < num_sectors; i++) {
    //   //       block_sector_t sector_new;
    //   //       free_map_allocate(1, &sector_new);
    //   //       block_write_offsz(fs_device, disk_inode->single_indirect, &sector_new,
    //   //                         sizeof(block_sector_t) * i, sizeof(block_sector_t));
    //   //       // //printf("%d at %d\n", i, sector_new);
    //   //     }
    //   //     sectors -= i;

    //   //     if (sectors > 0) {
    //   //     }
    //   //     // //printf("SZ: %d %d\n", length, sectors);
    //   //     ASSERT(sectors <= 0);
    //   //   }
    //   // }
    //   // block_write(fs_device, sector, disk_inode);
    //   success = true;
    // }
    success = true;
    inode_extend(sector, length);
    // //printf("new length: %d\n", length);
    free(disk_inode);
  }
  return success;
}

bool inode_already_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      return true;
    }
  }
  return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  struct inode_disk tempbuffer;

  block_read(fs_device, inode->sector, &tempbuffer);

  inode->is_dir = tempbuffer.is_dir;
  inode->direct = tempbuffer.direct;
  inode->length = tempbuffer.length;
  inode->single_indirect = tempbuffer.single_indirect;
  inode->double_indirect = tempbuffer.double_indirect;
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      free_map_release(inode->direct, bytes_to_sectors(inode->length));
    }

    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, directing at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) {
    /* Disk sector to read, directing byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    //printf("ra at %d from %d through %d + %d\n", sector_idx, offset, inode->sector, inode->direct);

    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      block_read(fs_device, sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      block_read_offsz(fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
    }
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  return bytes_read;
}

void inode_update(struct inode* inode, off_t len) {
  if (len <= inode->length)
    return;
  //printf("EXTEND FROM %d to %d at %d + %d\n", inode->length, len, inode->sector, inode->direct);
  struct inode_disk* disk_inode = malloc(sizeof(struct inode_disk));
  inode_extend(inode->sector, len);
  block_read(fs_device, inode->sector, disk_inode);
  inode->single_indirect = disk_inode->single_indirect;
  inode->double_indirect = disk_inode->double_indirect;
  inode->direct = disk_inode->direct;
  inode->length = disk_inode->length;
  free(disk_inode);
}

/* Writes SIZE bytes from BUFFER into INODE, directing at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;

  inode_update(inode, size + offset);

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) {
    /* Sector to write, directing byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    //printf("a at %d from %d through %d +\n", sector_idx, offset, inode->sector);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      block_write(fs_device, sector_idx, buffer + bytes_written);
    } else {

      block_write_offsz(fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) { return inode->length; }

bool inode_is_dir(struct inode* inode) { return inode->is_dir; }