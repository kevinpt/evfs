/* SPDX-License-Identifier: MIT
Copyright 2020 Kevin Thibedeau
(kevin 'period' thibedeau 'at' gmail 'punto' com)

See LICENSE in the EVFS project root for details
*/

/*
------------------------------------------------------------------------------
Embedded Virtual Filesystem

  Common path operations
------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "evfs.h"
#include "evfs_internal.h"

#include "evfs/util/glob.h"


static bool normalize_root_component(Evfs *vfs, char *path, StringRange *root) {
  bool is_absolute = vfs->m_path_root_component(vfs, path, root);

  if(is_absolute) { // Trim redundant slashes from range
    const char *start = root->start;
    char *pos = (char *)root->end-1;

    while(pos > start && char_match(*pos, EVFS_PATH_SEPS)) {
      pos--;
    }

    if(pos > start) // Root has non-slash prefix (drive letter); grow to restore one slash
      pos++;

    root->end = pos+1; // Trim range
    if(*pos == '\\')
      *pos = EVFS_DIR_SEP;
  }

  return is_absolute;
}


static bool append_normalized_root(Evfs *vfs, AppendRange *dest, StringRange *root) {
    bool truncated;

    // Copy the un-normalized root
    if(range_size(root) < 16) {
      char root_buf[16] = {0};
      strncpy(root_buf, root->start, range_size(root));
      normalize_root_component(vfs, root_buf, root);
      if(range_cat_range_no_nul(dest, root) < 0)
        truncated = true;

    } else { // Fallback to malloc
      char *root_norm = evfs_malloc(range_size(root)+1);
      if(MEM_CHECK(root_norm)) { // Failed malloc
        // Just append the root without normalization
        if(range_cat_range_no_nul(dest, root) < 0)
          truncated = true;

      } else { // Success
        memset(root_norm, 0, range_size(root)+1);
        strncpy(root_norm, root->start, range_size(root));
        normalize_root_component(vfs, root_norm, root);
        
        if(range_cat_range_no_nul(dest, root) < 0)
          truncated = true;

        evfs_free(root_norm);
      }
    }

  return truncated;
}


/*
Get the file name portion of a path

This copies the behavior of Python os.path.basename()

Args:
  path:   Path to extract basename from
  tail:   Substring of path corresponding to the basename

Returns:
  EVFS_OK on success
*/
int evfs_path_basename(const char *path, StringRange *tail) {
  if(PTR_CHECK(path) || PTR_CHECK(tail)) return EVFS_ERR_BAD_ARG;
  const char *end = (char *)path + strlen(path);
  const char *pos = end - 1;

  // Look for rightmost delimiter
  while(pos > path && !char_match(*pos, EVFS_PATH_SEPS)) // Skip basename
    pos--;

  if(char_match(*pos, EVFS_PATH_SEPS))
    pos++;

  range_init(tail, pos, end-pos);

  return EVFS_OK;
}


/*
Get the extension of a file

This copies the behavior of Python os.path.splitext()

Args:
  path:   Path to extract extension from
  ext:    Substring of path corresponding to the extension

Returns:
  EVFS_OK on success
*/
int evfs_path_extname(const char *path, StringRange *ext) {
  if(PTR_CHECK(path) || PTR_CHECK(ext)) return EVFS_ERR_BAD_ARG;

  // Get the file name
  evfs_path_basename(path, ext);
  const char *pos = ext->end;

  if(range_size(ext) > 0) {
    // Scan back for first period
    while(pos > ext->start) {
      if(*pos == '.')
        break;
      pos--;
    }
  }

  if(pos > ext->start) { // Extension found
    ext->start = pos;
  } else { // Return empty range
    ext->start = ext->end;
  }

  return EVFS_OK;
}


