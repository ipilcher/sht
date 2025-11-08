// SPDX-License-Identifier: GPL-3.0-or-later

/*
 *
 * 	SHT - hash table with "Robin Hood" probing
 *
 *	Copyright 2025 Ian Pilcher <arequipeno@gmail.com>
 *
 */


/** @file */


#include "sht.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @internal
 * Default initial capacity.
 */
#define SHT_DEF_CAPCITY		6

/**
 * @internal
 * Default load factor threshold.
 */
#define SHT_DEF_LFT		85

/**
 * @internal
 * Default maximum PSL.
 */
#define SHT_DEF_PSL_LIMIT	UINT8_C(127)

/**
 * @internal
 * Maximum table size (16,777,216).
 */
#define SHT_MAX_TSIZE		(UINT32_C(1) << 24)

/**
 * @internal
 * Maximum number of read-only iterators on a table.
 */
#define SHT_MAX_ITERS		UINT16_C(0x7fff)

/**
 * @private
 * Hash table bucket structure ("SHT bucket").
 */
union sht_bckt {
	struct {
		uint32_t	hash:24;	/**< Hash (low 24 bits). */
		uint32_t	psl:7;		/**< Probe sequence length. */
		uint32_t	empty:1;	/**< Is this bucket empty? */
	};
	uint32_t		all;		/**< All 32 bits. */
};

/**
 * @private
 * A hash table.
 */
struct sht_ht {
	//
	// Arrays are allocated when table is initialized or resized.
	//
	union sht_bckt	*buckets;	/**< Array of SHT buckets. */
	uint8_t		*entries;	/**< Array of entries. */
	//
	// The next 9 members don't change once the table is initialized.
	//
	sht_hashfn_t	hashfn;		/**< Hash function. */
	void		*hash_ctx;	/**< Context for hash function. */
	sht_eqfn_t	eqfn;		/**< Equality function. */
	void		*eq_ctx;	/**< Context for equality function. */
	sht_freefn_t	freefn;		/**< Entry resource free function. */
	void		*free_ctx;	/**< Context for free function. */
	uint32_t	esize;		/**< Size of each entry in the table. */
	uint32_t	ealign;		/**< Alignment of table entries. */
	uint32_t	lft;		/**< Load factor threshold * 100. */
	uint8_t		psl_limit;	/**< Maximum allowed PSL. */
	//
	// The next 3 members change whenever the arrays are (re)allocated.
	//
	uint32_t	tsize;		/**< Number of buckets (table size). */
	uint32_t	mask;		/**< Hash -> index bitmask. */
	uint32_t	thold;		/**< Expansion threshold. */
	//
	// These 6 members change as entries are added and removed.
	//
	uint32_t	count;		/**< Number of occupied buckets. */
	uint32_t	psl_sum;	/**< Sum of all PSLs. */
	uint32_t	max_psl_ct;	/**< Number of entries with max PSL. */
	enum sht_err	err;		/**< Last error. */
	uint8_t		peak_psl;	/**< Largest PSL in table. */
	//
	// Iterator reference count or r/w lock status
	//
	uint16_t	iter_lock;	/**< Iterator lock. */
};

/**
 * @private
 * Iterator types.
 */
enum sht_iter_type: _Bool {
	SHT_ITER_RO = 0,	/**< Read-only iterator. */
	SHT_ITER_RW = 1		/**< Read/write iterator. */
};

/**
 * @private
 * Hash table iterator.
 */
struct sht_iter {
	struct sht_ht		*ht;	/**< Table. */
	int32_t			last;	/**< Position of last entry returned. */
	enum sht_err		err;	/**< Last error. */
	enum sht_iter_type	type;	/**< Type of iterator (ro/rw). */
};

/**
 * @private
 * Read-only hash table iterator.
 */
struct sht_ro_iter {
	struct sht_iter		i;	/**< Iterator implementation. */
};

/**
 * @private
 * Read/write hash table iterator.
 */
struct sht_rw_iter {
	struct sht_iter		i;	/**< Iterator implementation. */
};

/**
 * Default critical error printing function.
 *
 * @param	msg	The error message.
 *
 * @see		sht_abort()
 */
static void sht_err_print(const char *msg)
{
	fprintf(stderr, "Fatal SHT error: %s\n", msg);
}

/**
 * @private
 */
void (*sht_abort_print)(const char *msg) = sht_err_print;

/**
 * Print an error message and abort the program.
 *
 * @param	msg	The error message.
 */
SHT_FNATTR(noreturn)
static void sht_abort(const char *msg)
{
	sht_abort_print(msg);
	abort();
}

/**
 * Get the error code of a table's last error.
 *
 * The value returned by this function is only valid after a previous function
 * call indicated an error.
 *
 * @param	ht	The hash table.
 *
 * @returns	A code that describes the error.
 */
enum sht_err sht_get_err(const struct sht_ht *ht)
{
	return ht->err;
}

/**
 * Get the description for an error code.
 *
 * **NOTE:** This function will abort the calling program if an invalid value of
 * @p err is specified.
 *
 * @param	err	The error code.  Must in the range `SHT_ERR_OK` to
 *			`SHT_ERR_COUNT - 1`.
 *
 * @returns	A string describing the error code.
 */
const char *sht_msg(enum sht_err err)
{
	static const char *err_msgs[] = {
		[SHT_ERR_OK]		= "No error",
		[SHT_ERR_ALLOC]		= "Memory allocation failed",
		[SHT_ERR_BAD_ESIZE]	= "Entry size too large (> 16KiB)",
		[SHT_ERR_TOOBIG]	= "Requested table size too large",
		[SHT_ERR_BAD_HASH]	= "Too many hash collisions",
		[SHT_ERR_ITER_LOCK]	= "Can't acquire iterator lock",
		[SHT_ERR_ITER_COUNT]	= "Table has too many iterators",
		[SHT_ERR_ITER_NO_LAST]	= "Iterator at beginning or end",
	};

	/* Ensure that we have a message for every code. */
	_Static_assert(sizeof err_msgs / sizeof err_msgs[0] == SHT_ERR_COUNT,
		       "sht_errs size");

	if (err >= SHT_ERR_COUNT)
		sht_abort("sht_msg: Invalid error code");

	return err_msgs[err];
}

