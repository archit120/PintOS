
#include "devices/block.h"

#define CACHE_SIZE 64

struct sector_cache {
  block_sector_t cached[CACHE_SIZE];
  int clock_hand;
  uint8_t recently_accessed[CACHE_SIZE];
  uint8_t buffer[BLOCK_SECTOR_SIZE * CACHE_SIZE];
  uint8_t dirty[CACHE_SIZE];
  uint8_t valid[CACHE_SIZE];
};

// ALLOCATES MEMORY FOR CACHE
void cache_init();

// THERE MUST BE AN EMPTY BLOCK IN CACHE before calling this
void cache_add(sector_cache* cache, void* buffer, block_sector_t sector);

// Gauranteed to remove a block from the cache
void* cache_evict(sector_cache* cache, block_sector_t* evicted_sector, uint8_t* dirty);

// returns true if sector exists in cache and reads it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t read_cache(sector_cache* cache, block_sector_t sector, void* buffer);

// returns true if sector exists in cache and writes it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t read_cache(sector_cache* cache, block_sector_t sector, void* buffer);
