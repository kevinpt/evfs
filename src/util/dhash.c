/* SPDX-License-Identifier: MIT
Copyright 2021 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/*
------------------------------------------------------------------------------
dhash

This implements Robin Hood hash using linear probing and prime numbers
for the bucket array size.

The primes are precomputed as a list inside prime_modulus.h. This can be
customized to tailor the growth behavior of the hash. The provided list
of primes is designed to grow slowly to minimize memory consumption on
memory constrained systems.

The maximum load factor is set to 93% to minimize wasted space. It can be
customized by changing the MAX_LOAD_FACTOR() macro to suit application
specific needs.


Reference:
  https://www.sebastiansylvan.com/post/robin-hood-hashing-should-be-your-default-hash-table-implementation/
  http://www.idryman.org/blog/2017/05/03/writing-a-damn-fast-hash-table-with-tiny-memory-footprints/
  https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/

------------------------------------------------------------------------------
*/


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#include "dhash.h"
#include "prime_modulus.h"
#include "search.h"



// ******************** Configuration ********************

#define dh__malloc(s)       calloc(1, s)
#define dh__realloc(p, s)   realloc((p), (s))
#define dh__free(p)         free(p)


// Hashed keys can be saved in the struct for each bucket to reduce overhead
// when rebuilding the hash and as a quick equality check before calling the
// is_equal() callback on lookups and insertions.

#define DH_USE_MEMOIZED_HASH


// Use generated callbacks for calculating modulus with prime numbers

#define DH_USE_MODULUS_FUNCS


// Robinhood hash lets us have high load factors so we can minimize wasted space.
// Limit load factor to ~93%
#define MAX_LOAD_FACTOR(b) ((b) * 15UL / 16UL)




#define PROBE_COUNT_BITS    15
#define MAX_PROBE_COUNT     ((1UL << PROBE_COUNT_BITS) - 1)

// Internal elements of the hash buckets array
typedef struct dhBucketEntry {
  dhKey   key;
#ifdef DH_USE_MEMOIZED_HASH
  dhIKey  ikey;  // Memoized hash of multi-byte key
#endif

  uint16_t probe_count:   PROBE_COUNT_BITS; // Number of probes. 0 indicates unused
  uint16_t deleted:       1;                // Mark tombstoned entries

  uintptr_t value_obj[]; // Additional space of value_size bytes
} dhBucketEntry;



#define DH_ERR_KEY_NOT_FOUND    -1
#define DH_ERR_TOO_MANY_PROBES  -2



#define SET_DELETED(entry)    ((entry)->deleted = 1)
#define CLEAR_DELETED(entry)  ((entry)->deleted = 0)

#define IN_USE(entry)         ((entry)->probe_count != 0)
#define WAS_DELETED(entry)    ((entry)->deleted)
#define PROBE_COUNT(entry)    ((entry)->probe_count)
#define SET_PROBE_COUNT(entry, probes)  ((entry)->probe_count = (probes))


// Round up object size to match alignment of a type
#define ROUND_UP_ALIGN(n, T)  ((n) + _Alignof(T)-1 - ((n) + _Alignof(T)-1) % _Alignof(T))


#ifndef MAX
#  define MAX(x, y) ((x) >= (y) ? (x) : (y))
#endif



// ******************** Prime modulus tables ********************

// List of primes we will use to size our dynamic array of buckets.
// We use a truncated list if bucket index is only 16-bit.
static dhBucketIndex s_prime_modulus[] = {
#define PRIME_MODULUS(i, p) p,

  PRIME_LIST_16(PRIME_MODULUS)
#ifndef DH_INDEX_16BIT
  PRIME_LIST(PRIME_MODULUS)
#endif
};


#ifndef DH_INDEX_16BIT
#  define PRIME_LIST_LEN  NUM_PRIME_MODULI
#else
#  define PRIME_LIST_LEN  NUM_PRIME_MODULI_16
#endif

// Retrieve the prime modulus using its table index
static inline dhBucketIndex get_prime(dhBucketIndex prime_ix) {
  if(prime_ix >= PRIME_LIST_LEN)
    prime_ix = PRIME_LIST_LEN - 1;

  return s_prime_modulus[prime_ix];
}