/**
 * Get a description of a table's last error.
 *
 * The value returned by this function is only valid after a previous function
 * call indicated an error.
 *
 * @param	ht	The hash table.
 *
 * @returns	A string describing the error.
 */
const char *sht_get_msg(const struct sht_ht *ht)
{
	return sht_msg(ht->err);
}

/**
 * Check that a `nonnull` pointer really isn't `NULL`.
 *
 * The public API (`sht.h`) declares most pointer arguments to be non-`NULL`
 * (using the `nonnull` function attribute).  This causes GCC to issue a warning
 * when it determines that one of these function arguments is `NULL`, which is a
 * very desirable behavior.
 *
 * However, it also has the effect of allowing GCC to assume that the value of
 * these arguments will **never** be `NULL`, even though that is not actually
 * enforced.  Because of this assumption, explicit checks for `NULL` values will
 * usually be optimized away.
 *
 * This function endeavors to "force" the compiler to check that a pointer value
 * is not `NULL`, even when its been told that it can't be.
 *
 * @param	p	The pointer to be checked.
 * @param	msg	Error message if pointer is `NULL`.
 *
 * @see		sht_abort()
 */
#ifdef __clang__
SHT_FNATTR(optnone, noinline)
#else
SHT_FNATTR(optimize(0), noinline)
#endif
static void sht_assert_nonnull(const void *p, const char *msg)
{
	volatile const void *vp = p;

	if (vp == NULL)
		sht_abort(msg);
}

/**
 * (Create a new hash table.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_NEW().
 *
 * A table returned by this function cannot be used until it has been
 * initialized.
 *
 * @param	hashfn	The function that will be used to compute the hash
 *			values of keys.
 * @param	eqfn	The function that will be used to compare keys for
 *			equality.
 * @param	esize	The size of the entries to be stored in the table.
 * @param	ealign	The alignment of the entries to be stored in the table.
 * @param[out]	err	Optional output pointer for error reporting.
 *
 * @returns	On success, a pointer to the new hash table is returned.  On
 *		error, `NULL` is returned, and an error code is returned in
 *		@p err (if it is not `NULL`).
 *
 * @see		SHT_NEW()
 *
 */
struct sht_ht *sht_new_(sht_hashfn_t hashfn, sht_eqfn_t eqfn, size_t esize,
			size_t ealign, enum sht_err *err)
{
	struct sht_ht *ht;

	// Casts suppress warnings about mixing function and object pointers
	sht_assert_nonnull((const void *)(uintptr_t)hashfn,
			   "sht_new_: hashfn must not be NULL");
	sht_assert_nonnull((const void *)(uintptr_t)eqfn,
			   "sht_new_: eqfn must not be NULL");
	if (__builtin_popcountg(ealign) != 1)
		sht_abort("sht_new_: ealign not a power of 2");
	if (esize % ealign != 0)
		sht_abort("sht_new_: Incompatible values of esize and ealign");

	if (esize > SHT_MAX_ESIZE) {
		err != NULL && (*err = SHT_ERR_BAD_ESIZE);
		return NULL;
	}

	if ((ht = calloc(1, sizeof *ht)) == NULL) {
		err != NULL && (*err = SHT_ERR_ALLOC);
		return NULL;
	}

	ht->hashfn = hashfn;
	ht->eqfn = eqfn;
	ht->esize = esize;
	ht->ealign = ealign;
	ht->lft = SHT_DEF_LFT;
	ht->psl_limit = SHT_DEF_PSL_LIMIT;

	return ht;
}

/**
 * Set the "context" for a table's hash function.
 *
 * Sets the value of the @p context argument for all calls to the table's hash
 * function.
 *
 * > **NOTE**
 * >
 * > This function cannot be called after the table has been initialized.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	context	The function-specific context.
 *
 * @see		sht_hashfn_t
 * @see		[Abort conditions](index.html#abort-conditions)
 */
void sht_set_hash_ctx(struct sht_ht *ht, void *context)
{
	if (ht->tsize != 0)
		sht_abort("sht_set_hash_ctx: Table already initialized");
	ht->hash_ctx = context;
}

/**
 * Set the "context" for a table's equality function.
 *
 * Sets the value of the @p context argument for all calls to the table's
 * equality function.
 *
 * > **NOTE**
 * >
 * > This function cannot be called after the table has been initialized.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	context	The function-specific context.
 *
 * @see		sht_eqfn_t
 * @see		[Abort conditions](index.html#abort-conditions)
 */
void sht_set_eq_ctx(struct sht_ht *ht, void *context)
{
	if (ht->tsize != 0)
		sht_abort("sht_set_eq_ctx: Table already initialized");
	ht->eq_ctx = context;
}

/**
 * Set the optional entry resource free function for a table.
 *
 * An entry free function is used to automatically free resources associated
 * with table entries.  It is not required in order to free the entries
 * themselves.
 *
 * > **NOTE**
 * >
 * > This function cannot be called after the table has been initialized.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	freefn	The function to be used to free entry resources.
 * @param	context	Optional function-specific context.
 *
 * @see		sht_freefn_t
 * @see		[Abort conditions](index.html#abort-conditions)
 */
void sht_set_freefn(struct sht_ht *ht, sht_freefn_t freefn, void *context)
{
	if (ht->tsize != 0)
		sht_abort("sht_set_freefn: Table already initialized");
	ht->freefn = freefn;
	ht->free_ctx = context;
}

