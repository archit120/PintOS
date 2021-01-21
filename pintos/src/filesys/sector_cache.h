#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#define CACHE_SIZE 64

struct sector_cache {
  struct lock cache_lock;
  block_sector_t cached[CACHE_SIZE];
  int clock_hand;
  uint8_t recently_accessed[CACHE_SIZE];
  uint8_t buffer[BLOCK_SECTOR_SIZE * CACHE_SIZE];
  uint8_t dirty[CACHE_SIZE];
  uint8_t valid[CACHE_SIZE];
};

typedef struct sector_cache sector_cache;
// ALLOCATES MEMORY FOR CACHE
void cache_init(sector_cache* cache);

// THERE MUST BE AN EMPTY BLOCK IN CACHE before calling this
uint8_t cache_add(sector_cache* cache, void* buffer, block_sector_t sector, uint8_t dirty,
                  block_sector_t* evicted_sector, uint8_t* dirty_evict, void* evicted_buffer);

// Gauranteed to remove a block from the cache
uint8_t cache_evict(sector_cache* cache, void* buffer, block_sector_t* evicted_sector,
                    uint8_t* dirty);

// returns true if sector exists in cache and reads it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t cache_read(sector_cache* cache, block_sector_t sector, void* buffer);

// returns true if sector exists in cache and writes it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t cache_write(sector_cache* cache, block_sector_t sector, void* buffer);

uint8_t cache_get_dirty(sector_cache* cache, void* buffer, block_sector_t* sector);
#endif