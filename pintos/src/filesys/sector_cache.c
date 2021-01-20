#include "sector_cache.h"
#include "lib/string.h"

void cache_init(sector_cache* cache) {
  for (int i = 0; i < CACHE_SIZE; i++)
    cache->valid[i] = 0;
  cache->clock_hand = 0;
  lock_init(&cache->cache_lock);
}

// lock must be held
void increment_clock(int* clock) {
  (*clock)++;
  if (*clock == CACHE_SIZE)
    *clock = 0;
}

uint8_t cache_evict_nolock(sector_cache* cache, void* buffer, block_sector_t* evicted_sector,
                           uint8_t* dirty);
{
  while (cache->valid[cache->clock_hand] && cache->recently_accessed[cache->clock_hand])
    cache->recently_accessed[cache->clock_hand] = 0, increment_clock(&cache->clock_hand);
  *evicted_sector = cache->cached[cache->clock_hand];
  *dirty = cache->dirty[cache->clock_hand];
  uint8_t ret = 0;
  if (cache->valid[cache->clock_hand])
    memcpy(buffer, &cache->buffer[BLOCK_SECTOR_SIZE * (cache->clock_hand)], BLOCK_SECTOR_SIZE),
        ret = 1;
  cache->valid[cache->clock_hand] = 0;
  return ret;
}

// THERE MUST BE AN EMPTY BLOCK IN CACHE before calling this
uint8_t cache_add(sector_cache* cache, void* buffer, block_sector_t sector, uint8_t dirty,
                  block_sector_t* evicted_sector, uint8_t* dirty_evict, void* evicted_buffer) {
  lock_acquire(&cache->cache_lock);
  uint8_t ret = cache_evict_nolock(cache, evicted_buffer, evicted_sector, dirty_evict);
  while (cache->valid[cache->clock_hand])
    increment_clock(&cache->clock_hand);
  memcpy(&cache->buffer[BLOCK_SECTOR_SIZE * (cache->clock_hand)], buffer, BLOCK_SECTOR_SIZE),
      cache->valid[cache->clock_hand] = 1, cache->dirty[cache->clock_hand] = dirty,
      cache->clock_hand++;
  lock_release(&cache->cache_lock);
  return ret;
}

// Gauranteed to remove a block from the cache if no empty block
uint8_t cache_evict(sector_cache* cache, void* buffer, block_sector_t* evicted_sector,
                    uint8_t* dirty) {
  lock_acquire(&cache->cache_lock);
  uint8_t ret = cache_evict_nolock(cache, buffer, evicted_sector, dirty);
  lock_release(&cache->cache_lock);
  return ret;
}

// returns true if sector exists in cache and reads it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t cache_read(sector_cache* cache, block_sector_t sector, void* buffer) {
  lock_acquire(&cache->cache_lock);
  int i = 0;
  for (int i = 0; i < CACHE_SIZE; i++)
    if (cache->cached[i] == sector)
      break;
  if (cache->cached[i] != sector) {
    lock_release(&cache->cache_lock);
    return 0;
  }
  memcpy(buffer, &cache->buffer[BLOCK_SECTOR_SIZE * (i)], BLOCK_SECTOR_SIZE);
  cache->recently_accessed[i] = 1;
  lock_release(&cache->cache_lock);
  return 1;
}

// returns true if sector exists in cache and writes it into buffer which must be atleast BLOCK_SECTOR_SIZE large
uint8_t cache_write(sector_cache* cache, block_sector_t sector, void* buffer) {
  lock_acquire(&cache->cache_lock);
  int i = 0;
  for (int i = 0; i < CACHE_SIZE; i++)
    if (cache->cached[i] == sector)
      break;
  if (cache->cached[i] != sector) {
    lock_release(&cache->cache_lock);
    return 0;
  }
  memcpy(&cache->buffer[BLOCK_SECTOR_SIZE * (i)], buffer, BLOCK_SECTOR_SIZE);
  cache->recently_accessed[i] = 1;
  cache->dirty[i] = 1;
  lock_release(&cache->cache_lock);
  return 1;
}

uint8_t cache_any_dirty(sector_cache* cache) {
  lock_acquire(&cache->cache_lock);
  uint8_t ret = 0;
  for (int i = 0; i < CACHE_SIZE; i++)
    if (cache->valid[i] && cache->dirty[i])
      ret = i;
  lock_release(&cache->cache_lock);
  return ret;
}