/**
 * Set the load factor threshold for a table.
 *
 * The load factor threshold (LFT) determines when a table is expanded, in order
 * to accomodate additional entries.  The size of the table is doubled when the
 * number of entries it contains exceeds a certain percentage of its size.  That
 * percentage is determined by the LFT.  Thus, the LFT must be between 1 and
 * 100, although values much different from the default (85) are unlikely to be
 * very useful.
 *
 * > **NOTE**
 * >
 * > This function cannot be called after the table has been initialized, nor
 * > can it be called with an invalid @p lft value.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	lft	The load factor threshold (`1` - `100`).
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
void sht_set_lft(struct sht_ht *ht, uint8_t lft)
{
	if (ht->tsize != 0)
		sht_abort("sht_set_lft: Table already initialized");
	if (lft < 1 || lft > 100)
		sht_abort("sht_set_lft: Invalid load factor threshold");
	ht->lft = lft;
}

/**
 * Set the PSL limit of a table.
 *
 * If an entry in the table has a PSL equal to the table's PSL limit, no
 * further entries will be allowed until 1 or more entries that hash to the same
 * "ideal" position are removed.  (See [Limits and assumptions][1].)
 *
 * > **NOTE**
 * >
 * > This function cannot be called after the table has been initialized, nor
 * > can it be called with an invalid @p limit value.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	limit	The PSL limit (`1` - `127`).
 *
 * @see		[Limits and assumptions][1]
 * @see		[Abort conditions](index.html#abort-conditions)
 *
 * [1]: index.html#limits-and-assumptions
 */
void sht_set_psl_limit(struct sht_ht *ht, uint8_t limit)
{
	if (ht->tsize != 0)
		sht_abort("sht_set_psl_limit: Table already initialized");
	if (limit < 1 || limit > 127)
		sht_abort("sht_set_psl_limit: Invalid PSL threshold");
	ht->psl_limit = limit;
}

/**
 * Allocate new arrays for a table.
 *
 * Allocates memory for a table's `buckets` and `entries` arrays.  If allocation
 * is successful, `ht->tsize`, `ht->mask`, and `ht->thold` are updated for the
 * new size, and `ht->count`, `ht->psl_sum`, `ht->peak_psl`, and
 * `ht->max_psl_ct` are reset to 0. If an error occurs, the state of the table
 *  is unchanged.
 *
 * @param	ht	The hash table.
 * @param	tsize	The new size (total number of buckets) of the table.
 *
 * @returns	On success, true (`1`) is returned.  On failure, false (`0`) is
 *		returned, and the table's error status is set.  (The state of
 *		the table is otherwise unchanged.)
 */
static _Bool sht_alloc_arrays(struct sht_ht *ht, uint32_t tsize)
{
	size_t b_size;	// size of bucket array
	size_t e_size;	// size of entry array
	size_t pad;	// padding to align entry array
	size_t size;	// total size
	uint8_t *new;

	_Static_assert(SHT_MAX_TSIZE == 1 << 24, "maximum table size");
	_Static_assert(sizeof(union sht_bckt) == 4, "sht_bckt size");
	_Static_assert(_Alignof(union sht_bckt) == 4, "sht_bckt alignment");

	assert(tsize <= SHT_MAX_TSIZE);

	b_size = tsize * sizeof(union sht_bckt);  /* Max result is 2^26 */

	if (__builtin_mul_overflow(tsize, ht->esize, &e_size)) {
		ht->err = SHT_ERR_TOOBIG;
		return 0;
	}

	pad = (ht->ealign - b_size % ht->ealign) % ht->ealign;

	if (__builtin_add_overflow(b_size + pad, e_size, &size)) {
		ht->err = SHT_ERR_TOOBIG;
		return 0;
	}

	if ((new = malloc(size)) == NULL) {
		ht->err = SHT_ERR_ALLOC;
		return 0;
	}

	// Mark all of the newly allocated buckets as empty.
	memset(new, 0xff, b_size);

	ht->buckets = (union sht_bckt *)(void *)new;
	ht->entries = new + b_size + pad;
	ht->tsize = tsize;
	ht->mask = tsize - 1;			// e.g. 0x8000 - 1 = 0x7fff
	ht->thold = tsize * ht->lft / 100;	// 2^24 * 100 < 2^32
	ht->count = 0;
	ht->psl_sum = 0;
	ht->peak_psl = 0;
	ht->max_psl_ct = 0;

	return 1;
}

/**
 * Initialize a hash table.
 *
 * @p capacity, along with the table's load factor threshold, is used to
 * calculate the minimum initial size of the table.  Setting an appropriate
 * initial size will avoid the need to resize the table as it grows (but will
 * consume unnecessary memory if fewer keys are stored in the table than
 * expected).
 *
 * If @p capacity is `0`, a default initial capacity (currently `6`) is used.
 *
 * > **NOTE**
 * >
 * > If this function succeeds, it must not be called again on the same table.
 * > A failed call may be retried, possibly with a lower @p capacity.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht		The hash table to be initialized.
 * @param	capacity	The initial capacity of the hash table (or `0`).
 *
 * @returns	On success, true (`1`) is returned.  On failure, false (`0`) is
 *		returned, and the table's error status is set.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_init(struct sht_ht *ht, uint32_t capacity)
{
	if (ht->tsize != 0)
		sht_abort("sht_init: Table already initialized");

	// Initial check avoids overflow below (SHT_MAX_TSIZE = 2^24)
	if (capacity > SHT_MAX_TSIZE) {
		ht->err = SHT_ERR_TOOBIG;
		return 0;
	}

	if (capacity == 0)
		capacity = SHT_DEF_CAPCITY;

	// Calculate required size at LFT (max result is less than 2^31)
	capacity = (capacity * 100 + ht->lft - 1) / ht->lft;

	// Find smallest power of 2 that is >= capacity (max result 2^31)
	capacity = UINT32_C(1) << (32 - __builtin_clzg(capacity - 1, 1));

	// Check final capacity
	if (capacity > SHT_MAX_TSIZE) {
		ht->err = SHT_ERR_TOOBIG;
		return 0;
	}

	return sht_alloc_arrays(ht, capacity);
}

/**
 * Get the number of entries in a table.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 *
 * @returns	The number of entries in the table.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
uint32_t sht_size(const struct sht_ht *ht)
{
	if (ht->tsize == 0)
		sht_abort("sht_size: Table not initialized");
	return ht->count;
}

/**
 * Determine whether a table is empty.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 *
 * @returns	True (`1`) if the table is empty; false (`0`) if it has at least
 *		one entry.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_empty(const struct sht_ht *ht)
{
	if (ht->tsize == 0)
		sht_abort("sht_empty: Table not initialized");
	return ht->count == 0;
}

/**
 * Low level insert function.
 *
 * Copies candidate entry & bucket into table and updates table statistics.
 *
 * @param	ht	The hash table.
 * @param	c_entry	The entry to be inserted.
 * @param	c_bckt	The bucket to be inserted.
 * @param	o_entry	Destination for @p c_entry.
 * @param	o_bckt	Destination for @p c_bckt.
 */
