/*
 * Implement an LFU replacement cache policy
 *
 * See the comments in gtcache.h for API documentation.
 *
 * The entry in the cache with the fewest hits since it entered the
 * cache (the most recent time) is evicted.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gtcache.h"

/*
 * Include headers for data structures as you see fit
 */
#include "hshtbl.h"
#include "indexminpq.h"
#include "steque.h"

typedef struct cache_entry {
  char *data;
  size_t size;
} cache_entry_t;

typedef struct cache {
  size_t capacity;
  size_t min_entry_size;
  size_t max_entries;
  size_t num_entries;
  size_t mem_used;
  cache_entry_t *entries;
} cache_t;

/* FIXME: Cache data structure */
static cache_t cache;

/* Hash table mapping URL to index in cache */
static hshtbl_t url_to_id_tbl;

/* Indexed priority queue tracking number of hits for cache index */
static indexminpq_t id_by_hits_pq;

/* Stack of free IDs for cache */
static steque_t free_ids;

static char need_eviction(size_t added_size) {
  if (cache.num_entries >= cache.max_entries) {
    return 1;
  }

  if (cache.mem_used + added_size > cache.capacity) {
    return 1;
  }

  return 0;
}

inline int keycmp(void *a, void *b) {
  int *x = (int *) a;
  int *y = (int *) b;
  return *x - *y;
}

int gtcache_init(size_t capacity, size_t min_entry_size, int num_levels){
  int i;

  /* Initialize cache metadata */
  cache.max_entries = capacity / min_entry_size;
  cache.num_entries = 0;
  cache.capacity = capacity;
  cache.min_entry_size = min_entry_size;

  /* Initialize cache */
  cache.entries = malloc(cache.max_entries * sizeof(cache_entry_t));
  for (i = 0; i < cache.max_entries; i++) {
    cache.entries[i].data = NULL;
    cache.entries[i].size = 0;
  }

  /* Initialize other data structures */
  /* Hash table should be at least twice the size of expected number of entries */
  hshtbl_init(&url_to_id_tbl, cache.max_entries * 2);
  indexminpq_init(&id_by_hits_pq, cache.max_entries, keycmp);
  steque_init(&free_ids);

  /* Populate free_ids */
  for (i = cache.max_entries - 1; i >= 0; i--) {
    steque_push(&free_ids, i);
  }

  /* FIXME: return some value */
  return 1;
}

void* gtcache_get(char *key, size_t *val_size){
  int *hits;
  char *data_copy;
  int id;
  int *idp;
  int len;

  idp  = (int *) hshtbl_get(&url_to_id_tbl, key);

  /* See if it is in the cache */
  if (idp != (int *) NULL) {
    id = *idp;
    free(idp);

    /* Cache hit */
    /* Increment hits in minpq */
    printf("minpq get index: %d\n", id);
    fflush(stdout);
    hits = indexminpq_keyof(&id_by_hits_pq, id);
    *hits = *hits + 1;
    indexminpq_increasekey(&id_by_hits_pq, id, (void *) hits);

    hits = indexminpq_keyof(&id_by_hits_pq, id);

    if (val_size == (size_t *) NULL) {
      /* Return all data */
      data_copy = (char *) malloc(cache.entries[id].size);
      memcpy(data_copy, cache.entries[id].data, cache.entries[id].size);
      /* Assume the last entry in the cache is a null terminator */
      /* FIXME: verify this works */
    } else {
      len = (*val_size > cache.entries[id].size) ? cache.entries[id].size : *val_size;

      data_copy = (char *) malloc(len);

      printf("Len: %d\n", (int) len);
      fflush(stdout);

      printf("get cache data loc: %p\n", cache.entries[id].data);
      fflush(stdout);

      printf("data copy loc: %p\n", data_copy);
      fflush(stdout);

      memcpy(data_copy, cache.entries[id].data, len - 1);

      printf("Got here 2\n");
      fflush(stdout);

      data_copy[len - 1] = '\0';
    }

    return data_copy;
  } else {
    /* Cache miss */
    /* FIXME: find out what we do here */
    return NULL;
  }
}

int gtcache_set(char *key, void *value, size_t val_size){
  cache_entry_t *victim;
  int id, *idp, hits = 1;

  /* Determine if we can add this to the cache without evicting */
  while (need_eviction(val_size)) {
    printf("Doing an eviction\n");
    fflush(stdout);

    /* Evict based on LFU policy */
    id = indexminpq_delmin(&id_by_hits_pq);
    victim = &cache.entries[id];

    cache.mem_used -= victim->size;
    cache.num_entries--;
    free(victim->data);
    victim->size = 0;
    steque_push(&free_ids, id);
  }

  /* Get next free ID */
  id = (int) steque_pop(&free_ids);

  idp = malloc(sizeof(int));
  *idp = id;

  /* Create hash table mapping */
  hshtbl_put(&url_to_id_tbl, key, (hshtbl_item) idp);

  printf("minpq set index: %d\n", id);
  fflush(stdout);

  /* Add ID to minpq */
  indexminpq_insert(&id_by_hits_pq, id, (void *) &hits);

  /* Allocate memory for new entry */
  cache.entries[id].size = val_size;
  cache.entries[id].data = (char *) malloc(val_size);
  cache.mem_used += val_size;
  cache.num_entries++;

  printf("set cache data loc: %p\n", cache.entries[id].data);
  fflush(stdout);

  /* Add to cache */
  memcpy(cache.entries[id].data, value, val_size);

  /* FIXME: Return some value */   
  return 1;
}

int gtcache_memused(){
  return cache.mem_used;
}

void gtcache_destroy(){
  int i;

  for (i = 0; i < cache.max_entries; i++) {
    if (cache.entries[i].size > 0) {
      free(cache.entries[i].data);
    }
  }

  free(cache.entries);

  steque_destroy(&free_ids);
  indexminpq_destroy(&id_by_hits_pq);
  hshtbl_destroy(&url_to_id_tbl);
}
