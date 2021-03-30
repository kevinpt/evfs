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
Get and set data from (potentially) unaligned pointers.

This implements inlinable functions for 16, 32, and 64-bit unsigned and signed
integers. Source data can be handled in native byte order or big/little-endian.

When compiled with C11, generic macros are available that use the type of the
data pointer to select the appropriate integer size function. An equivalent
set of C++ templates are provided that implement the same functionality.

------------------------------------------------------------------------------
*/

#ifndef UNALIGNED_ACCESS_H
#define UNALIGNED_ACCESS_H

#include <stdint.h>
#include <string.h>


#if defined __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-align"

#elif defined __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcast-align"
#endif

// ******************** uint16_t ********************

static inline uint16_t get_unaligned_u16(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  uint16_t val;

  memcpy(&val, bdata, sizeof(uint16_t));
  return val;
}

static inline uint16_t get_unaligned_u16be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint16_t val = 0;

  val |= (uint16_t)bdata[1];
  val |= (uint16_t)bdata[0] << 8;
  return val;
}

static inline uint16_t get_unaligned_u16le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint16_t val = 0;

  val |= (uint16_t)bdata[0];
  val |= (uint16_t)bdata[1] << 8;
  return val;
}


static inline void set_unaligned_u16(uint16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_u16be(uint16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[1] = (uint8_t)(value & 0xFF);
  bdest[0] = (uint8_t)((value >> 8) & 0xFF);
}

static inline void set_unaligned_u16le(uint16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
}


// ******************** int16_t ********************

static inline int16_t get_unaligned_s16(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  int16_t val;

  memcpy(&val, bdata, sizeof(int16_t));
  return val;
}

static inline int16_t get_unaligned_s16be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int16_t val = 0;

  val |= (int16_t)bdata[1];
  val |= (int16_t)bdata[0] << 8;
  return val;
}

static inline int16_t get_unaligned_s16le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int16_t val = 0;

  val |= (int16_t)bdata[0];
  val |= (int16_t)bdata[1] << 8;
  return val;
}


static inline void set_unaligned_s16(int16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_s16be(int16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[1] = (uint8_t)(value & 0xFF);
  bdest[0] = (uint8_t)((value >> 8) & 0xFF);
}

static inline void set_unaligned_s16le(int16_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
}



// ******************** uint32_t ********************

static inline uint32_t get_unaligned_u32(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  uint32_t val;

  memcpy(&val, bdata, sizeof(uint32_t));
  return val;
}

static inline uint32_t get_unaligned_u32be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint32_t val = 0;

  val |= (uint32_t)bdata[3];
  val |= (uint32_t)bdata[2] << 8;
  val |= (uint32_t)bdata[1] << 16;
  val |= (uint32_t)bdata[0] << 24;
  return val;
}

static inline uint32_t get_unaligned_u32le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint32_t val = 0;

  val |= (uint32_t)bdata[0];
  val |= (uint32_t)bdata[1] << 8;
  val |= (uint32_t)bdata[2] << 16;
  val |= (uint32_t)bdata[3] << 24;
  return val;
}


static inline void set_unaligned_u32(uint32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_u32be(uint32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[3] = (uint8_t)(value & 0xFF);
  bdest[2] = (uint8_t)((value >> 8) & 0xFF);
  bdest[1] = (uint8_t)((value >> 16) & 0xFF);
  bdest[0] = (uint8_t)((value >> 24) & 0xFF);
}

static inline void set_unaligned_u32le(uint32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
  bdest[2] = (uint8_t)((value >> 16) & 0xFF);
  bdest[3] = (uint8_t)((value >> 24) & 0xFF);

}


// ******************** int32_t ********************

static inline int32_t get_unaligned_s32(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  int32_t val;

  memcpy(&val, bdata, sizeof(int32_t));
  return val;
}

static inline int32_t get_unaligned_s32be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int32_t val = 0;

  val |= (int32_t)bdata[3];
  val |= (int32_t)bdata[2] << 8;
  val |= (int32_t)bdata[1] << 16;
  val |= (int32_t)bdata[0] << 24;
  return val;
}