static void sht_set_entry(struct sht_ht *ht, const uint8_t *restrict c_entry,
			  const union sht_bckt *restrict c_bckt,
			  uint8_t *restrict o_entry,
			  union sht_bckt *restrict o_bckt)
{
	*o_bckt = *c_bckt;
	memcpy(o_entry, c_entry, ht->esize);

	ht->count++;
	ht->psl_sum += c_bckt->psl;

	if (c_bckt->psl > ht->peak_psl)
		ht->peak_psl = c_bckt->psl;

	if (c_bckt->psl == ht->psl_limit) {
		ht->max_psl_ct++;
		assert(ht->max_psl_ct < ht->thold);  // should be much lower
	}
}

/**
 * Low level remove function.
 *
 * Copies entry & bucket from table to temporary storage and updates table
 * statistics.
 *
 * @param	ht	The hash table.
 * @param	o_entry	The entry to be removed.
 * @param	o_bckt	The bucket to be removed.
 * @param	t_entry	Destination for @p o_entry.
 * @param	t_bckt	Destination for @p o_bckt.
 */
static void sht_remove_entry(struct sht_ht *ht, const uint8_t *restrict o_entry,
			     const union sht_bckt *restrict o_bckt,
			     uint8_t *restrict t_entry,
			     union sht_bckt *restrict t_bckt)
{
	*t_bckt = *o_bckt;
	memcpy(t_entry, o_entry, ht->esize);

	ht->count--;
	ht->psl_sum -= o_bckt->psl;

	if (o_bckt->psl == ht->psl_limit) {
		assert(ht->max_psl_ct > 0);
		ht->max_psl_ct--;
	}
}

/**
 * Finds or inserts the specified key or entry in the table.
 *
 * Implements the core "Robin Hood" probing algorithm, used for lookup and
 * insertion, as well as populating newly allocated bucket and entry arrays
 * during rehashing.
 *
 * The mode of operation depends on the values of @p key, @p ce, and @p c_uniq.
 *
 * |      |  **key** |  **ce**  |**c_uniq**|
 * |------|:--------:|:--------:|:---------|
 * |search|non-`NULL`|  `NULL`  |  `false` |
 * |insert|non-`NULL`|non-`NULL`|  `false` |
 * |rehash|  `NULL`  |non-`NULL`|  `true`  |
 *
 * @param	ht	The hash table.
 * @param	hash	Hash of @p key.
 * @param	key	The key to be found or added.
 * @param	ce	"Candidate" entry to insert (if not already present).
 * @param	c_uniq	Is @p key known to not be present in the table?
 *
 * @returns	If @p key is already present in the table, its position (index)
 *		is returned.
 * @returns	`-1` indicates that @p key was not present in the table.  In
 *		insert mode, this indicates that it has been successfully added.
 * @returns	`-2` is returned only in insert mode.  It indicates that @p key
 *		is not present in the table, but the table must be expanded
 *		before it can be added.
 * @returns	The state of the table is unchanged, except when this function
 *		is called in insert mode and returns `-1`.
 */
static int32_t sht_probe(struct sht_ht *ht, uint32_t hash, const void *key,
			 const uint8_t *ce, _Bool c_uniq)
{
	uint8_t e_tmp[2][ht->esize];	// temp storage for displaced entries
	union sht_bckt b_tmp[2];	// temp storage for displaced buckets
	_Bool ti;			// index for e_tmp and b_tmp
	union sht_bckt *cb;		// candidate bucket
	union sht_bckt *ob;		// current position occupant bucket
	uint8_t *oe;			// current position occupant entry
	uint32_t p;			// current position (index)

	assert(	(key != NULL && ce == NULL && c_uniq == 0)	/* search */
		|| (key != NULL && ce != NULL && c_uniq == 0)	/* insert */
		|| (key == NULL && ce != NULL && c_uniq == 1)	/* rehash */
	);

	cb = b_tmp;  // Initial candidate bucket goes in b_tmp[0]
	cb->hash = hash;
	cb->psl = 0;
	cb->empty = 0;
	ti = 1;  // so 1st displacement doesn't overwrite the candidate bucket

	p = hash;  // masked to table size in loop

	while (1) {

		p &= ht->mask;
		ob = ht->buckets + p;
		oe = ht->entries + p * ht->esize;

		// Empty position?
		if (ob->empty) {
			if (ce != NULL) {
				if (ht->count == ht->thold)  // rehash needed?
					return -2;
				sht_set_entry(ht, ce, cb, oe, ob);
			}
			return -1;
		}

		// Found key?
		if (!c_uniq
			&& cb->all == ob->all
			&& ht->eqfn(key, oe, ht->eq_ctx)
		) {
			return p;
		}

		// Found later bucket group?
		if (cb->psl > ob->psl) {
			// If we're just searching, we're done
			if (ce == NULL)
				return -1;
			// Only need this check before 1st displacement
			if (!c_uniq && ht->count == ht->thold)
				return -2;
			// Move occupant to temp slot & adjust table stats
			sht_remove_entry(ht, oe, ob, e_tmp[ti], b_tmp + ti);
			// Move candidate to current pos'n & adjust table stats
			sht_set_entry(ht, ce, cb, oe, ob);
			// Old occupant (in temp slot) is new candidate
			cb = b_tmp + ti;
			ce = e_tmp[ti];
			// Use other temp slot next time (don't overwrite)
			ti ^= 1;
			// New candidate was in table; must be unique
			c_uniq = 1;
		}

		assert(cb->psl < ht->psl_limit);
		cb->psl++;
		p++;
	}
}