// Comparison callback for prime list
static ptrdiff_t comp_primes(const void *pkey, const void *pelem) {
  dhBucketIndex key = *(dhBucketIndex *)pkey;
  dhBucketIndex elem = *(dhBucketIndex *)pelem;

  return key - elem;
}

// Binary search for a suitable prime to fit n buckets
#define get_closest_prime_index(n) search_nearest_above(&(n), s_prime_modulus, PRIME_LIST_LEN, \
                                                  sizeof(s_prime_modulus[0]), comp_primes);

// External stroage buffer needs a prime *less* than maximum capacity
#define get_closest_prime_index_below(n) search_nearest_below(&(n), s_prime_modulus, PRIME_LIST_LEN, \
                                                  sizeof(s_prime_modulus[0]), comp_primes);


#ifdef DH_USE_MODULUS_FUNCS
// Compute 'n mod prime' using hardcoded constants for each case.
// This gives a compiler the chance to optimize out the division for bucket probes.
// We do this by building a table of function pointers that will perform a
// hard-coded modulus for each prime.

// Generate functions for each prime modulus
#  define PM_FUNC(i, p) \
    static dhBucketIndex dh__pm_##i(dhIKey k) { return k % p; }

PRIME_LIST_16(PM_FUNC)
#  ifndef DH_INDEX_16BIT
PRIME_LIST(PM_FUNC)
#  endif

// Build a table of function pointers
typedef dhBucketIndex (*dhPMFunc)(dhIKey k);

static const dhPMFunc s_prime_modulus_funcs[] = {
#  define PRIME_MODULUS_FUNC(i, p) dh__pm_##i,
  PRIME_LIST_16(PRIME_MODULUS_FUNC)
#  ifndef DH_INDEX_16BIT
  PRIME_LIST(PRIME_MODULUS_FUNC)
#  endif
};
#endif


static inline dhBucketIndex get_fast_modulus(dhIKey k, dhBucketIndex prime_ix) {
#ifdef DH_USE_MODULUS_FUNCS
  dhPMFunc f = s_prime_modulus_funcs[prime_ix];
  return f(k);

#else
  return k % s_prime_modulus[prime_ix];
#endif
}


static inline dhBucketIndex next_bucket(dhBucketIndex b, dhBucketIndex prime_ix) {
  dhBucketIndex prime = s_prime_modulus[prime_ix];

  // Increment with wrap around
  if(++b >= prime)
    b -= prime;

  return b;
}


// ******************** Default callbacks ********************

#if defined __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunused-function"
#elif defined __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

static dhIKey default_gen_hash(dhKey key) {
  dhIKey ikey = (uintptr_t)key.data; // Assume data pointer is a plain integer key

  return ikey;
}


// is_equal test is needed to disambiguate dhKeys that hash into the same dhIKey
// By default we will assume collisions don't happen and skip unhashed key checks
// This holds if the key.data pointer is treated as an integer key
static bool default_is_equal(dhKey key1, dhKey key2, void *ctx) {
  return true;
}

#if defined __clang__
#  pragma clang diagnostic pop
#elif defined __GNUC__
#  pragma GCC diagnostic pop
#endif

// ******************** Resource management ********************


// Bucket array is opaque void * so we index into it by these helpers
static inline dhBucketEntry *dh__get_entry_unsafe(dhash *hash, dhBucketIndex b) {
  size_t bkt_off = (size_t)b * (sizeof(dhBucketEntry) + hash->value_size);
  return hash->buckets + bkt_off;
}

static inline dhBucketEntry *dh__get_entry(dhash *hash, dhBucketIndex b) {
  if(b < 0)
    return NULL;

  size_t bkt_off = (size_t)b * (sizeof(dhBucketEntry) + hash->value_size);
  return hash->buckets + bkt_off;
}


// Allocate new bucket array 
static bool dh__alloc_buckets(dhash *hash, size_t new_num_buckets) {

#define DH_MIN_BUCKETS   s_prime_modulus[0]

  new_num_buckets = MAX(new_num_buckets, DH_MIN_BUCKETS);

  size_t bucket_size = sizeof(dhBucketEntry) + hash->value_size;
  size_t new_size = new_num_buckets * bucket_size;

  if(new_size / bucket_size != new_num_buckets) // Overflow in new_size
    return false;

  void *new_buckets = dh__malloc(new_size); // calloc() wrapper so already zeroed

  if(new_buckets) {
    hash->num_buckets = new_num_buckets;
    hash->buckets = new_buckets;

    return true;
  }

  return false;
}