static inline int32_t get_unaligned_s32le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int32_t val = 0;

  val |= (int32_t)bdata[0];
  val |= (int32_t)bdata[1] << 8;
  val |= (int32_t)bdata[2] << 16;
  val |= (int32_t)bdata[3] << 24;
  return val;
}


static inline void set_unaligned_s32(int32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_s32be(int32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[3] = (uint8_t)(value & 0xFF);
  bdest[2] = (uint8_t)((value >> 8) & 0xFF);
  bdest[1] = (uint8_t)((value >> 16) & 0xFF);
  bdest[0] = (uint8_t)((value >> 24) & 0xFF);
}

static inline void set_unaligned_s32le(int32_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
  bdest[2] = (uint8_t)((value >> 16) & 0xFF);
  bdest[3] = (uint8_t)((value >> 24) & 0xFF);
}


// ******************** uint64_t ********************

static inline uint32_t get_unaligned_u64(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  uint64_t val;

  memcpy(&val, bdata, sizeof(uint64_t));
  return val;
}

static inline uint64_t get_unaligned_u64be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint64_t val = 0;

  val |= (uint64_t)bdata[7];
  val |= (uint64_t)bdata[6] << 8;
  val |= (uint64_t)bdata[5] << 16;
  val |= (uint64_t)bdata[4] << 24;
  val |= (uint64_t)bdata[3] << 32;
  val |= (uint64_t)bdata[2] << 40;
  val |= (uint64_t)bdata[1] << 48;
  val |= (uint64_t)bdata[0] << 56;
  return val;
}

static inline uint64_t get_unaligned_u64le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  uint64_t val = 0;

  val |= (uint64_t)bdata[0];
  val |= (uint64_t)bdata[1] << 8;
  val |= (uint64_t)bdata[2] << 16;
  val |= (uint64_t)bdata[3] << 24;
  val |= (uint64_t)bdata[4] << 32;
  val |= (uint64_t)bdata[5] << 40;
  val |= (uint64_t)bdata[6] << 48;
  val |= (uint64_t)bdata[7] << 56;
  return val;
}


static inline void set_unaligned_u64(uint64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_u64be(uint64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[7] = (uint8_t)(value & 0xFF);
  bdest[6] = (uint8_t)((value >> 8) & 0xFF);
  bdest[5] = (uint8_t)((value >> 16) & 0xFF);
  bdest[4] = (uint8_t)((value >> 24) & 0xFF);
  bdest[3] = (uint8_t)((value >> 32) & 0xFF);
  bdest[2] = (uint8_t)((value >> 40) & 0xFF);
  bdest[1] = (uint8_t)((value >> 48) & 0xFF);
  bdest[0] = (uint8_t)((value >> 56) & 0xFF);
}

static inline void set_unaligned_u64le(uint64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
  bdest[2] = (uint8_t)((value >> 16) & 0xFF);
  bdest[3] = (uint8_t)((value >> 24) & 0xFF);
  bdest[4] = (uint8_t)((value >> 32) & 0xFF);
  bdest[5] = (uint8_t)((value >> 40) & 0xFF);
  bdest[6] = (uint8_t)((value >> 48) & 0xFF);
  bdest[7] = (uint8_t)((value >> 56) & 0xFF);
}


// ******************** int64_t ********************

static inline int64_t get_unaligned_s64(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  int64_t val;

  memcpy(&val, bdata, sizeof(int64_t));
  return val;
}

static inline int64_t get_unaligned_s64be(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int64_t val = 0;

  val |= (int64_t)bdata[7];
  val |= (int64_t)bdata[6] << 8;
  val |= (int64_t)bdata[5] << 16;
  val |= (int64_t)bdata[4] << 24;
  val |= (int64_t)bdata[3] << 32;
  val |= (int64_t)bdata[2] << 40;
  val |= (int64_t)bdata[1] << 48;
  val |= (int64_t)bdata[0] << 56;
  return val;
}

static inline int64_t get_unaligned_s64le(const void *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  int64_t val = 0;

  val |= (int64_t)bdata[0];
  val |= (int64_t)bdata[1] << 8;
  val |= (int64_t)bdata[2] << 16;
  val |= (int64_t)bdata[3] << 24;
  val |= (int64_t)bdata[4] << 32;
  val |= (int64_t)bdata[5] << 40;
  val |= (int64_t)bdata[6] << 48;
  val |= (int64_t)bdata[7] << 56;
  return val;
}


static inline void set_unaligned_s64(int64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(value));
}

