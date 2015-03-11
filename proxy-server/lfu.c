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
  char *url;
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

/* Cache data structure */
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
  cache.mem_used = 0;
  cache.capacity = capacity;
  cache.min_entry_size = min_entry_size;

  /* Initialize cache */
  cache.entries = malloc(cache.max_entries * sizeof(cache_entry_t));
  for (i = 0; i < cache.max_entries; i++) {
    cache.entries[i].data = NULL;
    cache.entries[i].url = NULL;
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

  return 1;
}

void* gtcache_get(char *key, size_t *val_size){
  int *hits;
  char *data_copy;
  int id;
  int *idp;

  idp  = (int *) hshtbl_get(&url_to_id_tbl, key);

  /* See if it is in the cache */
  if (idp != (int *) NULL) {
    id = *idp;

    /* Cache hit */
    /* Increment hits in minpq */
    hits = indexminpq_keyof(&id_by_hits_pq, id);
    *hits = *hits + 1;
    indexminpq_increasekey(&id_by_hits_pq, id, (void *) hits);

    /* Return all data */
    data_copy = (char *) malloc(cache.entries[id].size);
    memcpy(data_copy, cache.entries[id].data, cache.entries[id].size);

    /* Set val_size if possible */
    if (val_size != (size_t *) NULL) {
      *val_size = cache.entries[id].size;
    }

    return data_copy;
  } else {
    /* Cache miss */
    return NULL;
  }
}

int gtcache_set(char *key, void *value, size_t val_size){
  cache_entry_t *victim;
  int id, *idp, *hits;

  /* Determine if we can add this to the cache without evicting */
  while (need_eviction(val_size)) {
    /* Evict based on LFU policy */
    if (!indexminpq_isempty(&id_by_hits_pq)) {
      id = indexminpq_minindex(&id_by_hits_pq);
      hits = indexminpq_keyof(&id_by_hits_pq, id);

      indexminpq_delmin(&id_by_hits_pq);
      victim = &cache.entries[id];

      idp = (int *) hshtbl_get(&url_to_id_tbl, victim->url);
      hshtbl_delete(&url_to_id_tbl, victim->url);
      steque_push(&free_ids, id);

      free(idp);
      free(hits);

      cache.mem_used -= victim->size;
      cache.num_entries--;
      free(victim->data);
      free(victim->url);
      victim->size = 0;
    } else {
      /* the val size is bigger than the total cache available memory */
      return 0;
    }
  }

  /* Get next free ID */
  id = (int) steque_pop(&free_ids);
  idp = malloc(sizeof(int));
  *idp = id;

  /* Allocate memory for new entry */
  cache.entries[id].size = val_size;
  cache.entries[id].data = (char *) malloc(val_size);
  cache.entries[id].url = (char *) malloc(strlen(key));
  cache.mem_used += val_size;
  cache.num_entries++;

  /* Add to cache */
  memcpy(cache.entries[id].data, value, val_size);
  strcpy(cache.entries[id].url, key); // FIXME: Should probably use strncpy for safety

  /* Create hash table mapping */
  hshtbl_put(&url_to_id_tbl, cache.entries[id].url, (hshtbl_item) idp);

  hits = (int *) malloc(sizeof(int));
  *hits = 1;

  /* Add ID to minpq */
  indexminpq_insert(&id_by_hits_pq, id, (void *) hits);

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
      free(cache.entries[i].url);
    }
  }

  free(cache.entries);

  steque_destroy(&free_ids);
  indexminpq_destroy(&id_by_hits_pq);
  hshtbl_destroy(&url_to_id_tbl);
}