int evfs_vfs_path_dirname(Evfs *vfs, const char *path, StringRange *head) {
  if(PTR_CHECK(vfs) || PTR_CHECK(path) || PTR_CHECK(head)) return EVFS_ERR_BAD_ARG;
  const char *end = (char *)path + strlen(path) + 1;
  const char *pos = end - 1;

  // Look for rightmost delimiter
  while(pos > path && !char_match(*pos, EVFS_PATH_SEPS)) // Skip basename
    pos--;

  // If this is a root path we keep a trailing slash
  // We could have multiple root separators. Keep them all
  StringRange root;
  vfs->m_path_root_component(vfs, path, &root);
  if(pos == path + range_size(&root)-1 && char_match(*pos, EVFS_PATH_SEPS)) {
    pos++;
  }

  range_init(head, path, pos - path);

  return  EVFS_OK;
}


int evfs_vfs_path_join_str(Evfs *vfs, const char *head, const char *tail, StringRange *joined) {
  StringRange head_r, tail_r;
  range_init(&head_r, head, strlen(head));
  range_init(&tail_r, tail, strlen(tail));

  return evfs_vfs_path_join(vfs, &head_r, &tail_r, joined);
}


int evfs_vfs_path_join(Evfs *vfs, StringRange *head, StringRange *tail, StringRange *joined) {
  if(PTR_CHECK(vfs) || PTR_CHECK(head) || PTR_CHECK(tail) || PTR_CHECK(joined)) return EVFS_ERR_BAD_ARG;
/*
  '/foo' 'bar' --> '/foo/bar'
  '/'    'foo' --> '/foo'
  '/foo' ''    --> '/foo/'
  '/'    ''    --> '/'
*/

  AppendRange joined_a = *(AppendRange *)joined; // Preserve original target range

  size_t head_len = range_strlen(head);
  size_t tail_len = range_strlen(tail);
  // We need to fit in the joined string, possibly with a new separator
  size_t joined_len = head_len + 1 + tail_len;


#ifndef ALLOW_LONG_PATHS
  if(ASSERT(joined_len < EVFS_MAX_PATH, "joined string too long")) return EVFS_ERR_OVERFLOW;
#endif

  if(ASSERT(joined_len <= range_size(&joined_a), "joined string too long")) return EVFS_ERR_OVERFLOW;


  range_cat_range(&joined_a, head);

  // Join with the separator
  // If head is empty no separator
  // If head is only a root component we don't need to add an extra separator
  StringRange root;
  vfs->m_path_root_component(vfs, head->start, &root);

  if(head_len > 0 && head_len != range_size(&root))
    range_cat_char(&joined_a, EVFS_DIR_SEP);

  if(tail_len > 0)
    range_cat_range(&joined_a, tail);

  range_terminate(&joined_a);

  return EVFS_OK;
}


// Our zero-alloc normalize algorithm needs an implementation of clz()

// __builtin_clz() added in GCC 3.4 and Clang 5
#if (defined __GNUC__ && __GNUC__ >= 4) || (defined __clang__ && __clang_major__ >= 5)
#  define HAVE_BUILTIN_CLZ
#endif

#ifdef HAVE_BUILTIN_CLZ
#  define clz(x)  __builtin_clz(x)
#else
// Count leading zeros
// From Hacker's Delight 2nd ed. Fig 5-12. Modified to support 16-bit ints.
static int clz(unsigned x) {
  static_assert(sizeof(x) <= 4, "clz() only supports a 32-bit or 16-bit argument");
  unsigned y;
  int n = sizeof(x) * 8;

  if(sizeof(x) > 2) { // 32-bit x
    y = x >> 16; if(y) {n -= 16; x = y;}
  }
  y = x >> 8;  if(y) {n -= 8; x = y;}
  y = x >> 4;  if(y) {n -= 4; x = y;}
  y = x >> 2;  if(y) {n -= 2; x = y;}
  y = x >> 1;  if(y) return n - 2;

  return n - x;
}
#endif

static int path_normalize_long(Evfs *vfs, const char *path, AppendRange *normalized);