// Initialize or grow a hash table
static inline bool dh__init(dhash *hash, dhConfig *config, void *ctx, bool new_hash) {
  // Find closest prime to init_buckets
  dhBucketIndex prime_ix    = get_closest_prime_index(config->init_buckets);
  dhBucketIndex num_buckets = get_prime(prime_ix);
  //printf("## dh_init(): %d %zu --> %zu\n", prime_ix, config->init_buckets, num_buckets);


  // Bucket entries need to stay aligned so we pad out the value object
  size_t value_size = ROUND_UP_ALIGN(config->value_size, uintptr_t);

  // Restrict memory usage
  if(config->max_storage > 0 && !config->ext_storage) {
    size_t max_buckets = config->max_storage / (sizeof(dhBucketEntry) + value_size);

    if(num_buckets > max_buckets)
      return false;
  }


  // Notify owner that the dhash is about to grow.
  // This gives an opportunity to prepare data structures for the
  // increased capacity.
  if(config->grow_hash && !config->ext_storage) {
    if(!config->grow_hash(MAX_LOAD_FACTOR(num_buckets), ctx))
      return false;
  }

  // Two scenarios:
  //  1) If this is the first call to dh__init() we don't have a valid bucket array to resize.
  //  2) If this is called from dh__grow() we do have an existing bucket array.
  // In the latter case we want to preserve the original hash parameters if
  // dh__alloc_buckets() fails.

  if(new_hash) {
    *hash = (dhash){
      .num_buckets  = 0,
      .used_buckets = 0,
      .prime_ix     = prime_ix,
      .value_size   = value_size,
      .max_storage  = config->max_storage,
      .ctx          = ctx,
      .destroy_item = config->destroy_item,
      .gen_hash     = config->gen_hash,
      .is_equal     = config->is_equal,
      .grow_hash    = config->grow_hash
    };

    if(config->ext_storage) {
      memset(config->ext_storage, 0, config->max_storage);

      hash->buckets = config->ext_storage;

      size_t bucket_size = sizeof(dhBucketEntry) + value_size;
      size_t ext_buckets = config->max_storage / bucket_size;

      hash->prime_ix = get_closest_prime_index_below(ext_buckets);
      hash->num_buckets = get_prime(hash->prime_ix);
      hash->static_buckets = true;

      //printf("## EXT SIZE: %d  bsz: %d  num: %d\n", config->max_storage, bucket_size, ext_buckets);
      //printf("## EXT HASH STORE: %d  %ld\n", hash->num_buckets, get_prime(hash->prime_ix));

    } else { // Allocate buckets
      if(!dh__alloc_buckets(hash, num_buckets))
        return false;
    }

  } else { // Growing more buckets

    // Get buckets
    if(hash->static_buckets || !dh__alloc_buckets(hash, num_buckets))
      return false;

    hash->prime_ix    = prime_ix;
    hash->num_buckets = num_buckets;
  }

  hash->used_buckets = 0;
  return true;
}


/*
Initialize a dynamic hash

Args:
  hash:   Hash to init
  config: Config params for the hash
  ctx:    Optional user context for callbacks

Returns:
  true on success
*/
bool dh_init(dhash *hash, dhConfig *config, void *ctx) {
  if(!hash || !config || !config->destroy_item)
    return false;

  return dh__init(hash, config, ctx, /*new_hash*/true);
}




/*
Free a dynamic hash object

Args:
  hash: Hash to free
*/
void dh_free(dhash *hash) {
  dhBucketEntry *entry;

  if(!hash->buckets)
    return;

  // Eliminate all entries
  dhBucketIndex num_buckets = hash->num_buckets;
  for(dhBucketIndex b = 0; b < num_buckets; b++) {
    entry = dh__get_entry_unsafe(hash, b);

    if(!IN_USE(entry))
      continue;

    //uintptr_t value = entry->value_obj[0];
    //printf("## destroy item: %p.%p -> 0x%08lX\n", entry, &entry->value_obj, value);
    hash->destroy_item(entry->key, &entry->value_obj, hash->ctx);
  }

  if(!hash->static_buckets)
    dh__free(hash->buckets);

  hash->buckets = NULL;
  hash->num_buckets = 0;
}