static inline void set_unaligned_s64be(int64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[7] = (uint8_t)(value & 0xFF);
  bdest[6] = (uint8_t)((value >> 8) & 0xFF);
  bdest[5] = (uint8_t)((value >> 16) & 0xFF);
  bdest[4] = (uint8_t)((value >> 24) & 0xFF);
  bdest[3] = (uint8_t)((value >> 32) & 0xFF);
  bdest[2] = (uint8_t)((value >> 40) & 0xFF);
  bdest[1] = (uint8_t)((value >> 48) & 0xFF);
  bdest[0] = (uint8_t)((value >> 56) & 0xFF);
}

static inline void set_unaligned_s64le(int64_t value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  bdest[0] = (uint8_t)(value & 0xFF);
  bdest[1] = (uint8_t)((value >> 8) & 0xFF);
  bdest[2] = (uint8_t)((value >> 16) & 0xFF);
  bdest[3] = (uint8_t)((value >> 24) & 0xFF);
  bdest[4] = (uint8_t)((value >> 32) & 0xFF);
  bdest[5] = (uint8_t)((value >> 40) & 0xFF);
  bdest[6] = (uint8_t)((value >> 48) & 0xFF);
  bdest[7] = (uint8_t)((value >> 56) & 0xFF);
}


// C11 is required for generics
#if __STDC_VERSION__ >= 201112L
#  define get_unaligned(p)  _Generic((p), \
      uint16_t *: get_unaligned_u16, \
      int16_t * : get_unaligned_s16, \
      uint32_t *: get_unaligned_u32,  \
      int32_t * : get_unaligned_s32,  \
      uint64_t *: get_unaligned_u64,  \
      int64_t * : get_unaligned_s64  \
    )(p)

#  define get_unaligned_be(p)  _Generic((p), \
      uint16_t *: get_unaligned_u16be, \
      int16_t * : get_unaligned_s16be, \
      uint32_t *: get_unaligned_u32be,  \
      int32_t * : get_unaligned_s32be,  \
      uint64_t *: get_unaligned_u64be,  \
      int64_t * : get_unaligned_s64be  \
    )(p)

#  define get_unaligned_le(p)  _Generic((p), \
      uint16_t *: get_unaligned_u16le, \
      int16_t * : get_unaligned_s16le, \
      uint32_t *: get_unaligned_u32le,  \
      int32_t * : get_unaligned_s32le,  \
      uint64_t *: get_unaligned_u64le,  \
      int64_t * : get_unaligned_s64le  \
    )(p)



#  define set_unaligned(v, d)  _Generic((v), \
      uint16_t: set_unaligned_u16, \
      int16_t : set_unaligned_s16, \
      uint32_t: set_unaligned_u32,  \
      int32_t : set_unaligned_s32,  \
      uint64_t: set_unaligned_u64,  \
      int64_t : set_unaligned_s64  \
    )((v), (d))

#  define set_unaligned_be(v, d)  _Generic((v), \
      uint16_t: set_unaligned_u16be, \
      int16_t : set_unaligned_s16be, \
      uint32_t: set_unaligned_u32be,  \
      int32_t : set_unaligned_s32be,  \
      uint64_t: set_unaligned_u64be,  \
      int64_t : set_unaligned_s64be  \
    )((v), (d))

#  define set_unaligned_le(v, d)  _Generic((v), \
      uint16_t: set_unaligned_u16le, \
      int16_t : set_unaligned_s16le, \
      uint32_t: set_unaligned_u32le,  \
      int32_t : set_unaligned_s32le,  \
      uint64_t: set_unaligned_u64le,  \
      int64_t : set_unaligned_s64le  \
    )((v), (d))


#elif defined __cplusplus

template <class T>
static T get_unaligned(const T *data) {
  const uint8_t *bdata = (const uint8_t *)data; // Don't allow any assumed alignment 
  T val;

  memcpy(&val, bdata, sizeof(T));
  return val;
}