int evfs_vfs_path_normalize(Evfs *vfs, const char *path, StringRange *normalized) {
  if(PTR_CHECK(vfs) || PTR_CHECK(path) || PTR_CHECK(normalized)) return EVFS_ERR_BAD_ARG;

/*
  Any root component is reduced to its minimal form.
  Consecutive separators are merged into one
  All separators after root component are converted to EVFS_DIR_SEP
  ./ segments are removed
  ../ segments are removed with the preceeding segment
  Trailing slashes are removed
*/

  AppendRange norm_r = *(AppendRange *)normalized; // Duplicate so we preserve original range

  unsigned seg_mask = 0; // Bitmask of path segments to retain
#define MAX_SEGMENTS    (sizeof(seg_mask) * 8)
#define KEEP_SEG(s, m)  ((m) |= (1UL << (s)))
#define KILL_SEG(s, m)  ((m) &= ~(1UL << (s)))

// Check if the segment index is being kept in normalized string
#define NORM_SEG(s, m)  ((m) & (1UL << (s)))


  // Skip over root component
  // We don't want the tokenizer to see any DOS drive letters
  const char *path_start = path;
  StringRange root;
  bool is_absolute = vfs->m_path_root_component(vfs, path, &root);
  if(is_absolute)
    path_start += range_size(&root);

  // Iterate over each segment of path
  StringRange token;
  int tok_count = 0; // This is used as a zero-based index into bitmask inside loop
  int preserved_parent_ref = -1; // Limit for merging ".." segments with preceding directory
  bool found_prev_dir;

  // Scan the path to construct bitmask
  bool new_tok = range_token(path_start, EVFS_PATH_SEPS, &token);
  while(new_tok) {
    if(tok_count+1 > MAX_SEGMENTS) // Too many segments; Use fallback
      return path_normalize_long(vfs, path, &norm_r);


    // Skip '.' segments
    if(!(range_size(&token) == 1 && *token.start == '.')) {
      if(!strncmp(token.start, "..", 2)) { // Token is '..', Remove previous segment
        // Search back through mask for most recent kept segment and remove it
        // We preserve ".." segments if no preceding non-".." segment is found
        found_prev_dir = false;
        for(int seg_pos = tok_count-1; seg_pos > preserved_parent_ref; seg_pos--) {
          if(NORM_SEG(seg_pos, seg_mask)) {
            KILL_SEG(seg_pos, seg_mask);
            found_prev_dir = true;
            break;
          }
        }
        if(!found_prev_dir && !is_absolute) { // Relative paths need leading ".." preserved
          KEEP_SEG(tok_count, seg_mask);
          preserved_parent_ref = tok_count;
        }

      } else { // Keep this segment
        KEEP_SEG(tok_count, seg_mask);
      }

    }

    tok_count++;
    new_tok = range_token(NULL, EVFS_PATH_SEPS, &token);
  }

  // We have a bitmask of all segments that will be retained

  // Find the last segment in the mask
  int last_seg = -1;
  if(seg_mask) // Need at least one set bit for __builtin_clz to work
    last_seg = (sizeof(seg_mask)*8)-1 - clz(seg_mask);

  bool truncated = false;

  // If absolute path add root prefix
  if(is_absolute) {

    // Copy the un-normalized root
    if(append_normalized_root(vfs, &norm_r, &root))
      truncated = true;

  }

  if(last_seg >= 0) {
    // Scan the path again, adding normalized tokens to the result.
    // It's possible that the path and normalized range overlap so we defer inserting
    // NULs until the very end.
    tok_count = 0;
    new_tok = range_token(path_start, EVFS_PATH_SEPS, &token);
    while(new_tok) {
      tok_count++;
      if(NORM_SEG(tok_count-1, seg_mask)) {
        if(range_cat_range_no_nul(&norm_r, &token) < 0)
          truncated = true;

        if((tok_count-1) < last_seg) { // Add separator
          if(range_cat_char_no_nul(&norm_r, EVFS_DIR_SEP) < 0)
            truncated = true;
        }
      }

      new_tok = range_token(NULL, EVFS_PATH_SEPS, &token);
    }
  }

  // Add NUL
  *norm_r.start = '\0';

  if(truncated)
    THROW(EVFS_ERR_OVERFLOW);

  return EVFS_OK;
}