// Hash for integer key probes
// This is applied on top of any user supplied hash function
static inline dhIKey dh__hash_int(dhIKey ikey) {
  // Knuth multiplicative hash
  // This will spread out consecutive ikey values to increase odds of finding an
  // unoccupied bucket when probing:
  // https://book.huihoo.com/data-structures-and-algorithms-with-object-oriented-design-patterns-in-c++/html/page214.html

  // Constants are Fibonacci hash phi^-1 for n-bit words (2^n / 1.618)
#if UINTPTR_MAX >= INT64_MAX
  uint64_t h = ((uint64_t)ikey) * 0x9e3779b97f4a7c15ULL;
#else
  uint32_t h = ((uint32_t)ikey) * 0x9e3779b9UL;
#endif

  h >>= (sizeof(h) - sizeof(dhIKey)) * 8; // Extract upper bits
  //printf("@@ hashed ikey: %d -> %d\n", ikey, h);
  return (dhIKey)h;
}

// Combine hashes
static inline dhIKey dh__hash(dhash *hash, dhKey key) {
  return dh__hash_int(hash->gen_hash(key));
}


/*
Hash a string into an integer key

This is a helper function that can be used as the gen_hash member in a
dhConfig struct.

Args:
  key:  The key to be hashed

Returns:
  Hashed value of key
*/
dhIKey dh_gen_hash_string(dhKey key) {
  //printf("HASH STR: %ld '%.*s'", key.length, (int)key.length, (char *)key.data);
  char *str = (char *)key.data;
  // djb2 xor hash
  dhIKey h = 5381;

  while(key.length) {
    key.length--;
    h = (h + (h << 5)) ^ str[key.length];
  }

  //printf(" --> %08X\n", h);

  return h;
}


// ******************** Retrieval ********************

// Find the starting bucket for a probe sequence
static inline dhBucketIndex dh__initial_probe(dhash *hash, dhIKey ikey) {
  // Take the modulus using current number of buckets
  dhBucketIndex b = (dhBucketIndex)get_fast_modulus(ikey, hash->prime_ix);

  //printf("## initial probe: cap: %ld  k %ld -> %d\n", hash->num_buckets, ikey, b);
  return b;
}



// Search for a bucket with a given key
// Returns DH_ERR_KEY_NOT_FOUND if the key was not found
//         DH_ERR_TOO_MANY_PROBES if probe count exceeded
static inline dhBucketIndex dh__find_bucket(dhash *hash, dhKey key, dhBucketEntry **found_entry) {

  dhIKey ikey = dh__hash(hash, key);
  dhBucketIndex b = dh__initial_probe(hash, ikey);
  dhBucketEntry *entry = dh__get_entry_unsafe(hash, b);
  uint16_t probes = 1;

  while(1) {
    if(!IN_USE(entry) || (probes > PROBE_COUNT(entry))) {
      return DH_ERR_KEY_NOT_FOUND; // Key not found, invalid bucket

    } else if(
#ifdef DH_USE_MEMOIZED_HASH
              entry->ikey == ikey && 
#endif
                                     hash->is_equal(entry->key, key, hash->ctx)) {
      // Key match found
      *found_entry = entry;
      return b;
    } 


    b = next_bucket(b, hash->prime_ix);
    entry = dh__get_entry_unsafe(hash, b);
    if(probes < MAX_PROBE_COUNT)
      probes++;
    else
      return DH_ERR_TOO_MANY_PROBES;
  }

  return DH_ERR_KEY_NOT_FOUND;
}


/*
Search for a hash entry

Args:
  hash:   Hash to search
  key:    Key to search
  value:  Optional found value matching the key on success

Returns:
  true if item exists and non-NULL in value
*/
bool dh_lookup(dhash *hash, dhKey key, void *value) {
  dhBucketEntry *entry = NULL;
  dh__find_bucket(hash, key, &entry);
  //printf("## GOT BUCKET: %d\n", b);

  if(entry && IN_USE(entry) && !WAS_DELETED(entry)) { // Found match
    if(value)
      memcpy(value, &entry->value_obj, hash->value_size);

    return true;
  }

  return false;
}



// ******************** Storage ********************