/**
 * Doubles the size of the table.
 *
 * @param	ht	The hash table.
 *
 * @returns	On success, true (`1`) is returned.  On failure, false (`0`) is
 *		returned, and the table's error status is set.  (The state of
 *		the table is otherwise unchanged.)
 */
static _Bool sht_ht_grow(struct sht_ht *ht)
{
	union sht_bckt *b, *old;
	uint8_t *e;
	int result;
	uint32_t i;

	if (ht->tsize == SHT_MAX_TSIZE) {
		ht->err = SHT_ERR_TOOBIG;
		return 0;
	}

	old = ht->buckets;  // save to free
	b = ht->buckets;
	e = ht->entries;

	if (!sht_alloc_arrays(ht, ht->tsize * 2))
		return 0;

	for (i = 0; i < ht->tsize / 2; ++i, ++b, e += ht->esize) {
		if (!b->empty) {
			result = sht_probe(ht, b->hash, NULL, e, 1);
			assert(result == -1);
		}
	}

	free(old);

	return 1;
}

/**
 * Add an entry to a table.
 *
 * If @p key is already present in the table, the behavior depends on the value
 * of @p replace.  If it is true (`1`), the existing entry will be replaced by
 * @p entry; if it is false (`0`), the existing entry will be left in place.
 *
 * @param	ht	The hash table.
 * @param	key	The key of the new entry.
 * @param	entry	The new entry.
 * @param	replace	How to handle duplicate key.
 *
 * @returns	If an error occurs, `-1` is returned, the error status of the
 *		table is set, and the state of the table is otherwise unchanged.
 *		On success, `0` is returned if the key was not already present
 *		in the table, and `1` is returned if the key was already
 *		present.
 *
 * @see		sht_add()
 * @see		sht_set()
 */
static int sht_insert(struct sht_ht *ht, const void *key,
		      const void *entry, _Bool replace)
{
	uint32_t hash;
	int32_t result;
	uint8_t *current;

	if (ht->tsize == 0)
		sht_abort("sht_add/sht_set: Table not initialized");
	if (ht->iter_lock != 0)
		sht_abort("sht_add/sht_set: Table has iterator(s)");

	assert(key != NULL && entry != NULL);

	if (ht->max_psl_ct != 0) {
		ht->err = SHT_ERR_BAD_HASH;
		return -1;
	}

	hash = ht->hashfn(key, ht->hash_ctx);

	result = sht_probe(ht, hash, key, entry, 0);

	if (result >= 0) {
		if (replace) {
			current = ht->entries + result * ht->esize;
			if (ht->freefn != NULL)
				ht->freefn(current, ht->free_ctx);
			memcpy(current, entry, ht->esize);
		}
		return 1;
	}

	if (result == -1)
		return 0;

	assert(result == -2);

	if (!sht_ht_grow(ht))
		return -1;

	result = sht_probe(ht, hash, NULL, entry, 1);
	assert(result == -1);
	return 0;
}

/**
 * Add an entry to the table, if its key is not already present.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table or a table that has
 * > one or more iterators.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key of the new entry.
 * @param	entry	The new entry.
 *
 * @returns	If an error occurs, `-1` is returned, the error status of the
 *		table is set, and the state of the table is otherwise unchanged.
 *		On success, `0` is returned if the key was not already present
 *		in the table, and the new entry has been added; `1` indicates
 *		that the key was already present in the table, and the state of
 *		the table is unchanged.
 *
 * @see		sht_set()
 * @see		[Abort conditions](index.html#abort-conditions)
 */
int sht_add(struct sht_ht *ht, const void *key, const void *entry)
{
	return sht_insert(ht, key, entry, 0);
}

/**
 * Unconditionally set the value associated with a key.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table or a table that has
 * > one or more iterators.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key of the new entry.
 * @param	entry	The new entry.
 *
 * @returns	If an error occurs, `-1` is returned, the error status of the
 *		table is set, and the state of the table is otherwise unchanged.
 *		On success, `0` is returned if the key was not already present
 *		in the table, and the new entry has been added; `1` indicates
 *		that the key was already present in the table, and the new entry
 *		has replaced it.
 *
 * @see		sht_add()
 * @see		[Abort conditions](index.html#abort-conditions)
 */
int sht_set(struct sht_ht *ht, const void *key, const void *entry)
{
	return sht_insert(ht, key, entry, 1);
}

/**
 * Lookup an entry in a table.
 *
 * > **WARNING**
 * >
 * > The pointer returned by this function is only valid until the next time the
 * > table is changed.  Structural changes to the table (adding or removing
 * > keys) can cause other entries to be moved within the table, making pointers
 * > to those entries invalid.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the entry is to be retrieved.
 *
 * @returns	If the the key is present in the table, a pointer to the key's
 *		entry is returned.  Otherwise, `NULL` is returned.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
const void *sht_get(struct sht_ht *ht, const void *restrict key)
{
	uint32_t hash;
	int32_t result;

	if (ht->tsize == 0)
		sht_abort("sht_get: Table not initialized");

	hash = ht->hashfn(key, ht->hash_ctx);
	result = sht_probe(ht, hash, key, NULL, 0);

	if (result < 0) {
		assert(result == -1);
		return NULL;
	}

	return ht->entries + result * ht->esize;
}

/**
 * Change the entry at a known position.
 *
 * @param	ht	The hash table.
 * @param	pos	The position of the entry to be replaced.
 * @param	entry	The new entry.
 * @param[out]	out	Output buffer for previous entry (or `NULL`).  @p out
 *			may point to the same object as @p entry.
 *
 * @see		sht_change()
 */
