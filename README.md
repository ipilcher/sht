# Simple Hash Table

## Overview

This library implements a simple hash table that can be used as a dictionary or
set implementation in C programs.

## Design philosophy

This library aims to provide a straightforward hash table implementation that is
both readily understandable and suitable for most hash table use cases.  Maximum
possible performance and scabability are not design goals.

Contract violations by the calling program will cause the library to call
`abort()`.

## Limits

The library imposes a number of limits.

* The absolute maximum number of buckets in a table is 2^24 (16,777,216).  At
  the default load factor threshold (LFT) of 85%, this results in a maximum
  usable capacity of 14,260,633 entries.

* The maximum size of an entry is 16 KiB (16,384 bytes).

* The maximum probe sequence length (PSL) of an entry is 127.  (See
  [*Robin Hood hashing*](docs/robin-hood.md#probe-sequence-length) for a
  discussion of PSLs.)  A PSL value anywhere close to 127 indicates that the
  table's hash function is pathologically bad.

## Usage

The basic usage pattern is:

* Create a table &mdash; `SHT_NEW()`,
* If necessary, set any non-default table attributes &mdash; `sht_set_eq_ctx()`,
  `sht_set_lft()`, `sht_set_freefn()`, etc.,
* Initialize the table &mdash; `sht_init()`,
* Use the table &mdash; `sht_add()`, `sht_get()`, `sht_ro_iter()`, etc., and
* Free the table &mdash; `sht_free()`.

### Examples

**NOTE:**  The examples in this section omit error checking.

#### Example 1

This example shows the simplest possible usage &mdash; a set of integers.

* The keys are integers (type `int`).
* This is a set, rather than a dictionary, so there are no values.
* Thus each entry is simply an `int`; no structure type is required.

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

* The hash function used (`XXH3_64bits()`) returns a 64-bit hash value, but the
  upper 32 bits are discarded.  In fact, the library will only use the lower 24
  bits of the hash value.  It is imperative that the hash function used performs
  adequate bit mixing.

* A hash table must be initialized (`sht_init()`) before it can be used.

#### Example 2

This example creates a dictionary that maps host names to IP addresses.

```c
struct dict_entry {
    char            *hostname;
    struct in_addr  addr;
};

uint32_t dict_hash(const void *restrict key, void *restrict)
{
    return (uint32_t)XXH3_64bits(key, strlen(key));
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

    ht = SHT_NEW(dict_hash, dict_eq, struct dict_entry);
    sht_set_freefn(ht, dict_free, NULL);
    sht_init(ht, 20);
    return ht;
}
```

* This hash table requires a "free function," in order to avoid leaking memory
  (host names) when entries are deleted.

* The free function is one of several hash table attributes that can be set
  before the table is initialized.  Attempting to set any of these attributes
  on a table that has been initialized will cause the program to abort.

## Memory management

## Iterators