// Exchange value objects for Robin Hood algo.
static inline void swap_values(uintptr_t * restrict v1, uintptr_t * restrict v2, size_t value_size) {
  uintptr_t temp;

  // The value size is rounded up to a multiple of uintptr_t alignment so this
  // will never have a remainder.
  value_size /= sizeof(uintptr_t);

  for(size_t i = 0; i < value_size; i++) {
    temp = v1[i];
    v1[i] = v2[i];
    v2[i] = temp;
  }
}


static inline bool dh__insert_ex(dhash *hash, dhKey key, void *value, dhIKey ikey) {
  dhBucketEntry titem;
  dhBucketEntry *entry;
  dhBucketIndex b;
  uint16_t probes = 1;

  //printf("## dh__insert_ex(): %p  %ld\n", value, hash->value_size);

  b = dh__initial_probe(hash, ikey);
  entry = dh__get_entry_unsafe(hash, b);

  while(1) {
    if(!IN_USE(entry)) { // Fresh bucket
      // Add new entry

#ifdef DH_USE_MEMOIZED_HASH
      entry->ikey = ikey;
#endif
      entry->key = key;
      memcpy(&entry->value_obj, value, hash->value_size);

      SET_PROBE_COUNT(entry, probes);
      hash->used_buckets++;
      return true;
    }

    if(
#ifdef DH_USE_MEMOIZED_HASH
        entry->ikey == ikey &&
#endif
                                hash->is_equal(entry->key, key, hash->ctx)) {
       // Match to existing key: Replace value
      hash->destroy_item(entry->key, &entry->value_obj, hash->ctx);
      entry->key = key;
      memcpy(&entry->value_obj, value, hash->value_size);
      return true;
    }

    if(PROBE_COUNT(entry) < probes) {
      if(WAS_DELETED(entry)) { // Tombstone can be reused
        // Add new entry
#ifdef DH_USE_MEMOIZED_HASH
        entry->ikey = ikey;
#endif
        entry->key = key;
        memcpy(&entry->value_obj, value, hash->value_size);

        CLEAR_DELETED(entry); // Clear tombstone
        SET_PROBE_COUNT(entry, probes);
        hash->used_buckets++;
        return true;
      }

      // Time to steal from the rich
      // Swap with the current bucket and find a new home for the evicted item

      titem = *entry;

#ifdef DH_USE_MEMOIZED_HASH
      entry->ikey        = ikey;
#endif
      entry->key         = key;
      SET_PROBE_COUNT(entry, probes);

#ifdef DH_USE_MEMOIZED_HASH
      ikey   = titem.ikey;
#endif
      key    = titem.key;
      probes = PROBE_COUNT(&titem);

      swap_values((uintptr_t *)&entry->value_obj, (uintptr_t *)value, hash->value_size);
    }

    // Continue linear probe
    b = next_bucket(b, hash->prime_ix);
    entry = dh__get_entry_unsafe(hash, b);

    if(probes < MAX_PROBE_COUNT)
      probes++;
    else
      return false;
  }

}


// Insert without existing ikey
static inline bool dh__insert(dhash *hash, dhKey key, void *value) {
  return dh__insert_ex(hash, key, value, dh__hash(hash, key));
}



// Expand size of hash bucket array
static inline bool dh__grow(dhash *hash, dhBucketIndex new_buckets) {
  if(hash->static_buckets)
    return false;

  dhBucketIndex num_old_buckets = hash->num_buckets;

  if(new_buckets <= num_old_buckets)
    new_buckets = num_old_buckets+1; // This will round up to the next prime size

  //printf("## GROW HASH: %d\n", new_buckets);

  // Disconnect bucket array so we can restore it if grow fails
  dhBucketEntry *old_buckets = hash->buckets;
  hash->buckets = NULL;

  dhConfig cfg = {
    .init_buckets = new_buckets,
    .value_size   = hash->value_size,
    .max_storage  = hash->max_storage,
    .destroy_item = hash->destroy_item,
    .gen_hash     = hash->gen_hash,
    .is_equal     = hash->is_equal,
    .grow_hash    = hash->grow_hash
  };

  // Grow to next prime size
  if(!dh__init(hash, &cfg, hash->ctx, /*new_hash*/false)) {
    hash->buckets = old_buckets; // Restore and abort attempt to grow
    printf("\t Hash grow failed\n");
    return false;
  }

  //printf("## GROW: %zu\n", hash->num_buckets));

  // Copy old items
  for(dhBucketIndex b = 0; b < num_old_buckets; b++) {
    dhBucketEntry *item = &old_buckets[b];

    if(IN_USE(item) && !WAS_DELETED(item)) {
#ifdef DH_USE_MEMOIZED_HASH
      dh__insert_ex(hash, item->key, &item->value_obj, item->ikey); // Skip rehash of key
#else
      dh__insert(hash, item->key, &item->value_obj);
#endif
    }
  }

  dh__free(old_buckets);
  return true;
}



