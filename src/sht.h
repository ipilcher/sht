// SPDX-License-Identifier: GPL-3.0-or-later

/*
 *
 * 	SHT - hash table with "Robin Hood" probing
 *
 *	Copyright 2025 Ian Pilcher <arequipeno@gmail.com>
 *
 */


/** @file */


#ifndef SHT_H
#define SHT_H


#include <stddef.h>
#include <stdint.h>


/**
 * @internal
 * @brief
 * Maximum entry size.
 */
#define SHT_MAX_ESIZE		16384

/**
 * Critical error printing function.
 *
 * If the calling program violates this library's contract, the library will
 * print an error message and abort the program.  (See
 * [Abort conditions](index.html#abort-conditions).)  This variable can be set
 * to customize how the error message is printed or logged.
 *
 * ```c
 * static void log_sht_err(const char *msg)
 * {
 *     syslog(LOG_CRIT, "SHT library error: %s", msg);
 * }
 *
 * sht_abort_print = log_sht_err;
 * ```
 *
 * @param	msg	The error message (not newline terminated).
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
extern void (*sht_abort_print)(const char *msg);


/*******************************************************************************
 *
 *
 *	Callback function types
 *
 *
 ******************************************************************************/

/**
 * Hash function type.
 *
 * Callback function type used to calculate hash values for keys.
 *
 * 32-bit example, using
 * [`XXH32()`](https://xxhash.com/doc/v0.8.3/group___x_x_h32__family.html),
 * which requires a seed:
 *
 * ```c
 * uint32_t myhash32(const void *restrict key, void *restrict context)
 * {
 *     const struct mykey *k = key;
 *     const XXH32_hash_t *seed = ctx;
 *
 *     return XXH32(k, sizeof *k, *seed);
 * }
 * ```
 *
 * 64-bit example, using
 * [`XXH3_64bits()`](https://xxhash.com/doc/v0.8.3/group___x_x_h3__family.html),
 * which does not require a seed:
 *
 * ```c
 * uint32_t myhash64(const void *restrict key, const void *restrict)
 * {
 *     const struct mykey *k = key;
 *
 *     // Throw away the upper 32 bits
 *     return (uint32_t)XXH3_64bits(k, sizeof *k);
 * }
 * ```
 *
 * @param	key	The key to be hashed.
 * @param	context	Optional function-specific context.
 *
 * @returns	The hash value of the key.
 */
typedef uint32_t (*sht_hashfn_t)(const void *restrict key,
				 void *restrict context);

/**
 * Equality comparison function type.
 *
 * Callback function type used to compare a key with the key of an existing
 * bucket.  This function is only called when the lower 24 bits of the hash
 * values of the two keys are equal.
 *
 * ```c
 * struct my_entry {
 *     const char      *name;
 *     struct in_addr  address;
 * };
 *
 * bool my_cmp(const void *restrict key, const void *restrict entry,
 *              void *restrict)
 * {
 *     const char *const name = key;
 *     const struct my_entry *const e = entry;
 *
 *     return strcmp(name, e->name) == 0;
 * }
 * ```
 *
 * @param	key	The key to be compared against the key in the
 *			bucket.
 * @param	entry	The entry whose key is to be compared.
 * @param	context	Optional function-specific context.
 *
 * @returns	A boolean value that indicates whether @p key is equal to the
 *		key in @p entry.
 */
typedef bool (*sht_eqfn_t)(const void *restrict key,
			    const void *restrict entry,
			    void *restrict context);

/**
 * Free function type.
 *
 * Callback function type used to free entry resources.  For example:
 *
 * ```c
 * struct my_entry {
 *     const char      *name;
 *     struct in_addr  address;
 * };
 *
 * void my_free(const void *restrict entry, void *restrict)
 * {
 *     const struct my_entry *const e = entry;
 *     free(e->name);
 * }
 * ```
 *
 * @param	entry	The entry whose resources should be freed.
 * @param	context	Optional function-specific context.
 */
typedef void (*sht_freefn_t)(const void *restrict entry,
			     void *restrict context);