static void sht_change_at(struct sht_ht *ht, uint32_t pos,
			  const void *entry, void *out)
{
	uint8_t *e;

	e = ht->entries + pos * ht->esize;

	if (out == NULL) {
		if (ht->freefn != NULL)
			ht->freefn(e, ht->free_ctx);
		memcpy(e, entry, ht->esize);
	}
	else if (out == entry) {
		uint8_t tmp[ht->esize];
		memcpy(tmp, e, ht->esize);
		memcpy(e, entry, ht->esize);
		memcpy(out, tmp, ht->esize);
	}
	else {
		memcpy(out, e, ht->esize);
		memcpy(e, entry, ht->esize);
	}
}

/**
 * Change the entry associated with an existing key.
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the value is to be changed.
 * @param	entry	The new entry.
 * @param[out]	out	Output buffer for previous entry (or `NULL`).  @p out
 *			may point to the same object as @p entry.
 *
 * @returns	If the key was present in the table, true (`1`) is returned, the
 *		entry associated with the key is replaced with the new entry,
 *		and the previous entry is copied to @p out (if it is not
 *		`NULL`).  Otherwise, false (`0`) is returned (and the contents
 *		of @p out are unchanged).
 *
 * @see		sht_replace()
 * @see		sht_swap()
 */
static _Bool sht_change(struct sht_ht *ht, const void *key,
			const void *entry, void *out)
{
	uint32_t hash;
	int32_t pos;

	if (ht->tsize == 0)
		sht_abort("sht_replace/sht_swap: Table not initialized");

	hash = ht->hashfn(key, ht->hash_ctx);
	pos = sht_probe(ht, hash, key, NULL, 0);

	if (pos < 0) {
		assert(pos == -1);
		return 0;
	}

	sht_change_at(ht, pos, entry, out);

	return 1;
}

/**
 * Replace the entry associated with an existing key.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the value is to be replaced.
 * @param	entry	The new entry for the key.
 *
 * @returns	If the key was present in the table, true (`1`) is returned, and
 *		the entry associated with the key is replaced with the new
 *		entry.  Otherwise, false (`0`) is returned.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_replace(struct sht_ht *ht, const void *key, const void *entry)
{
	return sht_change(ht, key, entry, NULL);
}

/**
 * Exchange an existing entry and a new entry.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the value is to be replaced.
 * @param	entry	The new entry for the key.
 * @param	out	Output buffer for the previous entry.  Must be large
 *			enough to hold an entry.  (@p out may point to the same
 *			object as @p entry.)
 *
 * @returns	If the key was present in the table, true (`1`) is returned, the
 *		entry associated with the key is replaced with the new entry,
 *		and the previous entry is copied to @p out.  Otherwise, false
 *		(`0`) is returned, and the contents of @p out are unchanged.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_swap(struct sht_ht *ht, const void *key, const void *entry, void *out)
{
	return sht_change(ht, key, entry, out);
}

/**
 * Shift a block of entries (and buckets) down by 1 position.
 *
 * This function does **not** handle wrap-around.
 *
 * @param	ht	The hash table.
 * @param	dest	Position to which the block should be moved.
 * @param	count	The number of entries and buckets to be moved.
 */
static void sht_shift(struct sht_ht *ht, uint32_t dest, uint32_t count)
{
	uint32_t i;

	assert(dest + count < ht->tsize);

	// Move entries
	memmove(ht->entries + dest * ht->esize,
		ht->entries + (dest + 1) * ht->esize,
		count * ht->esize);

	// Move buckets
	memmove(ht->buckets + dest,
		ht->buckets + dest + 1,
		count * sizeof(union sht_bckt));

	// Every shifted entry is now 1 position closer to its ideal position
	for (i = dest; i < dest + count; ++i) {

		if (ht->buckets[i].psl == ht->psl_limit) {
			assert(ht->max_psl_ct > 0);
			ht->max_psl_ct--;
		}

		ht->buckets[i].psl--;
	}

	// PSL total decreased by 1x number of moved entries
	ht->psl_sum -= count;
}

/**
 * Shift the entry at position 0 "down" to the last position in the table.
 *
 * @param	ht	The hash table.
 */
static void sht_shift_wrap(struct sht_ht *ht)
{
	// ht->mask is also index of last position

	// Move entry
	memcpy(ht->entries + ht->mask * ht->esize, ht->entries, ht->esize);

	// Move bucket
	ht->buckets[ht->mask] = ht->buckets[0];

	// Entry is now 1 position closer to its ideal position
	if (ht->buckets[ht->mask].psl == ht->psl_limit) {
		assert(ht->max_psl_ct > 0);
		ht->max_psl_ct--;
	}

	ht->buckets[ht->mask].psl--;
	ht->psl_sum--;
}

/**
 * Remove and possibly return an entry at a known position.
 *
 * @param	ht	The hash table.
 * @param	pos	The position of the entry to be removed.
 * @param[out]	out	Entry output buffer (or `NULL`).
 *
 * @see		sht_remove()
 */
static void sht_remove_at(struct sht_ht *ht, uint32_t pos, void *restrict out)
{
	uint32_t end, next;

	// Copy entry to output buffer or free its resources
	if (out != NULL) {
		memcpy(out, ht->entries + pos * ht->esize, ht->esize);
	}
	else if (ht->freefn != NULL) {
		ht->freefn(ht->entries + pos * ht->esize, ht->free_ctx);
	}

	// Update table stats for removal
	ht->psl_sum -= ht->buckets[pos].psl;
	ht->count--;

	// Find range to shift (if any)
	end = pos;
	next = (pos + 1) & ht->mask;
	while (!ht->buckets[next].empty && ht->buckets[next].psl != 0) {
		end = next;
		next = (next + 1) & ht->mask;
	}

	// Do any necessary shifts
	if ((uint32_t)pos == end) {
		// no shifts needed
	}
	else if ((uint32_t)pos < end) {
		// no wrap-around
		sht_shift(ht, pos, end - pos);
	}
	else {
		// Shift entries up to end of table (if any)
		if ((uint32_t)pos < ht->mask)	// mask is also max index
			sht_shift(ht, pos, ht->mask - pos);

		// Shift entry from position 0 "down" to the end of table
		sht_shift_wrap(ht);

		// Shift entries at the beginning of the table
		sht_shift(ht, 0, end);
	}

	// Mark position at end of range as empty
	ht->buckets[end].empty = 1;
}