// Fallback normalization routine for paths with more then sizeof(unsigned)*8 segments.
// This uses malloc to construct a stack of normalized path segments.
static int path_normalize_long(Evfs *vfs, const char *path, AppendRange *normalized) {
/*
  Any root component is reduced to its minimal form.
  Consecutive separators are merged into one
  All separators after root component are converted to EVFS_DIR_SEP
  ./ segments are removed
  ../ segments are removed with the preceeding segment
  Trailing slashes are removed
*/

  // Skip over root component
  // We don't want the tokenizer to see any DOS drive letters
  const char *path_start = path;
  StringRange root;
  bool is_absolute = vfs->m_path_root_component(vfs, path, &root);
  if(is_absolute)
    path_start += range_size(&root);


  // Iterate over each segment of path
  StringRange token;
  int tok_count = 0;

  // Scan the path to find out how many tokens are produced
  bool new_tok = range_token(path_start, EVFS_PATH_SEPS, &token);
  while(new_tok) {
    tok_count++;
    new_tok = range_token(NULL, EVFS_PATH_SEPS, &token);
  }


  // Create a stack to track tokens
  StringRange *tok_stack = evfs_malloc(tok_count * sizeof(StringRange));
  if(MEM_CHECK(tok_stack)) return EVFS_ERR_ALLOC;

  int stack_head = 0;
  int preserved_parent_ref = -1; // Limit for merging ".." segments with preceding directory

  // Scan the path again, adding tokens to the stack
  new_tok = range_token(path_start, EVFS_PATH_SEPS, &token);
  while(new_tok) {
    // Add tokens except for '.'
    if(!(range_size(&token) == 1 && *token.start == '.')) {
      // Pop stack if token is '..' unless the stack is empty
      if(!strncmp(token.start, "..", 2)) {
        if(stack_head > preserved_parent_ref+1) {
          stack_head--;
        } else if(!is_absolute) { // Relative paths need leading ".." preserved
          preserved_parent_ref = stack_head;
          tok_stack[stack_head++] = token;
        }
      } else { // Normal segment
        // Add to stack of tokens
        tok_stack[stack_head++] = token;
      }
    }

    new_tok = range_token(NULL, EVFS_PATH_SEPS, &token);
  }


  // Assemble the stacked tokens into a path
  bool truncated = false;

  // If absolute path add root prefix
  if(is_absolute) {
    if(append_normalized_root(vfs, normalized, &root))
      truncated = true;
  }

  // It's possible that the path and normalized range overlap so we defer inserting
  // NULs until the very end.
  for(int i = 0; i < stack_head; i++) {
    if(range_cat_range_no_nul(normalized, &tok_stack[i]) < 0)
      truncated = true;

    if(i < stack_head-1) { // Add separator
      if(range_cat_char_no_nul(normalized, EVFS_DIR_SEP) < 0)
        truncated = true;
    }
  }

  // Add NUL
  *normalized->start = '\0';

  evfs_free(tok_stack);

  if(truncated)
    THROW(EVFS_ERR_OVERFLOW);

  return EVFS_OK;
}




static int path_absolute_from_rel_overlap(Evfs *vfs, const char *path, StringRange *absolute);

