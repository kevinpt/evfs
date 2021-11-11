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
------------------------------------------------------------------------------
*/

#ifndef DHASH_H
#define DHASH_H

#include <inttypes.h>

// Set the largest number of hash entries to support.
// This is primarily to help 8 and 16-bit platforms use a smaller data type
// for handling bucket indices.

#define DH_MAX_HASH_ENTRIES  INT32_MAX


// Bucket indices are signed so that -1 can indicate a failed lookup
#if DH_MAX_HASH_ENTRIES > INT32_MAX
typedef int64_t  dhBucketIndex;
#  define PRIBkt  PRId64
#elif DH_MAX_HASH_ENTRIES > INT16_MAX
typedef int32_t  dhBucketIndex;
#  define PRIBkt  PRId32
#else
typedef int16_t  dhBucketIndex;
#  define PRIBkt  PRId16
#  define DH_INDEX_16BIT
#endif


typedef struct dhKey {
  const void *data;
  size_t length;
} dhKey;


typedef uint32_t dhIKey;  // Hashed dhKey


// Callback signatures
typedef void   (*ItemDestructor)(dhKey key, void *value, void *ctx);
typedef bool   (*ItemReplace)(dhKey key, void *old_value, void *new_value, void *ctx);
typedef dhIKey (*ComputeHash)(dhKey key);
typedef bool   (*EqualKeys)(dhKey key1, dhKey key2, void *ctx);
typedef bool   (*GrowHash)(size_t max_items, void *ctx);

// Configuration settings passed to dh_init()
typedef struct dhConfig {
  size_t          init_buckets; // Initial number of buckets for the hash
  size_t          value_size;   // Bytes per entry value
  size_t          max_storage;  // Max bytes to use for bucket array

  void            *ext_storage; // Optional external buffer for buckets; Size in max_storage

  // Callbacks
  ItemDestructor  destroy_item; // Required callback for evicted entries
  ComputeHash     gen_hash;     // Required callback to convert dhKey into dhIKey
  EqualKeys       is_equal;     // Required callback to test if two dhKeys match (on ikey collision)
  ItemReplace     replace_item; // Optional callback for replaced entries
  GrowHash        grow_hash;    // Optional callback to notify increase in hash size
} dhConfig;


typedef struct dhash {
  // Bucket handling
  void           *buckets;      // Dynamic array of struct dhPair
  dhBucketIndex   num_buckets;

  dhBucketIndex   used_buckets; // Number of buckets in use
  dhBucketIndex   prime_ix;     // Index into table or primes for bucket array sizes

  // Configuration
  size_t          value_size;   // Bytes per entry value
  size_t          max_storage;  // Max bytes to use for bucket array

  // Callbacks
  void           *ctx;          // User context for callbacks
  ItemDestructor  destroy_item; // Required callback for evicted entries
  ComputeHash     gen_hash;     // Required callback to convert dhKey into dhIKey
  EqualKeys       is_equal;     // Required callback to test if two dhKeys match (on ikey collision)
  ItemReplace     replace_item; // Optional callback for replaced entries
  GrowHash        grow_hash;    // Optional callback to notify increase in hash size

  bool            static_buckets; // Buckets array is from ext_storage

} dhash;


typedef struct {
  dhash *hash;
  dhBucketIndex bucket;
} dhIter;


typedef bool (*HashVisitor)(dhKey key, void *value, void *ctx);


#ifdef __cplusplus
extern "C" {
#endif

// ******************** Resource management ********************
bool dh_init(dhash *hash, dhConfig *config, void *ctx);
void dh_free(dhash *hash);

// ******************** Retrieval ********************
bool dh_lookup(dhash *hash, dhKey key, void *value);
#define dh_exists(h, k)  dh_lookup(h, k, NULL)
bool dh_lookup_in_place(dhash *hash, dhKey key, void **value);

void dh_iter_init(dhash *hash, dhIter *it);
bool dh_iter_next(dhIter *it, dhKey *key, void **value);
//bool dh_iter_remove(dhIter *it, void *value);

// ******************** Storage ********************
bool dh_insert(dhash *hash, dhKey key, void *value);
bool dh_remove(dhash *hash, dhKey key, void *value);
#define dh_delete(hash, key)  dh_remove(hash, key, NULL)

// ******************** Resource utilization ********************
size_t dh_num_items(dhash *hash);
size_t dh_cur_capacity(dhash *hash);
int dh_load_factor(dhash *hash);
bool dh_reserve_capacity(dhash *hash, size_t add_capacity);


// ******************** Utility ********************
int dh_mean_probe_count(dhash *hash);
int dh_max_probe_count(dhash *hash);

void dh_dump(dhash *hash, HashVisitor print_item, void *ctx);

void dh_foreach(dhash *hash, HashVisitor visitor, void *ctx);

dhIKey dh_gen_hash_string(dhKey key);
bool dh_equal_hash_keys_string(dhKey key1, dhKey key2, void *ctx);
dhIKey dh_gen_hash_int(dhKey key);
bool dh_equal_hash_keys_int(dhKey key1, dhKey key2, void *ctx);

#ifdef __cplusplus
}
#endif


#endif // DHASH_H
