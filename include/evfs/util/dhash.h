
#ifndef DHASH_H
#define DHASH_H

// Set the largest number of hash entries to support.
// This is primarily to help 8 and 16-bit platforms use a smaller data type
// for handling bucket indices.

#define DH_MAX_HASH_ENTRIES  INT32_MAX



// Bucket indices are signed so that -1 can indicate a failed lookup
#if DH_MAX_HASH_ENTRIES > INT32_MAX
typedef int64_t  dhBucketIndex;
#elif DH_MAX_HASH_ENTRIES > INT16_MAX
typedef int32_t  dhBucketIndex;
#else
typedef int16_t  dhBucketIndex;
#  define DH_INDEX_16BIT
#endif


typedef struct dhKey {
  const void *data;
  size_t length;
} dhKey;


typedef uint32_t dhIKey;  // Hashed dhKey


// Callback signatures
typedef void   (*ItemDestructor)(dhKey key, void *value, void *ctx);
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
  ComputeHash     gen_hash;     // Optional callback to convert dhKey into dhIKey
  EqualKeys       is_equal;     // Optional callback to test if two dhKeys match (on ikey collision)
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
  ComputeHash     gen_hash;     // Optional callback to convert dhKey into dhIKey
  EqualKeys       is_equal;     // Optional callback to test if two dhKeys match (on ikey collision)
  GrowHash        grow_hash;    // Optional callback to notify increase in hash size

  bool            static_buckets; // Buckets array is from ext_storage

} dhash;


typedef bool (*HashVisitor)(dhKey key, void *value, void *ctx);

// ******************** Resource management ********************
bool dh_init(dhash *hash, dhConfig *config, void *ctx);
void dh_free(dhash *hash);

// ******************** Retrieval ********************
bool dh_lookup(dhash *hash, dhKey key, void *value);
#define dh_exists(h, k)  dh_lookup(h, k, NULL)

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


#endif // DHASH_H
