/*
 * Implement an LRUMIN replacement cache policy
 *
 * See the comments in gtcache.h for API documentation.
 *
 * The LRUMIN eviction should be equivalent to the 
 * following psuedocode.
 *
 * while more space needs to be cleared
 *   Let n be the smallest integer such that 2^n >= space needed
 *   If there is an entry of size >= 2^n,
 *      delete the least recently used entry of size >= 2^n
 *   Otherwise, let m be the smallest inetger such that there an entry of size >= 2^m
 *     delete the least recently used item of size >=2^m
 *
 * The convention for what size entries belong to what level is as follows.
 *
 * Level 0: 0 through 2 * min_entry_size
 * Level 1: 2 * min_entry_size through 4 * min_entry_size
 * Level 2: 4 * min_entry_size through 8 * min_entry_size
 * Level 3: 8 * min_entry_size through 16 * min_entry_size
 * ...
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

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

/* Indexed priority queue tracking last access time for cache index */
static indexminpq_t *id_by_time_pq;
static int num_bands;

/* Stack of free IDs for cache */
static steque_t free_ids;

/*
 * Level 0:  0 - 2^1 * min entry size - 1
 * Level 1:  2^1 * min entry size   through   2^2 * min entry size - 1
 * ...
 * Level N:  2^N * min entry size   through   2^N+1 * min entry size - 1
 */
static int get_band(size_t sz) {
  int i;
  for (i = 1; sz >= ((1 << i) * cache.min_entry_size); i++);
  return i-1;
}

inline int keycmp(void *a, void *b) {
  struct timeval *x = (struct timeval *) a;
  struct timeval *y = (struct timeval *) b;
  return (x->tv_sec * 1000000 + x->tv_usec ) - (y->tv_sec * 1000000 + y->tv_usec);
}

static int get_lru_band(int min_band, int max_band) {
  int band = -1;

  if (!indexminpq_isempty(&id_by_time_pq[min_band])) {
    band = min_band;
  }

  return band;
}

static char need_eviction(size_t added_size) {
  if (cache.num_entries >= cache.max_entries) {
    return 1;
  }

  if (cache.mem_used + added_size > cache.capacity) {
    return 1;
  }

  return 0;
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
  steque_init(&free_ids);

  /* Set up LRUMIN bands */
  num_bands = num_levels;
  id_by_time_pq = (indexminpq_t *) malloc(num_bands * sizeof(indexminpq_t));

  for (i = 0; i < num_bands; i++) {
    //printf("Band %d: < %d\n", i, (int) ((2 << i) * cache.min_entry_size));
    indexminpq_init(&id_by_time_pq[i], cache.max_entries, keycmp);
  }

  //fflush(stdout);

  /* Populate free_ids */
  for (i = cache.max_entries - 1; i >= 0; i--) {
    steque_push(&free_ids, i);
  }

  return 1;
}

void* gtcache_get(char *key, size_t *val_size){
  struct timeval *time;
  char *data_copy;
  int id, band;
  int *idp;

  idp  = (int *) hshtbl_get(&url_to_id_tbl, key);

  /* See if it is in the cache */
  if (idp != (int *) NULL) {
    id = *idp;
    band = get_band(cache.entries[id].size);

    /* Cache hit */
    /* Update access time minpq */
    time = indexminpq_keyof(&id_by_time_pq[band], id);
    gettimeofday(time, NULL);

    indexminpq_increasekey(&id_by_time_pq[band], id, (void *) time);

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
  struct timeval *time;
  cache_entry_t *victim;
  int id, *idp, band, tgt_band, min_band, max_band;
  size_t eviction_size = val_size;

  //printf("Num bands %d\n", num_bands);
  //printf("Adding to band %d\n", band);

  /* Determine if we can add this to the cache without evicting */
  while (need_eviction(val_size)) {
    band = get_band(eviction_size);

    if (band + 1 < num_bands) {
      min_band = band + 1;
      max_band = num_bands - 1;
    } else {
      min_band = band;
      max_band = band;
    }

    // Find the band with the LRU entry >= tgt_band
    do {
      tgt_band = get_lru_band(min_band, max_band);

      if (min_band <= band) {
        if (tgt_band == -1) {
          max_band = min_band;
          min_band--;
        }
      } else {
        if (tgt_band == -1) {
          if (min_band < num_bands - 1) {
            min_band++;
          } else {
            min_band = band;
          }
        }
      }
    } while (tgt_band == -1 && min_band >= 0);

    if (min_band < 0) {
      return 0;
    }

    //printf("Attempting to evict from band %d with size %d\n", tgt_band, indexminpq_size(&id_by_time_pq[tgt_band]));
    //printf("Size needed: %d   Free size: %d\n", (int) val_size, ((int) cache.capacity - (int) cache.mem_used));
    fflush(stdout);

    /* Remove min val from current band */
    id = indexminpq_minindex(&id_by_time_pq[tgt_band]);
    time = indexminpq_keyof(&id_by_time_pq[tgt_band], id);
    indexminpq_delmin(&id_by_time_pq[tgt_band]);

    /* Evict */
    victim = &cache.entries[id];

    //printf("Evicting from band %d, freeing %d\n", tgt_band, (int) victim->size);
    //fflush(stdout);

    idp = (int *) hshtbl_get(&url_to_id_tbl, victim->url);
    hshtbl_delete(&url_to_id_tbl, victim->url);
    steque_push(&free_ids, id);

    free(idp);
    free(time);

    eviction_size -= victim->size;
    cache.mem_used -= victim->size;
    cache.num_entries--;
    free(victim->data);
    free(victim->url);
    victim->size = 0;
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

  time = (struct timeval *) malloc(sizeof(struct timeval));
  gettimeofday(time, NULL);

  band = get_band(val_size);

  /* Add ID to minpq */
  indexminpq_insert(&id_by_time_pq[band], id, (void *) time);

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
  hshtbl_destroy(&url_to_id_tbl);

  for (i = 0; i < num_bands; i++) {
    indexminpq_destroy(&id_by_time_pq[i]);
  }

  free(id_by_time_pq);
}