int evfs_vfs_path_absolute(Evfs *vfs, const char *path, StringRange *absolute) {
  if(PTR_CHECK(vfs) || PTR_CHECK(path) || PTR_CHECK(absolute)) return EVFS_ERR_BAD_ARG;
  int rval;

#ifndef ALLOW_LONG_PATHS
  if(ASSERT(range_size(absolute) <= EVFS_MAX_PATH, "Absolute range too long")) return EVFS_ERR_OVERFLOW;
#endif

  // Check if already absolute
  if(evfs_vfs_path_is_absolute(vfs, path))
    return evfs_vfs_path_normalize(vfs, path, absolute); // Just apply normalization

  /*
  Join relative path to current directory and normalize:

  We want to minimize memory allocations. If the output string has sufficient extra
  space we can use it for all of our intermediate strings.

  It's possible the un-normalized string doesn't fit even if the end result does.
  In that case we need to allocate a temp string for the oversize path.

  We may be given an output string that overlaps the input relative path. We can't
  non-destructively join the CWD onto the input nor do we know in advance how long
  the CWD will be. For this case we use a strategy where the CWD is first placed
  after the input path and then swapped.

  p = path element; r = relative root prefix; c = CWD element

  No overlap:
    path:       [rpp0]
    absolute:          [...............]

    Get CWD:           [rrccc0.........]
    Remove dup roots:  [rccc0..........]
    Join path:         [rccc/ppp0......]
    Normalize:         [rcc/pp0........]

  No overlap but not enough intermediate space:
    path:       [rpp0]
    absolute:          [.......]

    Get CWD:           [rccc0..]              (not enough left to do join)
    Malloc temp:                 [.........]
    Join path:                   [rccc/ppp0]
    Normalize:         [rcc/pp0]

  Overlap:
    path:       [rpp0]
    absolute:   [rpp0...........]

    Get CWD:    [rpp0cccc0......]  (This step gives us CWD len)
    Move path:  [r.....pp0......]
    Get CWD:    [rcccc0pp0......]
    Add sep:    [rcccc/pp0......]
    Normalize:  [rcc/pp0........]

  */

  // Test for overlap
  const char *path_end = path + strlen(path)+1;
  if(absolute->start < path_end && absolute->end >= path_end) // Overlap with absolute
    return path_absolute_from_rel_overlap(vfs, path, absolute);



  // No overlap cases handled here:

  StringRange joined_r = *absolute;
  char *joined;

  // We could have a DOS-style drive letter on a relative path ("C:foo"); Skip over it
  const char *path_start = path;
  StringRange root;
  vfs->m_path_root_component(vfs, path, &root);
  size_t root_len = range_size(&root);
  path_start += root_len;

  if(range_size(&root) > 0) // Add any relative root prefix from path now
    range_cat_range((AppendRange *)&joined_r, &root);

  // Get current directory
  rval = vfs->m_get_cur_dir(vfs, &joined_r);
  if(rval != EVFS_OK)
    return rval;

  size_t cwd_len = range_strlen(&joined_r);

  /*
  Current dir may have a DOS-style drive letter
  We need to prevent conflict with a relative drive letter from the path. There
  is no way to do this for FatFs without race conditions from changing volumes so
  we do the fixup here as best as possible in a generic way.
  */
  if(root_len > 0) { // Path has a (relative) root component
    // Strip any root component from CWD
    vfs->m_path_root_component(vfs, joined_r.start, &root);
    size_t cwd_root_len = range_size(&root);
    if(cwd_root_len > 1) { // Cur dir also has a root component beyond just '/'
      // Copy back over the root component, keeping the last '/'
      memmove((char *)joined_r.start, joined_r.start+cwd_root_len-1, cwd_len - (cwd_root_len-1) + 1);
    }
  }

  size_t path_len = strlen(path_start);
  StringRange path_r;
  range_init(&path_r, (char *)path_start, path_len);

  // Check if we have space left for joining without a new string
  size_t free_space = range_size(&joined_r) - cwd_len - 1;

  if(free_space >= 1 + path_len) { // No malloc needed
    joined = (char *)joined_r.start; // Save the start position before it is moved

    rval = evfs_vfs_path_join(vfs, &joined_r, &path_r, &joined_r);
    if(rval != EVFS_OK) // This shouldn't happen
      return rval;

    // Adjust start of joined string for any root component
    joined -= root_len;

    // Normalize joined path
    rval = evfs_vfs_path_normalize(vfs, joined, absolute);

  } else { // Need temp string
    size_t joined_len = root_len + cwd_len + 1 + path_len + 1;
    joined = evfs_malloc(joined_len);
    if(MEM_CHECK(joined)) return EVFS_ERR_ALLOC;

    range_init(&joined_r, joined, joined_len); // Joining into new temp buf

    StringRange cwd_r = *absolute; // This includes any DOS-style root component added above

    rval = evfs_vfs_path_join(vfs, &cwd_r, &path_r, &joined_r);

    if(rval == EVFS_OK) {
      // Normalize joined path
      rval = evfs_vfs_path_normalize(vfs, joined, absolute);
    }

    evfs_free(joined);
  }

  return rval;
}

