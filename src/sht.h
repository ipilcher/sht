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
 * Maximum entry size.
 */
#define SHT_MAX_ESIZE		16384

/**
 * @internal
 * Set function attributes without confusing Doxygen.
 */
#ifndef SHT_DOXYGEN
#define SHT_FNATTR(...)		__attribute__((__VA_ARGS__))
#else
#define SHT_FNATTR(...)
#endif


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
 * _Bool my_cmp(const void *restrict key, const void *restrict entry,
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
typedef _Bool (*sht_eqfn_t)(const void *restrict key,
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
 * Read-only hash table iterator.
 */
struct sht_ro_iter;

/**
 * Read/write hash table iterator.
 */
struct sht_rw_iter;

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

_Static_assert(sizeof(enum sht_err) == 1, "sht_err size");



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
SHT_FNATTR(nonnull(1, 2))
struct sht_ht *sht_new_(sht_hashfn_t hashfn, sht_eqfn_t eqfn, size_t esize,
			size_t ealign, enum sht_err *err);

// Set the "context" for a table's hash function.
SHT_FNATTR(nonnull(1))
void sht_set_hash_ctx(struct sht_ht *ht, void *context);

// Set the "context" for a table's equality function.
SHT_FNATTR(nonnull(1))
void sht_set_eq_ctx(struct sht_ht *ht, void *context);

// Set the optional entry resource free function for a table.
SHT_FNATTR(nonnull(1))
void sht_set_freefn(struct sht_ht *ht, sht_freefn_t freefn, void *context);

// Set the load factor threshold for a table.
SHT_FNATTR(nonnull)
void sht_set_lft(struct sht_ht *ht, uint8_t lft);

// Set the PSL threshold of a table.
SHT_FNATTR(nonnull)
void sht_set_psl_thold(struct sht_ht *ht, uint8_t thold);

// Initialize a hash table.
SHT_FNATTR(nonnull)
_Bool sht_init(struct sht_ht *ht, uint32_t capacity);

// Free the resources used by a hash table.
SHT_FNATTR(nonnull)
void sht_free(struct sht_ht *ht);


/*
 * Table operations - get, set, delete, etc.
 */

// Add an entry to the table, if its key is not already present.
SHT_FNATTR(nonnull)
int sht_add(struct sht_ht *ht, const void *key, const void *entry);

// Unconditionally set the value associated with a key.
SHT_FNATTR(nonnull)
int sht_set(struct sht_ht *ht, const void *key, const void *entry);

// Lookup an entry in a table.
SHT_FNATTR(nonnull)
const void *sht_get(struct sht_ht *ht, const void *restrict key);

// Get the number of entries in a table.
SHT_FNATTR(nonnull)
uint32_t sht_size(const struct sht_ht *ht);

// Determine whether a table is empty.
SHT_FNATTR(nonnull)
_Bool sht_empty(const struct sht_ht *ht);

// Remove an entry from the table.
SHT_FNATTR(nonnull)
_Bool sht_delete(struct sht_ht *ht, const void *restrict key);

// Remove and return an entry from the table.
SHT_FNATTR(nonnull)
_Bool sht_pop(struct sht_ht *ht, const void *restrict key, void *restrict out);

// Replace the entry associated with an existing key.
SHT_FNATTR(nonnull)
_Bool sht_replace(struct sht_ht *ht, const void *key,
		  const void *entry);

// Exchange an existing entry and a new entry.
SHT_FNATTR(nonnull)
_Bool sht_swap(struct sht_ht *ht, const void *key,
	       const void *entry, void *out);


/*
 * Iterator lifecycle - create & free
 */

// Create a new read-only iterator.
SHT_FNATTR(nonnull)
struct sht_ro_iter *sht_ro_iter(struct sht_ht *ht);

// Create a new read/write iterator.
SHT_FNATTR(nonnull)
struct sht_rw_iter *sht_rw_iter(struct sht_ht *ht);

// Free a read-only iterator.
SHT_FNATTR(nonnull)
void sht_ro_iter_free_(struct sht_ro_iter *iter);

// Free a read/write iterator.
SHT_FNATTR(nonnull)
void sht_rw_iter_free_(struct sht_rw_iter *iter);


/*
 * Iterator operations - next, delete & replace
 */

// Get the next entry from a read-only iterator.
SHT_FNATTR(nonnull)
const void *sht_ro_iter_next_(struct sht_ro_iter *iter);

// Get the next entry from a read/write iterator.
SHT_FNATTR(nonnull)
void *sht_rw_iter_next_(struct sht_rw_iter *iter);

// Remove the last entry returned by a read/write iterator.
SHT_FNATTR(nonnull)
_Bool sht_iter_delete(struct sht_rw_iter *iter);

// Replace the last entry returned by a read-only iterator.
SHT_FNATTR(nonnull)
_Bool sht_ro_iter_replace_(struct sht_ro_iter *iter,
			   const void *restrict entry);

// Replace the last entry returned by a read/write iterator.
SHT_FNATTR(nonnull)
_Bool sht_rw_iter_replace_(struct sht_rw_iter *iter,
			   const void *restrict entry);


/*
 * Error reporting
 */

// Get the error code of a table's last error.
SHT_FNATTR(nonnull)
enum sht_err sht_get_err(const struct sht_ht *ht);

// Get the error code of a read-only iterator's last error.
SHT_FNATTR(nonnull)
enum sht_err sht_ro_iter_err_(const struct sht_ro_iter *iter);

// Get the error code of a read/write iterator's last error.
SHT_FNATTR(nonnull)
enum sht_err sht_rw_iter_err_(const struct sht_rw_iter *iter);

// Get the description for an error code.
const char *sht_msg(enum sht_err err);

// Get a description of a table's last error.
SHT_FNATTR(nonnull)
const char *sht_get_msg(const struct sht_ht *ht);

// Get a description of a read-only iterator's last error.
SHT_FNATTR(nonnull)
const char *sht_ro_iter_msg_(const struct sht_ro_iter *iter);

// Get a description of a read/write iterator's last error.
SHT_FNATTR(nonnull)
const char *sht_rw_iter_msg_(const struct sht_rw_iter *iter);


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
 * ht = sht_new(hashfn, eqfn, sizeof(struct entry),
 *              _Alignof(struct entry), &err);
 *
 * // Rewrite as ...
 * ht = SHT_NEW(hashfn, eqfn, struct entry, &err);
 *
 * // Without error reporting ...
 * ht = sht_new(hashfn, eqfn, sizeof(struct entry),
 *              _Alignof(struct entry), NULL);
 *
 * // Becomes ...
 * ht = SHT_NEW(hashfn, eqfn, struct entry);
 * ```
 *
 * @param	hashfn	The function that will be used to compute the hash
 *			values of keys.
 * @param	eqfn	The function that will be used to compare keys for
 *			equality.
 * @param	etype	The type of the entries to be stored in the table.
 * @param[out]	...	Optional output pointer for error reporting.
 *
 * @returns	On success, a pointer to the new hash table is returned.  On
 *		error, `NULL` is returned, and an error code is returned in
 *		@p err (if it is not `NULL`).
 *
 * @see		sht_new_()
 */
#define SHT_NEW(hashfn, eqfn, etype, ...)				\
	({								\
		_Static_assert(sizeof(etype) <= SHT_MAX_ESIZE,		\
			       "Entry type (" #etype ") too large");	\
		sht_new_(hashfn, eqfn, sizeof(etype), _Alignof(etype),	\
			SHT_ARG2(_, ##__VA_ARGS__, NULL));		\
	})

/*
 * Iterator generics
 */

/**
 * @internal
 * Template macro for generic iterator helpers.
 *
 * @param	op	The "operation" portion of the underlying function
 *			names.  E.g. `next` generates a helper macro for
 *			sht_ro_iter_next_() and sht_rw_iter_next_().
 * @param	iter	The iterator that will be passed to the underlying
 *			function.
 * @param	...	Any additional arguments that will be passed to the
 *			underlying function.
 *
 * @returns	This macro expands to a generic helper macro that calls the
 *		correct underlying function for read-only or read/write
 *		iterators.
 */
#define SHT_ITER_GENERIC(op, iter, ...)				\
	_Generic((iter),					\
		 struct sht_ro_iter *:	sht_ro_iter_##op##_,	\
		 struct sht_rw_iter *:	sht_rw_iter_##op##_	\
	)(iter, ##__VA_ARGS__)

/**
 * Free a hash table iterator.
 *
 * @param	iter	The iterator.
 *
 * @see		sht_ro_iter_free_()
 * @see		sht_rw_iter_free_()
 */
#define SHT_ITER_FREE(iter)		SHT_ITER_GENERIC(free, iter)

/**
 * Get the next entry from an iterator.
 *
 * @param	iter	The iterator.
 *
 * @returns	A pointer to the next entry, if any.  If no more entries are
 *		available, `NULL` is returned.
 *
 * @see		sht_ro_iter_next_()
 * @see		sht_rw_iter_next_()
 */
#define	SHT_ITER_NEXT(iter)		SHT_ITER_GENERIC(next, iter)


/**
 * Replace the last entry returned by an iterator.
 *
 * > **WARNING**
 * >
 * > The new entry **must** have the same key as the entry being replaced.
 * > Replacing an entry with an entry that contains a different key will corrupt
 * > the table.
 *
 * @param	iter	The iterator.
 * @param	entry	The new entry.
 *
 * @returns	On success, true (`1`) is returned.  On error, false (`0`) is
 *		returned and the error status of the iterator is set.
 *
 * @see		sht_ro_iter_replace_()
 * @see		sht_rw_iter_replace_()
 */
#define SHT_ITER_REPLACE(iter, entry)	SHT_ITER_GENERIC(replace, iter, entry)

/**
 * Get the the error code of an iterator's last error.
 *
 * The value returned by this macro is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A code that describes the error.
 *
 * @see		sht_ro_iter_status_()
 * @see		sht_rw_iter_status_()
 */
#define SHT_ITER_ERR(iter)		SHT_ITER_GENERIC(err, iter)

/**
 * Get a description of an iterator's last error.
 *
 * The value returned by this macro is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A string that describes the error.
 *
 * @see		sht_ro_iter_msg_()
 * @see		sht_rw_iter_msg_()
 */
#define SHT_ITER_MSG(iter)		SHT_ITER_GENERIC(msg, iter)


#endif		/* SHT_H */
