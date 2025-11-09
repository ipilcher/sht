# SHT Hash Table

## Overview

This library implements a simple hash table that can be used as a dictionary or
set implementation in C programs.  It uses a "Robin Hood" probing algorithm,
which is described [here][1].

## Design philosophy

This library aims to provide a straightforward hash table implementation that is
both readily understandable and suitable for most hash table use cases.  Maximum
possible performance and scabability are not design goals.

Also, the library provides no explicit support for concurrency.  Any use of the
library in a multi-threaded application will require external synchronization.

The library takes a "fail fast" approach to logic errors in the calling program.
The calling program will be aborted if it violates the library's API contract.
(See [Abort conditions](#abort-conditions) below.)  This frees the calling
program from the need to check for and react to unrecoverable programming
errors.

## Limits and assumptions

This library is developed on Linux with GCC (15.2.1 as of this writing).  It
makes use of a number of GCC builtins and C23 features.  It should also work
with Clang.

The library imposes a number of limits.

* The absolute maximum number of buckets in a table is 2²⁴ (16,777,216).  At
  the default load factor threshold (LFT) of 85%, this results in a maximum
  usable capacity of 14,260,633 entries.

* The maximum size of an entry is 16 KiB (16,384 bytes).

* The maximum probe sequence length (PSL) of an entry is 127.  (See
  [*Robin Hood hashing*][1] and [*PSL Limits*][4] for a discussion of PSLs.)

* The maximum number of read-only iterators an a table is 32,767.

In addition to the limits imposed by the library, the total size of a table is
limited by the amount of memory available to the application.  A table's total
memory footprint scales with the product of the number of buckets and the entry
size (i.e. `total_size ∝ buckets × entry_size`), so the largest possible table
will not fit in the 4 GiB address space of a 32-bit system.

## Usage

The basic usage pattern is:

* Create a table &mdash; `SHT_NEW()`,
* If necessary, set any non-default table attributes &mdash; `sht_set_eq_ctx()`,
  `sht_set_lft()`, `sht_set_freefn()`, etc.,
* Initialize the table &mdash; `sht_init()`,
* Use the table &mdash; `sht_add()`, `sht_get()`, `sht_ro_iter()`, etc., and
* Free the table &mdash; `sht_free()`.

### Table examples

To create a hash table, the following are required.

* An entry type to be stored in the table.  This will usually be a data
  structure that contains both a key and a value.  If the table will be used as
  a set rather than a dictionary, an entry will only contain a key, so it may be
  a primitive type.

* A function that calculates the hash value of a key &mdash; the "hash
  function."

* A function that compares two keys (one of which is inside an entry) for
  equality &mdash; the "equality function."

* In some cases, a "free function" is required.  The library calls the free
  function to release the resources associated with an entry that is being
  deleted from the table.  (See [Memory management](#memory-management)
  below.)

> **NOTE**
>
> The examples in this section omit error checking.

#### Example 1

This example shows the simplest possible usage &mdash; a set of integers.

* The keys are integers (type `int`).
* This is a set, rather than a dictionary, so there are no values.
* Thus, each entry is simply an `int`; no structure is required.

```c
uint32_t int_hash(const void *restrict key, void *restrict)
{
    const int *i = key;
    return (uint32_t)XXH3_64bits(i, sizeof *i);
}

_Bool int_eq(const void *restrict key, const void *restrict entry,
             void *restrict)
{
    const int *i1 = key;
    const int *i2 = entry;
    return *i1 == *i2;
}

struct sht_ht *int_set(void)
{
    struct sht_ht *ht;

    ht = SHT_NEW(int_hash, int_eq, int);
    sht_init(ht, 0);  // default initial capacity
    return ht;
}
```

Note the following.

* This example uses[`XXH3_64bits()`][2] as its underlying hash function.
  [`XXH3_64bits()`][2] returns a 64-bit hash value,  but the upper 32 bits are
  discarded.  In fact, the library will only use the lower 24 bits of the hash
  value, so it is imperative that the hash function   performs adequate bit
  mixing.

* Neither the hash function or the equality function makes use of any context.

* A hash table must be initialized (`sht_init()`) before it can be used.

#### Example 2

This example creates a dictionary that maps host names to IP addresses.

```c
static XXH32_hash_t hash_seed;

struct dict_entry {
    char            *hostname;
    struct in_addr  addr;
};

uint32_t dict_hash(const void *restrict key, void *restrict context)
{
    const XXH32_hash_t *seed = context;
    return XXH32(key, strlen(key), *context);
}

_Bool dict_eq(const void *restrict key, const void *restrict entry,
            void *restrict)
{
    const struct my_entry *e = entry;
    return strcmp(key, e->hostname) == 0;
}

void dict_free(const void *restrict entry, void *restrict)
{
    const struct my_entry *e = entry;
    free(e->hostname);
}

struct sht_ht *dict(void)
{
    struct sht_ht *ht;

    getrandom(&hash_seed, sizeof hash_seed, 0);
    ht = SHT_NEW(dict_hash, dict_eq, struct dict_entry);
    sht_set_hash_ctx(ht, &hash_seed);
    sht_set_freefn(ht, dict_free, NULL);
    sht_init(ht, 20);
    return ht;
}
```

Note:

* This example uses [`XXH32()`][3], which  requires a seed value.  The hash
  function context is used to pass the seed to the hash function.

* This hash table requires a "free function," in order to avoid leaking memory
  (host names) when entries are deleted.  (See
  [Memory management](#memory-management) below.)

* The hash function context and the free function are two of several hash table
  attributes that can be set before the table is initialized.  Attempting to set
  any of these attributes on a table that has been initialized is an API
  contract violation that will cause the program to abort.

### Helper macros

Some of the library's API functions are intended to be called via helper macros.
The names of these functions end in an underscore (`_`), and their short
descriptions in the API documentation are enclosed in parentheses.

* SHT_NEW() wraps sht_new_().
* SHT_ITER_FREE() wraps sht_ro_iter_free_() and sht_rw_iter_free_().
* SHT_ITER_NEXT() wraps sht_ro_iter_next_() and sht_rw_iter_next_().
* SHT_ITER_REPLACE() wraps sht_ro_iter_replace_() and sht_rw_iter_replace_().
* SHT_ITER_ERR() wraps sht_ro_iter_err_() and sht_rw_iter_err_().
* SHT_ITER_MSG() wraps sht_ro_iter_msg_() and sht_rw_iter_msg_().

## Memory management

Table entries may contain pointers to other objects.  (See `dict_entry.hostname`
in the example above.)  If such an entry is deleted from a table without freeing
these objects, memory leaks can occur.  If a table has a free function set, that
function will be called whenever an entry is deleted from that table, in order
to free that entry's associated resources.

The following operations can cause entries to be deleted.

* sht_set()
* sht_replace()
* sht_delete()
* sht_free()
* sht_iter_delete()
* SHT_ITER_REPLACE()

(Operations such as sht_swap(), that return the entry being removed from the
table, will not call the free function.)

Note that entry resources should not be automatically freed if they are still
referenced elsewhere.  Users of the library must take care to avoid both memory
leaks and use-after-free bugs.

## Iterators

The library supports 2 iterator variations &mdash; read-only and read/write.

* Multiple read-only iterators can be exist on a table simultaneously, as long
  as no read/write iterator exists.

* Only a single read/write iterator can exist on a table, and no read-only
  iterators can exist at the same time.

* A read-only iterator cannot be used to delete entries from a table, but it can
  be used to replace entries (with other entries that have equal keys).

* A read/write iterator can be used to replace or delete entries from a table.

The table below summarizes these differences.

|                   |Read-only|Read/write|
|-------------------|:-------:|:--------:|
|Multiple iterators?|   YES   |    NO    |
|Replace entries?   |   YES   |    YES   |
|Delete entries?    |    NO   |    YES   |

Calling a function that may modify the structure of the table while any
iterators (read-only or read/write) exist on the table will cause the library to
abort the program.  (See [Abort conditions](#abort-conditions) below.)

> **NOTE**
>
> The order in which an iterator returns the items in the table is effectively
> random, and it may change as entries are added to and removed from the table.

## Error handling

### Non-fatal errors

Some of the functions (and macros) in this library return an error indication in
some circumstances.

|Function |Error return|Error info|Error codes                                               |
|----------------------|:----:|:-:|----------------------------------------------------------|
|SHT_NEW()               |`NULL`|1|`SHT_ERR_ALLOC`†                                          |
|- sht_new_()            |`NULL`|1|`SHT_ERR_BAD_ESIZE`, `SHT_ERR_ALLOC`                      |
|sht_init()              |  `0` |2|`SHT_ERR_TOOBIG`, `SHT_ERR_ALLOC`                         |
|sht_add()               | `-1` |2|`SHT_ERR_TOOBIG`, `SHT_ERR_ALLOC`, `SHT_ERR_BAD_HASH`     |
|sht_set()               | `-1` |2|`SHT_ERR_TOOBIG`, `SHT_ERR_ALLOC`, `SHT_ERR_BAD_HASH`     |
|sht_ro_iter()           |`NULL`|2|`SHT_ERR_ITER_LOCK`, `SHT_ERR_ITER_COUNT`, `SHT_ERR_ALLOC`|
|sht_rw_iter()           |`NULL`|2|`SHT_ERR_ITER_LOCK`, `SHT_ERR_ALLOC`                      |
|sht_iter_delete()       |  `0` |3|`SHT_ERR_ITER_NO_LAST`                                    |
|SHT_ITER_REPLACE()      |  `0` |3|`SHT_ERR_ITER_NO_LAST`                                    |
|- sht_ro_iter_replace_()|  `0` |3|`SHT_ERR_ITER_NO_LAST`                                    |
|- sht_rw_iter_replace_()|  `0` |3|`SHT_ERR_ITER_NO_LAST`                                    |

† SHT_NEW() checks the entry size during compilation.

1. If an error occurs, sht_new_() stores an error code in the variable
   referenced by its `err` argument, provided that the value of `err` is not
   `NULL`.  A description of the error can be retrieved with sht_msg().

   SHT_NEW() can be called with either 3 or 4 arguments.  If a fourth argument
   is provided, it is used as the `err` argument in the internal call to
   sht_new_().  Otherwise, sht_new_() is called with its `err` argument set to
   `NULL`.

2. If an error occurs in a "table operation" function (a function that takes a
   pointer to a table as its first argument), the error code can be retrieved
   with sht_get_err() (and passed to sht_msg()), or an error description can be
   retrieved directly with sht_get_msg().

3.  If an error occurs in an "iterator operator" function (a function that takes
    a pointer to an iterator as its first argument), the error code can be
    retrieved with SHT_ITER_ERR() (and passed to sht_msg()), or an error
    description can be retrieved directly with SHT_ITER_MSG().

### Abort conditions

As mentioned [above](#design-philosophy), the library takes a "fail fast"
approach to API contract violations by the calling program.  The library will
`abort()` the calling program if any of the following occur.

* An invalid value is passed to sht_msg().

* A `NULL` function pointer is passed to sht_new_().

* sht_new_() is called with an invalid `esize` or `ealign` argument.  (This
  cannot occur if sht_new_() is called via SHT_NEW().)

  * `ealign` must be a power of 2.
  * `esize` must be a multiple of `ealign`.

* An invalid load factor threshold is passed to sht_set_lft().

* An invalid PSL limit is passed to sht_set_psl_limit().

* One of the functions in the table below is called on a table that is in an
  inappropriate state.

  | Function             | Uninitialized | Initialized | Iterator(s) exist |
  |----------------------|:-------------:|:-----------:|:-----------------:|
  |sht_set_hash_ctx()    |               |  **ABORT**  |         †         |
  |sht_set_eq_ctx()      |               |  **ABORT**  |         †         |
  |sht_set_freefn()      |               |  **ABORT**  |         †         |
  |sht_set_lft()         |               |  **ABORT**  |         †         |
  |sht_set_psl_limit()   |               |  **ABORT**  |         †         |
  |sht_init()            |               |  **ABORT**  |         †         |
  |sht_free()            |               |             |     **ABORT**     |
  |sht_add()             |   **ABORT**   |             |     **ABORT**     |
  |sht_set()             |   **ABORT**   |             |     **ABORT**     |
  |sht_get()             |   **ABORT**   |             |                   |
  |sht_size()            |   **ABORT**   |             |                   |
  |sht_empty()           |   **ABORT**   |             |                   |
  |sht_delete()          |   **ABORT**   |             |     **ABORT**     |
  |sht_pop()             |   **ABORT**   |             |     **ABORT**     |
  |sht_replace()         |   **ABORT**   |             |                   |
  |sht_swap()            |   **ABORT**   |             |                   |
  |sht_ro_iter()         |   **ABORT**   |             |                   |
  |sht_rw_iter()         |   **ABORT**   |             |                   |

  † Abort implied.  (An iterator cannot be created on an uninitialized
    table.)

[1]: https://github.com/ipilcher/sht/blob/main/docs/robin-hood.md
[2]: https://xxhash.com/doc/v0.8.3/group___x_x_h3__family.html
[3]: https://xxhash.com/doc/v0.8.3/group___x_x_h32__family.html
[4]: https://github.com/ipilcher/sht/blob/main/docs/psl-limits.md