/*******************************************************************************
 *
 *
 *	Other types
 *
 *
 ******************************************************************************/

/**
 * A hash table.
 */
struct sht_ht;

/**
 * Iterator types.
 */
enum sht_iter_type: bool {
	SHT_ITER_RO = 0,	/**< Read-only iterator. */
	SHT_ITER_RW = 1		/**< Read/write iterator. */
};

/**
 * Hash table iterator.
 */
struct sht_iter;

/**
 * Error codes.
 */
enum sht_err: uint8_t {
	SHT_ERR_OK = 0,		/**< No error. */
	SHT_ERR_ALLOC,		/**< Memory allocation failed. */
	SHT_ERR_BAD_ESIZE,	/**< Entry size too large (> 16KiB). */
	SHT_ERR_TOOBIG,		/**< Requested table size too large. */
	SHT_ERR_BAD_HASH,	/**< Too many hash collisions. */
	SHT_ERR_ITER_LOCK,	/**< Can't acquire iterator lock. */
	SHT_ERR_ITER_COUNT,	/**< Table has too many iterators. */
	SHT_ERR_ITER_NO_LAST,	/**< Iterator at beginning or end. */
		//
		// 	Add all values above this comment
		//
	SHT_ERR_COUNT		/**< (Not an error; used for bounds checks.) */
};

static_assert(sizeof(enum sht_err) == 1);



/*******************************************************************************
 *
 *
 *	Functions - documented in ht.c
 *
 *
 ******************************************************************************/


/*
 * Table lifecycle - create, configure, initialize & free
 */

// Create a new hash table.
[[gnu::nonnull(1, 2)]]
struct sht_ht *sht_new_(sht_hashfn_t hashfn, sht_eqfn_t eqfn,
			sht_freefn_t freefn, size_t esize,
			size_t ealign, enum sht_err *err);

// Set the "context" for a table's hash function.
[[gnu::nonnull(1)]]
void sht_set_hash_ctx(struct sht_ht *ht, void *context);

// Set the "context" for a table's equality function.
[[gnu::nonnull(1)]]
void sht_set_eq_ctx(struct sht_ht *ht, void *context);

// Set the "context" for a table's free function.
[[gnu::nonnull(1)]]
void sht_set_free_ctx(struct sht_ht *ht, void *context);

// Set the load factor threshold for a table.
[[gnu::nonnull]]
void sht_set_lft(struct sht_ht *ht, uint8_t lft);

// Set the PSL limit of a table.
[[gnu::nonnull]]
void sht_set_psl_limit(struct sht_ht *ht, uint8_t limit);

// Initialize a hash table.
[[gnu::nonnull]]
bool sht_init(struct sht_ht *ht, uint32_t capacity);

// Free the resources used by a hash table.
[[gnu::nonnull]]
void sht_free(struct sht_ht *ht);


/*
 * Table operations - get, set, delete, etc.
 */

// Add an entry to the table, if its key is not already present.
[[gnu::nonnull]]
int sht_add(struct sht_ht *ht, const void *key, const void *entry);

// Unconditionally set the value associated with a key.
[[gnu::nonnull]]
int sht_set(struct sht_ht *ht, const void *key, const void *entry);

// Lookup an entry in a table.
[[gnu::nonnull]]
const void *sht_get(struct sht_ht *ht, const void *restrict key);

// Get the number of entries in a table.
[[gnu::nonnull]]
uint32_t sht_size(const struct sht_ht *ht);

// Determine whether a table is empty.
[[gnu::nonnull]]
bool sht_empty(const struct sht_ht *ht);

// Remove an entry from the table.
[[gnu::nonnull]]
bool sht_delete(struct sht_ht *ht, const void *restrict key);

// Remove and return an entry from the table.
[[gnu::nonnull]]
bool sht_pop(struct sht_ht *ht, const void *restrict key, void *restrict out);

// Replace the entry associated with an existing key.
[[gnu::nonnull]]
bool sht_replace(struct sht_ht *ht, const void *key,
		  const void *entry);