/**
 * Remove and possibly return an entry from the table.
 *
 * (If @p out is `NULL`, the entry is simply removed from the table.)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the entry is to be retrieved and
 *			removed.
 * @param[out]	out	Entry output buffer (or `NULL`).
 *
 * @returns	If the key was present in the table, true (`1`) is returned, and
 *		its entry is stored in @p out (if it is not `NULL`).  Otherwise,
 *		false (`0`) is returned (and the contents of @p out are
 *		unchanged).
 *
 * @see		sht_pop()
 * @see		sht_delete()
 */
static _Bool sht_remove(struct sht_ht *ht, const void *restrict key,
			void *restrict out)
{
	uint32_t hash;
	int32_t pos;

	if (ht->tsize == 0)
		sht_abort("sht_pop/sht_delete: Table not initialized");
	if (ht->iter_lock != 0)
		sht_abort("sht_pop/sht_delete: Table has iterator(s)");

	// Find the entry
	hash = ht->hashfn(key, ht->hash_ctx);
	pos = sht_probe(ht, hash, key, NULL, 0);
	if (pos < 0) {
		assert(pos == -1);
		return 0;
	}

	sht_remove_at(ht, pos, out);
	return 1;
}

/**
 * Remove and return an entry from the table.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table or a table that has
 * > one or more iterators.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the entry is to be "popped."
 * @param[out]	out	Entry output buffer.  Must be large enough to hold an
 *			entry.
 * @returns	If the key was present in the table, true (`1`) is returned, and
 *		its entry is stored in @p out.  Otherwise, false (`0`) is
 *		returned (and the contents of @p out are unchanged).
 *
 * @see		sht_delete()
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_pop(struct sht_ht *ht, const void *restrict key, void *restrict out)
{
	return sht_remove(ht, key, out);
}

/**
 * Remove an entry from the table.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table or a table that has
 * > one or more iterators.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 * @param	key	The key for which the entry is to be removed.
 *
 * @returns	If the key was present in the table, true (`1`) is returned.
 *		Otherwise, false (`0`) is returned.
 *
 * @see		sht_pop()
 * @see		[Abort conditions](index.html#abort-conditions)
 */
_Bool sht_delete(struct sht_ht *ht, const void *restrict key)
{
	return sht_remove(ht, key, NULL);
}

/**
 * Free the resources used by a hash table.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on a table that has one or more iterators.
 * > (See [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
void sht_free(struct sht_ht *ht)
{
	uint32_t i;
	uint8_t *e;
	union sht_bckt *b;

	if (ht->iter_lock != 0)
		sht_abort("sht_free: Table has iterator(s)");

	if (ht->freefn != NULL) {

		for (i = 0, b = ht->buckets; i < ht->tsize; ++i, ++b) {

			if (!b->empty) {
				e = ht->entries + i * ht->esize;
				ht->freefn(e, ht->free_ctx);
			}
		}
	}

	free(ht->buckets);
	free(ht);
}

/**
 * Create a new hash table iterator.
 *
 * @param	ht	The hash table.
 * @param	type	The type of the iterator (read-only or read/write).
 *
 * @returns	On success, a pointer to the new iterator is returned.  If
 *		memory allocation fails, `NULL` is returned, and the error
 *		status of the table is set.
 *
 * @see		sht_ro_iter()
 * @see		sht_rw_iter()
 */
static struct sht_iter *sht_iter_new(struct sht_ht *ht,
				     enum sht_iter_type type)
{
	struct sht_iter *iter;
	uint16_t lock;

	if (ht->tsize == 0)
		sht_abort("sht_ro_iter/sht_rw_iter: Table not initialized");

	if (type == SHT_ITER_RO) {

		if (ht->iter_lock == UINT16_MAX) {
			ht->err = SHT_ERR_ITER_LOCK;
			return NULL;
		}

		if ((lock = ht->iter_lock + 1) > SHT_MAX_ITERS) {
			ht->err = SHT_ERR_ITER_COUNT;
			return NULL;
		}
	}
	else {	// SHT_ITER_RW
		if (ht->iter_lock != 0) {
			ht->err = SHT_ERR_ITER_LOCK;
			return NULL;
		}

		lock = UINT16_MAX;
	}

	if ((iter = calloc(1, sizeof *iter)) == NULL) {
		ht->err = SHT_ERR_ALLOC;
		return NULL;
	}

	iter->ht = ht;
	iter->last = -1;
	iter->type = type;

	ht->iter_lock = lock;

	return iter;
}

/**
 * Create a new read-only iterator.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 *
 * @returns	On success, a pointer to the new iterator is returned.  If an
 *		error occurs, `NULL` is returned, and the error status of the
 *		table is set.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
struct sht_ro_iter *sht_ro_iter(struct sht_ht *ht)
{
	return (struct sht_ro_iter *)sht_iter_new(ht, SHT_ITER_RO);
}

/**
 * Create a new read/write iterator.
 *
 * > **NOTE**
 * >
 * > This function cannot be called on an unitialized table.  (See
 * > [Abort conditions](index.html#abort-conditions).)
 *
 * @param	ht	The hash table.
 *
 * @returns	On success, a pointer to the new iterator is returned.  If an
 *		error occurs, `NULL` is returned, and the error status of the
 *		table is set.
 *
 * @see		[Abort conditions](index.html#abort-conditions)
 */
struct sht_rw_iter *sht_rw_iter(struct sht_ht *ht)
{
	return (struct sht_rw_iter *)sht_iter_new(ht, SHT_ITER_RW);
}

/**
 * (Get the error code of a read-only iterator's last error.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_ERR().
 *
 * The value returned by this function is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A code that describes the error.
 *
 * @see		SHT_ITER_STATUS()
 */