/*
Add a new hash entry

If the key already exists the configured destroy_item callback will be
called with the old value and then replaced with the new value.

Args:
  hash:   Hash to insert into
  key:    Key for new value
  value:  value object to associate with the key

Returns:
  true on success
*/
bool dh_insert(dhash *hash, dhKey key, void *value) {
  // Check if we have too much load
  dhBucketIndex max_buckets = MAX_LOAD_FACTOR(hash->num_buckets); // ~ 90%

  if(hash->used_buckets >= max_buckets) { // Load is too high
    //printf("#### REHASH %d\n", hash->used_buckets);
    //dh_dump(hash);
    if(!dh__grow(hash, 0)) return false;
  }

  return dh__insert(hash, key, value);
}



/*
Remove a hash entry

If the value parameter is provided the removed entry is returned.
Otherwise it is destroyed via the destroy_item() callback.

Args:
  hash:   Hash to remove from
  key:    Key for item to remove
  value:  Optional found value matching the key on success

Returns:
  true on success
*/
bool dh_remove(dhash *hash, dhKey key, void *value) {
  dhBucketEntry *entry = NULL;
  dh__find_bucket(hash, key, &entry);

  if(entry) { // Bucket found with matching key
    // Return removed value if caller wants to manage it, otherwise destroy it
    // after we finish internal bookkeeping.
    if(value)
      memcpy(value, &entry->value_obj, hash->value_size);

#ifdef DH_USE_MEMOIZED_HASH
    entry->ikey = 0;
#endif
    entry->key.data = NULL;
    entry->key.length = 0;

    // We need to preserve the probe count so turn this into a tombstone
    SET_DELETED(entry);
    hash->used_buckets--;

    if(!value)
      hash->destroy_item(entry->key, &entry->value_obj, hash->ctx);

    memset(&entry->value_obj, 0, hash->value_size);

    return true;
  }

  return false;
}


// ******************** Resource utilization ********************

/*
Count the number of items in a hash

Args:
  hash:   Hash to count items in

Returns:
  Number of key/value entrys in the hash
*/
size_t dh_num_items(dhash *hash) {
  return hash->used_buckets;
}


/*
Current maximum capacity of a hash

Args:
  hash:   Hash to measure capacity

Returns:
  Max number of key/value entrys before growth is triggered
*/
size_t dh_cur_capacity(dhash *hash) {
  return MAX_LOAD_FACTOR(hash->num_buckets);
}


/*
Load factor of the hash table

Args:
  hash:   Hash to measure load factor on

Returns:
  Load factor scaled by 100
*/
int dh_load_factor(dhash *hash) {
  if(hash->num_buckets == 0)
    return 0;

  return (hash->used_buckets * 100 + 50) / hash->num_buckets;
}




/*
Reserve extra entry space for future use

This bypasses the slow growth of the bucket array when a known
number of insertions are about to be performed.

Args:
  hash:         Hash to increase capacity
  add_capacity: Number of additional entries to support

Returns:
  true on success
*/
bool dh_reserve_capacity(dhash *hash, size_t add_capacity) {
  if(add_capacity == 0)
    return true;

  size_t hash_capacity = dh_cur_capacity(hash);
  size_t free_buckets = hash_capacity - dh_num_items(hash);

  if(add_capacity <= free_buckets) // Enough buckets exist
    return true;

  if(hash->static_buckets) // Can't grow
    return false;

  add_capacity -= free_buckets;

  // Add space for extra buckets
  const size_t lfactor = MAX_LOAD_FACTOR(128UL); // Get the numerator
  size_t new_buckets = (hash_capacity + add_capacity) * 128UL / lfactor;

  return dh__grow(hash, new_buckets);
}



// ******************** Utility ********************