template <class T>
static T get_unaligned_be(const T *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  T val = 0;

  constexpr int max_byte = sizeof(T) - 1;
  static_assert(max_byte < 8, "Integer is larger than 64-bits");

  val |= (T)bdata[max_byte];

  if(max_byte > 0) { // 16-bits or more
    val |= (T)bdata[max_byte-1] << 8;

    if(max_byte > 1) {  // 32-bits or more
      val |= (T)bdata[max_byte-2] << 16;
      val |= (T)bdata[max_byte-3] << 24;

      if(max_byte > 3) { // 64-bits
        val |= (T)bdata[max_byte-4] << 32;
        val |= (T)bdata[max_byte-5] << 40;
        val |= (T)bdata[max_byte-6] << 48;
        val |= (T)bdata[max_byte-7] << 56;
      }
    }
  }

  return val;
}


template <class T>
static T get_unaligned_le(const T *data) {
  const uint8_t *bdata = (const uint8_t *)data;
  T val = 0;

  constexpr int max_byte = sizeof(T) - 1;
  static_assert(max_byte < 8, "Integer is larger than 64-bits");

  val |= (T)bdata[0];

  if(max_byte > 0) { // 16-bits or more
    val |= (T)bdata[1] << 8;

    if(max_byte > 1) {  // 32-bits or more
      val |= (T)bdata[2] << 16;
      val |= (T)bdata[3] << 24;

      if(max_byte > 3) { // 64-bits
        val |= (T)bdata[4] << 32;
        val |= (T)bdata[5] << 40;
        val |= (T)bdata[6] << 48;
        val |= (T)bdata[7] << 56;
      }
    }
  }

  return val;
}


template <class T>
static void set_unaligned(const T value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest; // Don't allow any assumed alignment 
  memcpy(bdest, &value, sizeof(T));
}


template <class T>
static void set_unaligned_be(const T value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  constexpr int max_byte = sizeof(T) - 1;
  static_assert(max_byte < 8, "Integer is larger than 64-bits");

  if(max_byte > 3) { // 64-bits
    bdest[max_byte-7] = (uint8_t)((value >> 56) & 0xFF);
    bdest[max_byte-6] = (uint8_t)((value >> 48) & 0xFF);
    bdest[max_byte-5] = (uint8_t)((value >> 40) & 0xFF);
    bdest[max_byte-4] = (uint8_t)((value >> 32) & 0xFF);
  }
  if(max_byte > 1) { // 32-bits
    bdest[max_byte-3] = (uint8_t)((value >> 24) & 0xFF);
    bdest[max_byte-2] = (uint8_t)((value >> 16) & 0xFF);
  }
  if(max_byte > 0) { // 16-bits
    bdest[max_byte-1] = (uint8_t)((value >> 8) & 0xFF);
  }
  bdest[max_byte] = (uint8_t)(value & 0xFF);
}


template <class T>
static void set_unaligned_le(const T value, void *dest) {
  uint8_t *bdest = (uint8_t *)dest;

  constexpr int max_byte = sizeof(T) - 1;
  static_assert(max_byte < 8, "Integer is larger than 64-bits");

  if(max_byte > 3) { // 64-bits
    bdest[7] = (uint8_t)((value >> 56) & 0xFF);
    bdest[6] = (uint8_t)((value >> 48) & 0xFF);
    bdest[5] = (uint8_t)((value >> 40) & 0xFF);
    bdest[4] = (uint8_t)((value >> 32) & 0xFF);
  }
  if(max_byte > 1) { // 32-bits
    bdest[3] = (uint8_t)((value >> 24) & 0xFF);
    bdest[2] = (uint8_t)((value >> 16) & 0xFF);
  }
  if(max_byte > 0) { // 16-bits
    bdest[1] = (uint8_t)((value >> 8) & 0xFF);
  }
  bdest[0] = (uint8_t)(value & 0xFF);
}


#endif



#if defined __GNUC__
#  pragma GCC diagnostic pop

#elif defined __clang__
#  pragma clang diagnostic pop
#endif


#endif // UNALIGNED_ACCESS_H