// Construct absolute path that overlaps relative input path
static int path_absolute_from_rel_overlap(Evfs *vfs, const char *path, StringRange *absolute) {
/*
  Overlap:
    path:       [rpp0]
    absolute:   [rpp0...........]

    Get CWD:    [rpp0cccc0......]  (This step gives us CWD len)
    Move path:  [r.....pp0......]
    Get CWD:    [rcccc0pp0......]
    Add sep:    [rcccc/pp0......]
    Normalize:  [rcc/pp0........]
*/

  int rval;
  size_t path_len = strlen(path);

  /*
  It's possible there is a partial overlap between path and absolute.
  We don't want to deal with this scenario below so just move path to align
  with absolute.
  */
  if(path != absolute->start) {
    if(path_len+1 > range_size(absolute)) // Give up if we can't fit
      return EVFS_ERR_OVERFLOW;

    memmove((char *)absolute->start, path, path_len+1);
    path = absolute->start;
  }

  char *path_start = (char *)path;
  AppendRange cwd_r = {.start = (char *)path_start+path_len+1, .end = (char *)absolute->end}; // Put first CWD in space after path

  // We could have a DOS-style drive letter on a relative path ("C:foo"); Skip over it
  StringRange root;
  vfs->m_path_root_component(vfs, path, &root);
  size_t root_len = range_size(&root);
  path_start += root_len; // Skip over root component to leave it in place
  path_len -= root_len;


  // Get current directory
  rval = vfs->m_get_cur_dir(vfs, (StringRange *)&cwd_r);
  if(rval != EVFS_OK)
    return rval;

  size_t cwd_len = range_strlen((StringRange *)&cwd_r);

  // Move path
  memmove(path_start + cwd_len+1, path_start, path_len+1);
  range_init(&cwd_r, path_start, cwd_len+1); // Change CWD to newly opened gap at start

  // Get current directory
  rval = vfs->m_get_cur_dir(vfs, (StringRange *)&cwd_r);
  if(rval != EVFS_OK)
    return rval;

  // Add sep
  *(cwd_r.end-1) = EVFS_DIR_SEP; // Overwrite the NUL in CWD

  /*
  Current dir may have a DOS-style drive letter
  We need to prevent conflict with a relative drive letter from the path. There
  is no way to do this for FatFs without race conditions from changing volumes so
  we do the fixup here as best as possible in a generic way.
  */
  if(root_len > 0) { // Path has a (relative) root component
    // Strip any root component from cur_dir
    vfs->m_path_root_component(vfs, path_start, &root);
    size_t cwd_root_len = range_size(&root);
    if(cwd_root_len > 1) { // Cur dir also has a root component beyond just '/'
      // Copy back over the root component, keeping the last '/'
      memmove(path_start, path_start+cwd_root_len-1, cwd_len+1 + path_len+1 - (cwd_root_len-1));
    }
  }

  return evfs_vfs_path_normalize(vfs, path, absolute);
}