/*
Get the average number of probes for each used hash bucket

Args:
  hash:   Hash to scan for probe count

Returns:
  Average number of probes scaled by 100
*/
int dh_mean_probe_count(dhash *hash) {
  int total = 0;

  dhBucketIndex num_buckets = hash->num_buckets;
  dhBucketEntry *entry;

  for(dhBucketIndex b = 0; b < num_buckets; b++ ) {
    entry = dh__get_entry_unsafe(hash, b);
    if(!IN_USE(entry) || WAS_DELETED(entry)) continue; // Skip unused and tombstones

    total += PROBE_COUNT(entry);
  }

  // Scale by 100 to avoid floats on embedded
  int mean = 0;
  if(hash->used_buckets > 0)
    mean = ((long)total * 100 + 50) / hash->used_buckets;

  return mean;
}


/*
Get the maximum number of probes for each used hash bucket

Args:
  hash:   Hash to scan for probe count

Returns:
  Maximum number of probes
*/
int dh_max_probe_count(dhash *hash) {
  int max_probes = 0;
  dhBucketIndex num_buckets = hash->num_buckets;
  dhBucketEntry *entry;

  for(dhBucketIndex b = 0; b < num_buckets; b++ ) {
    entry = dh__get_entry_unsafe(hash, b);
    if(!IN_USE(entry) || WAS_DELETED(entry)) continue; // Skip unused and tombstones

    max_probes = MAX(max_probes, PROBE_COUNT(entry));
  }

  return max_probes;
}



/*
Print a dump of internal data on the hash

Args:
  hash:       Hash to dump
  print_item: Callback for printing info about the key/value items
  ctx:        Optional user data for the callback
*/
void dh_dump(dhash *hash, HashVisitor print_item, void *ctx) {
  dhBucketIndex num_buckets = hash->num_buckets;
  dhBucketEntry *entry;

  puts("Hash dump:");
  for(int b = 0; b < num_buckets; b++) {
    entry = dh__get_entry_unsafe(hash, b);
    if(!IN_USE(entry)) continue;

    printf("  %3d: k=%4d v=%p (%zu), flag=%01X probes=%d init=%d\n", b, 
#ifdef DH_USE_MEMOIZED_HASH
      entry->ikey,
#else
      -1,
#endif
      &entry->value_obj, (uintptr_t)entry->value_obj,
      //entry->flags,
      entry->deleted,
      PROBE_COUNT(entry),
#ifdef DH_USE_MEMOIZED_HASH
      dh__initial_probe(hash, entry->ikey)
#else
      -1
#endif
    );

    if(print_item && IN_USE(entry) && !WAS_DELETED(entry))
      print_item(entry->key, &entry->value_obj, ctx);
  }

  int mean = dh_mean_probe_count(hash);
  printf("\nMean probes: %d.%d\n", mean / 100, mean % 100);
}



/*
Iterate over all occupied buckets in a hash

Args:
  hash:     Hash to iterate
  visitor:  Callback for each used bucket
  ctx:      Optional user data for the callback
*/
void dh_foreach(dhash *hash, HashVisitor visitor, void *ctx) {
  dhBucketIndex num_buckets = hash->num_buckets;
  dhBucketEntry *entry;
  for(dhBucketIndex b = 0; b < num_buckets; b++) {
    entry = dh__get_entry_unsafe(hash, b);
    if(!IN_USE(entry))
      continue;

    if(!visitor(entry->key, &entry->value_obj, ctx))
      break;
  }
}




#if 0

void destroy_hashed(dhKey key, void *value) {
  //printf("### DESTROY: %d\n", (uintptr_t)value);
}