enum sht_err sht_ro_iter_err_(const struct sht_ro_iter *iter)
{
	return iter->i.err;
}

/**
 * (Get the error code of a read/write iterator's last error.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_ERR().
 *
 * The value returned by this function is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A code that describes the error.
 *
 * @see		SHT_ITER_STATUS()
 */
enum sht_err sht_rw_iter_err_(const struct sht_rw_iter *iter)
{
	return iter->i.err;
}

/**
 * (Get a description of a read-only iterator's last error.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_MSG().
 *
 * The value returned by this function is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A string that describes the error.
 *
 * @see		SHT_ITER_MSG()
 */
const char *sht_ro_iter_msg_(const struct sht_ro_iter *iter)
{
	return sht_msg(iter->i.err);
}

/**
 * (Get a description of a read/write iterator's last error.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_MSG().
 *
 * The value returned by this function is only valid after a previous iterator
 * function call indicated an error.
 *
 * @param	iter	The iterator.
 *
 * @returns	A string that describes the error.
 *
 * @see		SHT_ITER_MSG()
 */
const char *sht_rw_iter_msg_(const struct sht_rw_iter *iter)
{
	return sht_msg(iter->i.err);
}

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
 * @see		SHT_ITER_FREE()
 */
static void *sht_iter_next(struct sht_iter *iter)
{
	uint32_t next;
	const struct sht_ht *ht;

	if (iter->last == INT32_MAX)
		return NULL;

	assert(iter->last < (int32_t)SHT_MAX_TSIZE);  // 2^24

	if (iter->last == -1)
		next = 0;
	else
		next = iter->last + 1;

	for (ht = iter->ht; next < ht->tsize; ++next) {

		if (!ht->buckets[next].empty) {
			iter->last = next;
			return ht->entries + next * ht->esize;
		}
	}

	iter->last = INT32_MAX;
	return NULL;
}

/**
 * (Get the next entry from a read-only iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_NEXT().
 *
 * @param	iter	The iterator.
 *
 * @returns	A pointer to the next entry, if any.  If no more entries are
 *		available, `NULL` is returned.
 *
 * @see		SHT_ITER_NEXT()
 */
const void *sht_ro_iter_next_(struct sht_ro_iter *iter)
{
	return sht_iter_next(&iter->i);
}

/**
 * (Get the next entry from a read/write iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_NEXT().
 *
 * @param	iter	The iterator.
 *
 * @returns	A pointer to the next entry, if any.  If no more entries are
 *		available, `NULL` is returned.
 *
 * @see		SHT_ITER_NEXT()
 */
void *sht_rw_iter_next_(struct sht_rw_iter *iter)
{
	return sht_iter_next(&iter->i);
}

/**
 * Remove the last entry returned by a read/write iterator.
 *
 * @param	iter	The iterator.
 *
 * @returns	On success, true (`1`) is returned.  On error, false (`0`) is
 *		returned and the error status of the iterator is set.
 */
_Bool sht_iter_delete(struct sht_rw_iter *iter)
{
	if (iter->i.last == -1 || iter->i.last == INT32_MAX) {
		iter->i.err = SHT_ERR_ITER_NO_LAST;
		return 0;
	}

	assert(iter->i.last >= 0 && (uint32_t)iter->i.last < iter->i.ht->tsize);
	assert(!iter->i.ht->buckets[iter->i.last].empty);

	sht_remove_at(iter->i.ht, iter->i.last, NULL);

	// If an entry has been shifted down, return it on next call to
	// SHT_ITER_NEXT
	iter->i.last--;

	return 1;
}

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
 * @see		SHT_ITER_REPLACE()
 */
static _Bool sht_iter_replace(struct sht_iter *iter, const void *restrict entry)
{
	if (iter->last == -1 || iter->last == INT32_MAX) {
		iter->err = SHT_ERR_ITER_NO_LAST;
		return 0;
	}

	assert(iter->last >= 0 && (uint32_t)iter->last < iter->ht->tsize);
	assert(!iter->ht->buckets[iter->last].empty);

	sht_change_at(iter->ht, iter->last, entry, NULL);

	return 1;
}

/**
 * (Replace the last entry returned by a read-only iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_REPLACE().
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
 * @see		SHT_ITER_REPLACE()
 */
_Bool sht_ro_iter_replace_(struct sht_ro_iter *iter,
			   const void *restrict entry)
{
	return sht_iter_replace(&iter->i, entry);
}

/**
 * (Replace the last entry returned by a read/write iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_REPLACE().
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
 * @see		SHT_ITER_REPLACE()
*/
_Bool sht_rw_iter_replace_(struct sht_rw_iter *iter,
			   const void *restrict entry)
{
	return sht_iter_replace(&iter->i, entry);
}

/**
 * Free a hash table iterator.
 *
 * @param	iter	The iterator.
 *
 * @see		sht_ro_iter_free_()
 * @see		sht_rw_iter_free_()
 * @see		SHT_ITER_FREE()
 */
static void sht_iter_free(struct sht_iter *iter)
{
	struct sht_ht *ht;

	ht = iter->ht;

	if (iter->type == SHT_ITER_RO) {
		assert(ht->iter_lock > 0 && ht->iter_lock <= SHT_MAX_ITERS);
		ht->iter_lock--;
	}
	else {
		assert(ht->iter_lock == UINT16_MAX);
		ht->iter_lock = 0;
	}

	free(iter);
}

/**
 * (Free a read-only iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_FREE().
 *
 * @param	iter	The iterator.
 *
 * @see		SHT_ITER_FREE()
 */
void sht_ro_iter_free_(struct sht_ro_iter *iter)
{
	sht_iter_free(&iter->i);
}

/**
 * (Free a read/write iterator.)
 *
 * > **NOTE**
 * >
 * > Do not call this function directly.  Use SHT_ITER_FREE().
 *
 * @param	iter	The iterator.
 *
 * @see		SHT_ITER_FREE()
 */
void sht_rw_iter_free_(struct sht_rw_iter *iter)
{
	sht_iter_free(&iter->i);
}