// Exchange an existing entry and a new entry.
[[gnu::nonnull]]
bool sht_swap(struct sht_ht *ht, const void *key,
	       const void *entry, void *out);


/*
 * Iterators
 */

// Create a new iterator
[[gnu::nonnull]]
struct sht_iter *sht_iter_new(struct sht_ht *ht, enum sht_iter_type type);

// Free an iterator.
[[gnu::nonnull]]
void sht_iter_free(struct sht_iter *iter);

// Get the next entry from an iterator.
[[gnu::nonnull]]
const void *sht_iter_next(struct sht_iter *iter);

// Remove the last entry returned by a read/write iterator.
[[gnu::nonnull]]
bool sht_iter_delete(struct sht_iter *iter);

// Replace the last entry returned by an iterator.
[[gnu::nonnull]]
bool sht_iter_replace(struct sht_iter *iter, const void *restrict entry);


/*
 * Error reporting
 */

// Get the error code of a table's last error.
[[gnu::nonnull]]
enum sht_err sht_get_err(const struct sht_ht *ht);

// Get the error code of an iterator's last error.
[[gnu::nonnull]]
enum sht_err sht_iter_err(const struct sht_iter *iter);

// Get the description for an error code.
const char *sht_msg(enum sht_err err);

// Get a description of a table's last error.
[[gnu::nonnull]]
const char *sht_get_msg(const struct sht_ht *ht);

// Get a description of an iterator's last error.
[[gnu::nonnull]]
const char *sht_iter_msg(const struct sht_iter *iter);


/*******************************************************************************
 *
 *
 *	Helper macros
 *
 *
 ******************************************************************************/

/*
 * SHT_NEW()
 */

/**
 * @internal
 * @brief
 * Macro that expands to its second argument.
 *
 * This macro is used by SHT_HT_NEW() to accept an "optional" `err` argument.
 *
 * @param	_	Dummy argument; only present to make syntax valid.
 * @param	a2	The expression to which this macro will expand.
 * @param	...	Possible additional arguments.  Not used.
 *
 * @returns	This macro expands to its second argument (@p a2).
 */
#define SHT_ARG2(_, a2, ...)	(a2)

/**
 * Create a new hash table.
 *
 * This macro is a wrapper for sht_new_().
 *
 * ```c
 * struct entry {
 *     const char      *name;
 *     struct in_addr  address;
 * };
 *
 * struct sht_ht *ht;
 * enum sht_err err;
 *
 * ht = sht_new(hashfn, eqfn, NULL, sizeof(struct entry),
 *              alignof(struct entry), &err);
 *
 * // Rewrite as ...
 * ht = SHT_NEW(hashfn, eqfn, NULL, struct entry, &err);
 *
 * // Without error reporting ...
 * ht = sht_new(hashfn, eqfn, NULL, sizeof(struct entry),
 *              alignof(struct entry), NULL);
 *
 * // Becomes ...
 * ht = SHT_NEW(hashfn, eqfn, NULL, struct entry);
 * ```
 *
 * @param	hashfn	Function to be used to compute the hash values of keys.
 * @param	eqfn	Function to be used to compare keys for	equality.
 * @param	freefn	Function to be used to free entry resources.  (May be
 *			`NULL`.)
 * @param	etype	The type of the entries to be stored in the table.
 * @param[out]	...	Optional output pointer for error reporting.
 *
 * @returns	On success, a pointer to the new hash table is returned.  On
 *		error, `NULL` is returned, and an error code is returned in
 *		@p err (if it is not `NULL`).
 *
 * @see		sht_new_()
 */
#define SHT_NEW(hashfn, eqfn, freefn, etype, ...)			\
	({								\
		static_assert(sizeof(etype) <= SHT_MAX_ESIZE,		\
			       "Entry type (" #etype ") too large");	\
		sht_new_(hashfn, eqfn, freefn,				\
			 sizeof(etype), alignof(etype),			\
			 SHT_ARG2(_, ##__VA_ARGS__, nullptr));		\
	})


#endif		/* SHT_H */