int main(int argc, char *argv[]) {
  dhash *h;

  dhConfig cfg = {
    .init_buckets = 11,
    .destroy_item = destroy_hashed
  };

  h = calloc(1, sizeof(dhash));

  printf("X\n");
  dh_init(h, &cfg);
  printf("Y\n");


#define KEY(n) (dhKey){.data=(void *)(uintptr_t)(n), .length=1}

  dh_insert(h, KEY(1), (void *)(41));
  dh_insert(h, KEY(2), (void *)(43));
  dh_insert(h, KEY(3), (void *)(44));
  dh_insert(h, KEY(40), (void *)(42));
  dh_insert(h, KEY(50), (void *)(42));
  dh_insert(h, KEY(60), (void *)(42));
  dh_insert(h, KEY(70), (void *)(42));
  dh_insert(h, KEY(80), (void *)(42));
  dh_insert(h, KEY(90), (void *)(42));
  dh_insert(h, KEY(100), (void *)(42));
  dh_insert(h, KEY(110), (void *)(42));
  dh_insert(h, KEY(120), (void *)(42));
  dh_insert(h, KEY(130), (void *)(422));
  dh_insert(h, KEY(140), (void *)(42));
  dh_insert(h, KEY(150), (void *)(42));
  dh_insert(h, KEY(4), (void *)(45));
  dh_insert(h, KEY(5), (void *)(45));
  dh_insert(h, KEY(6), (void *)(45));
  dh_insert(h, KEY(7), (void *)(45));

  dh_insert(h, KEY(3), (void *)(45));
  dh_insert(h, KEY(3), (void *)(46));
  //dh_insert(h, 3, (void *)(47), NULL);
  //dh_insert(h, 3, (void *)(48), NULL);

  dh_insert(h, KEY(200), (void *)(48));
  dh_insert(h, KEY(201), (void *)(48));
  dh_insert(h, KEY(202), (void *)(48));
  dh_insert(h, KEY(203), (void *)(48));
  dh_insert(h, KEY(204), (void *)(48));
  dh_insert(h, KEY(205), (void *)(48));
  dh_insert(h, KEY(206), (void *)(48));
  dh_insert(h, KEY(207), (void *)(48));
  dh_insert(h, KEY(208), (void *)(48));
  dh_insert(h, KEY(209), (void *)(48));
  dh_insert(h, KEY(210), (void *)(48));
  dh_insert(h, KEY(211), (void *)(48));
  dh_insert(h, KEY(212), (void *)(48));
  dh_insert(h, KEY(213), (void *)(48));
  dh_insert(h, KEY(214), (void *)(48));
  dh_insert(h, KEY(215), (void *)(48));
  dh_insert(h, KEY(216), (void *)(48));
  dh_insert(h, KEY(217), (void *)(48));
  dh_insert(h, KEY(218), (void *)(48));
  dh_insert(h, KEY(219), (void *)(48));


  void *vpv;
  dh_lookup(h, KEY(1), &vpv); printf("lookup: k %d -> %zu\n", 1, (uintptr_t)vpv);
  dh_lookup(h, KEY(2), &vpv); printf("lookup: k %d -> %zu\n", 2, (uintptr_t)vpv);

/*

  for(uintptr_t i = 0; i < 2; i++) {
    //printf("| insert %d\n", i);
    dh_insert(&h, i, (void *)(i*10));
  }


  dh_remove(&h, 5);
  dh_remove(&h, 6);
  dh_remove(&h, 7);


  printf("A\n");
  for(uintptr_t i = 0; i < 9; i++) {
    dh_insert(&h, i+100, (void *)(i*100));
  }
  printf("B\n");

  void *v;
  for(uintptr_t i = 0; i < 10; i++) {
    bool found = dh_lookup(&h, i, &v);
    printf("Lookup %d = %d  %d\n", i, (uintptr_t)v, found);
  }
  printf("C\n");
*/

  dh_dump(h, NULL, NULL);

  dh_free(h);
  free(h);

  printf("## Exhaustive test");

  dhash h2, h3;
  uintptr_t max_items = 100;
  uintptr_t offset = 0; //24984087;

  dh_init(&h2, &cfg);

  cfg.init_buckets = max_items*3;
  dh_init(&h3, &cfg);

  for(uintptr_t i = 0; i < max_items; i++) {
    dh_insert(&h2, KEY(i+offset), (void *)(i+offset));
    dh_insert(&h3, KEY(i+offset), (void *)(i+offset));
  }

  uintptr_t h2_val, h3_val;

  int matches = 0;
  for(uintptr_t i = 0; i < max_items; i++) {
    dh_lookup(&h2, KEY(i+offset), (void **)&h2_val);
    dh_lookup(&h3, KEY(i+offset), (void **)&h3_val);
    if(h2_val != h3_val || h2_val != i+offset)
      printf("ERROR: Mismatch on %ld\n", i+offset);
    else
      matches++;
  }

  printf("\t%d match\n", matches);

  dh_dump(&h2, NULL, NULL);

  dh_free(&h2);
  dh_free(&h3);


  return 0;
}
#endif
