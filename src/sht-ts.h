// SPDX-License-Identifier: GPL-3.0-or-later

/*
 *
 * 	SHT - hash table with "Robin Hood" probing
 *
 *	Copyright 2025 Ian Pilcher <arequipeno@gmail.com>
 *
 */


/** @file */


#ifndef SHT_TS_H
#define SHT_TS_H


#include "sht.h"


/*******************************************************************************
 *
 *
 *	Low-level helper macros
 *
 *	See https://github.com/ipilcher/cpp-tricks
 *
 *
 ******************************************************************************/

#ifndef SHT_DOXYGEN

// Conditional expansion
#define SHT_ARG1_NX(a1, ...)		a1
#define SHT_ARG1(...)			SHT_ARG1_NX(__VA_ARGS__)
#define SHT_IF_ELSE(a, b, ...)		SHT_ARG1(__VA_OPT__(a,) b)

// Removing parentheses: Required parentheses
#define SHT_EXPAND(...)			__VA_ARGS__
#define SHT_DEPAREN(x)			SHT_EXPAND x

// Removing parentheses: Optional parentheses
#define SHT_PAREN_PROBE(...)
#define SHT_CDP_MACRO(x)						\
	SHT_IF_ELSE(							\
		SHT_EXPAND,						\
		SHT_DEPAREN,						\
		SHT_PAREN_PROBE x					\
	)
#define SHT_COND_DEPAREN(x)		SHT_CDP_MACRO(x)(x)

// Stringification
#define SHT_STR_NX(x)			#x
#define SHT_STR(x)			SHT_STR_NX(x)

// Type checking
#define SHT_CHECK_TYPE(expr, type, ...)					\
	static_assert(							\
		_Generic((expr),					\
			type: 1,					\
			default: 0					\
		)							\
		__VA_OPT__(,)						\
		__VA_ARGS__						\
	)

// Tuples: First required, second optional
#define SHT_STRIP_ARG1_NX(a1, ...)	__VA_ARGS__
#define SHT_STRIP_ARG1(...)		SHT_STRIP_ARG1_NX(__VA_ARGS__)
#define SHT_FRSO_REQ(t)			SHT_ARG1(SHT_COND_DEPAREN(t))
#define SHT_FRSO_OPT(t)			SHT_STRIP_ARG1(SHT_COND_DEPAREN(t))

// Tuples: First optional, second required
#define SHT_FOSR_OPT(t)							\
	SHT_IF_ELSE(							\
		SHT_ARG1(SHT_COND_DEPAREN(t)),				\
		SHT_STRIP_ARG1(SHT_COND_DEPAREN(t)),			\
		SHT_STRIP_ARG1(SHT_COND_DEPAREN(t))			\
	)

#define SHT_FOSR_REQ(t)							\
	SHT_IF_ELSE(							\
		SHT_STRIP_ARG1(SHT_COND_DEPAREN(t)),			\
		SHT_ARG1(SHT_COND_DEPAREN(t)), 				\
		SHT_STRIP_ARG1(SHT_COND_DEPAREN(t))			\
	)

// Unique identifiers
#define SHT_CONCAT_NX(a, b)		a##b
#define SHT_CONCAT(a, b)		SHT_CONCAT_NX(a, b)
#define SHT_MKID(base)			SHT_CONCAT(base, __LINE__)

#endif


/*******************************************************************************
 *
 *
 *	Callback function context arguments/types
 *
 *	Type-safe callback functions do not have a context argument if those
 *	arguments would be unused.  When generating a typedef for a pointer
 *	or a call to such a function, the context type or argument must be
 *	omitted in that case.
 *
 *
 ******************************************************************************/

/**
 * @internal
 * @brief
 * Expands to the context argument (if any) of a call to a wrapped function.
 *
 * If the context referent type argument is present, this macro expands to a
 * sequence of 2 tokens &mdash; a comma followed by the identifier `context`
 * (`, context`).  If the argument is not present, it expands to nothing.
 *
 * @param	...	Optional callback function context referent type.
 *
 */
#define SHT_CB_CTX_ARG(...)						\
	SHT_DEPAREN(							\
		SHT_IF_ELSE(						\
			(, context),					\
			(),						\
			__VA_ARGS__					\
		)							\
	)

/**
 * @internal
 * @brief
 * The type of the context argument (if any) in a wrapped function's signature.
 *
 * If the context referent type argument is present, this macro expands to a
 * token sequence that contains a comma followed by the type and `*restrict`.
 * For example, if the context type is `int`, this macro expands to
 * `, int *restrict`.  If the context referent type is not present, this macro
 * expands to nothing.
 *
 * @param	...	Optional callback function context referent type.
 *
 */
#define SHT_CB_CTX_TYPE(...)						\
	SHT_DEPAREN(							\
		SHT_IF_ELSE(						\
			(, __VA_ARGS__ *restrict),			\
			(),						\
			__VA_ARGS__					\
		)							\
	)


/*******************************************************************************
 *
 *
 *	Type-safe hash function wrapper
 *
 *
 ******************************************************************************/

/**
 * @internal
 * @brief
 * Creates a typedef for a pointer to the expected type-safe hash function.
 *
 * > **NOTE**
 * >
 * > The name of the generated typedef is always `ts_hashfn_t`.  Therefore, this
 * > macro should only be used **inside** a function.
 *
 * @param	ktype	Key referent type.
 * @param	...	Optional context referent type.
 *
 */
#define SHT_HASHFN_TD(ktype, ...)					\
	typedef uint32_t (*ts_hashfn_t)(				\
				const ktype *restrict			\
				SHT_CB_CTX_TYPE(__VA_ARGS__)		\
	)

/**
 * @internal
 * @brief
 * Error message string for incorrect type-safe hash function signature.
 *
 * @param	hashfn	Type-safe hash function name.
 * @param	ktype	Key referent type.
 * @param	...	Optional type-safe hash function context referent type.
 *
 */
#define SHT_HASHFN_ERR(hashfn, ktype, ...)				\
	SHT_STR(							\
		hashfn has incorrect signature; expected:		\
		uint32_t hashfn ( const ktype *restrict			\
				  SHT_CB_CTX_TYPE(__VA_ARGS__) )	\
	)

/**
 * @internal
 * @brief
 * Generates a wrapper for a type-safe hash function.
 *
 * The generated function definition accepts void pointers for both its @p key
 * and @p context arguments and calls the type-safe hash function identified by
 * @p hasnfn.  If the context referent type argument is not present, the
 * type-safe function is called with only the @p key argument.
 *
 * The generated function also includes a compile-time check of the type-safe
 * hash function's signature.
 *
 * @param	name	Wrapper function name.
 * @param	ktype	Key referent type.
 * @param	hashfn	The type-safe hash function to be wrapped.
 * @param	...	Optional context referent type.
 */
#define SHT_MKHASHFN(name, ktype, hashfn, ...)				\
	static uint32_t name(const void *restrict key,			\
			     void *restrict context)			\
	{								\
		/* typedef for wrapped function signature check */	\
		SHT_HASHFN_TD(ktype, __VA_ARGS__);			\
		/* Check that wrapped function matches typedef */	\
		SHT_CHECK_TYPE(						\
			hashfn,						\
			ts_hashfn_t,					\
			SHT_HASHFN_ERR(hashfn, ktype, __VA_ARGS__)	\
		);							\
		/* Suppress potential unused argument warning */	\
		(void)context;						\
		/* Call the type-safe function */			\
		return hashfn(key SHT_CB_CTX_ARG(__VA_ARGS__));		\
	}


/*******************************************************************************
 *
 *
 *	Type-safe equality function wrapper
 *
 *
 ******************************************************************************/

/**
 * @internal
 * @brief
 * Creates a typedef for a pointer to the expected type-safe equality function.
 *
 * > **NOTE**
 * >
 * > The name of the generated typedef is always `ts_eqfn_t`.  Therefore, this
 * > macro should only be used **inside** a function.
 *
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 * @param	...	Optional type-safe equality function context referent
 *			type.
 *
 */
#define SHT_EQFN_TD(ktype, etype, ...)					\
	typedef bool (*ts_eqfn_t)(					\
				const ktype *restrict,			\
				const etype *restrict			\
				SHT_CB_CTX_TYPE(__VA_ARGS__)		\
	)

/**
 * @internal
 * @brief
 * Error message string for incorrect type-safe equality function signature.
 *
 * @param	eqfn	Type-safe equality function name.
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 * @param	...	Optional type-safe equality function context referent
 *			type.
 *
 */
#define SHT_EQFN_ERR(eqfn, ktype, etype, ...)				\
	SHT_STR(							\
		eqfn has incorrect signature; expected:			\
		bool eqfn ( const ktype *restrict ,			\
			    const etype *restrict			\
			    SHT_CB_CTX_TYPE(__VA_ARGS__) )		\
	)

/**
 * @internal
 * @brief
 * Generates a wrapper for a type-safe equality function.
 *
 * The generated function definition accepts void pointers for its @p key,
 * @p entry, and @p context arguments and calls the type-safe equality function
 * identified by @p eqfn.  If the context referent type argument is not present,
 * the type-safe function is called with only the @p key and @p entry arguments.
 *
 * The generated function also includes a compile-time check of the type-safe
 * equality function's signature.
 *
 * @param	name	Wrapper function name.
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 * @param	eqfn	The type-safe equality function to be wrapped.
 * @param	...	Optional context referent type.
 */
#define SHT_MKEQFN(name, ktype, etype, eqfn, ...)			\
	static bool name(const void *restrict key,			\
			 const void *restrict entry,			\
			 void *restrict context)			\
	{								\
		/* typedef for wrapped function signature check */	\
		SHT_EQFN_TD(ktype, etype, __VA_ARGS__);			\
		/* Check that wrapped function matches typedef */	\
		SHT_CHECK_TYPE(						\
			eqfn,						\
			ts_eqfn_t,					\
			SHT_EQFN_ERR(eqfn, ktype, etype, __VA_ARGS__)	\
		);							\
		/* Suppress potential unused argument warning */	\
		(void)context;						\
		/* Call the type-safe function */			\
		return eqfn(key, entry SHT_CB_CTX_ARG(__VA_ARGS__));	\
	}


/*******************************************************************************
 *
 *
 *	Type-safe free function wrapper
 *
 *
 ******************************************************************************/

/**
 * @internal
 * @brief
 * Creates a typedef for a pointer to the expected type-safe free function.
 *
 * > **NOTE**
 * >
 * > The name of the generated typedef is always `ts_freefn_t`.  Therefore, this
 * > macro should only be used **inside** a function.
 *
 * @param	etype	Entry referent type.
 * @param	...	Optional type-safe free function context referent type.
 *
 */
#define SHT_FREEFN_TD(etype, ...)					\
	typedef void (*ts_freefn_t)(					\
				const etype *restrict			\
				SHT_CB_CTX_TYPE(__VA_ARGS__)		\
	)

/**
 * @internal
 * @brief
 * Error message string for incorrect type-safe free function signature.
 *
 * @param	freefn	Type-safe free function name.
 * @param	etype	Entry referent type.
 * @param	...	Optional type-safe free function context referent type.
 *
 */
#define SHT_FREEFN_ERR(freefn, etype, ...)				\
	SHT_STR(							\
		freefn has incorrect signature; expected:		\
		void freefn ( const etype *restrict			\
			      SHT_CB_CTX_TYPE(__VA_ARGS__) )		\
	)

/**
 * @internal
 * @brief
 * Generates a wrapper for a type-safe free function.
 *
 * The generated function definition accepts void pointers for both its @p entry
 * and @p context arguments and calls the type-safe free function identified by
 * @p freefn.  If the context referent type argument is not present, the
 * type-safe function is called with only the @p entry argument.
 *
 * The generated function also included a compile-time check of the type-safe
 * free function's signature.
 *
 * @param	name	Wrapper function name.
 * @param	etype	Entry referent type.
 * @param	freefn	The type-safe free function to be wrapped.
 * @param	...	Optional free function context type.
 */
#define SHT_MKFREEFN(name, etype, freefn, ...)				\
	static void name(const void *restrict entry,			\
			 void *restrict context)			\
	{								\
		/* typedef for wrapped function signature check	*/	\
		SHT_FREEFN_TD(etype, __VA_ARGS__);			\
		/* Check that wrapped function matches typedef */	\
		SHT_CHECK_TYPE(						\
			freefn,						\
			ts_freefn_t,					\
			SHT_FREEFN_ERR(freefn, etype, __VA_ARGS__)	\
		);							\
		/* Suppress potential unused argument warning */	\
		(void)context;						\
		/* Call the type-safe function */			\
		freefn(entry SHT_CB_CTX_ARG(__VA_ARGS__));		\
	}


/*******************************************************************************
 *
 *
 *	const stripping
 *
 *
 ******************************************************************************/

/**
 * Cast a `const`-qualified pointer to a non-`const`-qualified pointer.
 *
 * The callback function types (#sht_hashfn_t, #sht_eqfn_t, and #sht_freefn_t)
 * all accept a `context` argument that library users may use to provide
 * additional data to their callback functions (e.g., a hash function seed).
 * The library also defines a "setter" function for each context type &mdash;
 * sht_set_hash_ctx(), sht_set_eq_ctx(), and sht_set_free_ctx().  The `context`
 * argument to these functions is not `const`-qualified; this allows callback
 * functions to modify their context referents, if desired.
 *
 * In many cases, there is no requirement for callback function contexts to be
 * mutable, so the type-safe hash, equality, or free function can declare its
 * context argument to be a `const`-qualified pointer type.  The generated
 * wrapper function, which accepts a non-`const` context pointer in order to
 * conform to the library API, can simply call the type-safe function, because
 * **adding** a `const` qualifier to a pointer is always allowed.
 *
 * This case presents a problem for the generated setter functions.  They should
 * accept a `const`-qualified `context` argument and pass it to the library
 * setter function which expects a non-`const` pointer.  This can generate an
 * unwanted warning.
 *
 * This function exists to "cast away" the `const` qualifier from a pointer
 * without generating any warnings.  It should not generate any actual object
 * code when optimization is enabled.
 *
 * @param	p	A (possibly) `const`-qualified pointer.
 *
 * @returns	The value of @p p, as a non-`const`-qualified pointer.
 */
static inline void *sht_strip_const_(const void *p)
{
	_Pragma("GCC diagnostic push")
	_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")

	return (void *)p;

	_Pragma("GCC diagnostic pop")
}


/*******************************************************************************
 *
 *
 *	Other function wrappers
 *
 *
 ******************************************************************************/

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_new_().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	name	Wrapper function name.
 * @param	etype	Entry type.
 * @param	hashfn	Hash function wrapper name.
 * @param	eqfn	Equality function wrapper name.
 * @param	freefn	Free function wrapper name or `NULL`.
 * @param	...	Absorbs `NULL` argument , if free function exists.
 */
#define SHT_WRAP_NEW(sc, ttype, name, etype, hashfn, eqfn, freefn, ...)	\
	[[maybe_unused]]						\
	sc ttype *name(void)						\
	{								\
		return (ttype *)sht_new_(hashfn, eqfn, freefn,		\
					 sizeof(etype), alignof(etype),	\
					 NULL);				\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set_hash_ctx(), if applicable.
 *
 * This macro expands to nothing if the context referent type is empty.
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	...	Optional context referent type.
 */
#define SHT_WRAP_SET_HASH_CTX(sc, name, ttype, ...)			\
	__VA_OPT__(							\
		[[maybe_unused, gnu::nonnull(1)]]			\
		sc void name(ttype *ht, __VA_ARGS__ *context)		\
		{							\
			sht_set_hash_ctx((struct sht_ht *)ht,		\
					 sht_strip_const_(context));	\
		}							\
	)

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set_eq_ctx(), if applicable.
 *
 * This macro expands to nothing if the context referent type is empty.
 *
 * @param	sc	Storage clas (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	...	Optional context referent type.
 */
#define SHT_WRAP_SET_EQ_CTX(sc, name, ttype, ...)			\
	__VA_OPT__(							\
		[[maybe_unused, gnu::nonnull(1)]]			\
		sc void name(ttype *ht, __VA_ARGS__ *context)		\
		{							\
			sht_set_eq_ctx((struct sht_ht *)ht,		\
				       sht_strip_const_(context));	\
		}							\
	)

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set_free_ctx(), if applicable.
 *
 * This macro expands to nothing if the context referent type is empty.
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	...	Optional context referent type.
 */
#define SHT_WRAP_SET_FREE_CTX(sc, name, ttype, ...)			\
	__VA_OPT__(							\
		[[maybe_unused, gnu::nonnull(1)]]			\
		sc void name(ttype *ht, __VA_ARGS__ *context)		\
		{							\
			sht_set_free_ctx((struct sht_ht *)ht,		\
					 sht_strip_const_(context));	\
		}							\
	)

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set_lft().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_SET_LFT(sc, name, ttype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc void name(ttype *ht, uint8_t lft)				\
	{								\
		sht_set_lft((struct sht_ht *)ht, lft);			\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set_psl_limit().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_SET_PSL_LIMIT(sc, name, ttype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc void name(ttype *ht, uint8_t limit)				\
	{								\
		sht_set_psl_limit((struct sht_ht *)ht, limit);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_init().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_INIT(sc, name, ttype)					\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht, uint32_t capacity)			\
	{								\
		return sht_init((struct sht_ht *)ht, capacity);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_free().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_FREE(sc, name, ttype)					\
	[[maybe_unused, gnu::nonnull]]					\
	sc void name(ttype *ht)						\
	{								\
		sht_free((struct sht_ht *)ht);				\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_add().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_ADD(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc int name(ttype *ht, const ktype *key, const etype *entry)	\
	{								\
		return sht_add((struct sht_ht *)ht, key, entry);	\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_set().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_SET(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc int name(ttype *ht, const ktype *key, const etype *entry)	\
	{								\
		return sht_set((struct sht_ht *)ht, key, entry);	\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_get().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_GET(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc const etype *name(ttype *ht, const ktype *key)		\
	{								\
		return sht_get((struct sht_ht *)ht, key);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_size().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_SIZE(sc, name, ttype)					\
	[[maybe_unused, gnu::nonnull]]					\
	sc uint32_t name(ttype *ht)					\
	{								\
		return sht_size((struct sht_ht *)ht);			\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_empty().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_EMPTY(sc, name, ttype)					\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht)						\
	{								\
		return sht_empty((struct sht_ht *)ht);			\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_delete().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 */
#define SHT_WRAP_DELETE(sc, name, ttype, ktype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht, const ktype *key)			\
	{								\
		return sht_delete((struct sht_ht *)ht, key);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_pop().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_POP(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht, const ktype *restrict key,		\
		     etype *restrict out)				\
	{								\
		return sht_pop((struct sht_ht *)ht, key, out);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_replace().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_REPLACE(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht, const ktype *key, const etype *entry)	\
	{								\
		return sht_replace((struct sht_ht *)ht, key, entry);	\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_swap().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	ktype	Key referent type.
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_SWAP(sc, name, ttype, ktype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(ttype *ht, const ktype *key,			\
		     const etype *entry, etype *out)			\
	{								\
		return sht_swap((struct sht_ht *)ht, key, entry, out);	\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_new().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 * @param	itype	Type-safe iterator type (incomplete).
 */
#define SHT_WRAP_ITER_NEW(sc, name, ttype, itype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc itype *name(ttype *ht, enum sht_iter_type type)		\
	{								\
		return (itype *)sht_iter_new((struct sht_ht *)ht,	\
					     type);			\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_free().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 */
#define SHT_WRAP_ITER_FREE(sc, name, itype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc void name(itype *iter)					\
	{								\
		sht_iter_free((struct sht_iter *)iter);			\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_next().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_ITER_NEXT(sc, name, itype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc const etype *name(itype *iter)				\
	{								\
		return sht_iter_next((struct sht_iter *)iter);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_delete().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 */
#define SHT_WRAP_ITER_DELETE(sc, name, itype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(itype *iter)					\
	{								\
		return sht_iter_delete((struct sht_iter *)iter);	\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_replace().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 * @param	etype	Entry referent type.
 */
#define SHT_WRAP_ITER_REPLACE(sc, name, itype, etype)			\
	[[maybe_unused, gnu::nonnull]]					\
	sc bool name(itype *iter, const etype *entry)			\
	{								\
		return sht_iter_replace((struct sht_iter *)iter,	\
					entry);				\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_get_err().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_GET_ERR(sc, name, ttype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc enum sht_err name(ttype *ht)					\
	{								\
		return sht_get_err((struct sht_ht *)ht);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_get_msg().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	ttype	Type-safe table type (incomplete).
 */
#define SHT_WRAP_GET_MSG(sc, name, ttype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc const char *name(ttype *ht)					\
	{								\
		return sht_get_msg((struct sht_ht *)ht);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_err().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 */
#define SHT_WRAP_ITER_ERR(sc, name, itype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc enum sht_err name(itype *iter)				\
	{								\
		return sht_iter_err((struct sht_iter *)iter);		\
	}

/**
 * @internal
 * @brief
 * Generate a type-safe wrapper for sht_iter_msg().
 *
 * @param	sc	Storage class (e.g., `static`).  May be empty.
 * @param	name	Wrapper function name.
 * @param	itype	Type-safe iterator type (incomplete).
 */
#define SHT_WRAP_ITER_MSG(sc, name, itype)				\
	[[maybe_unused, gnu::nonnull]]					\
	sc  const char *name(itype *iter)				\
	{								\
		return sht_iter_msg((struct sht_iter *)iter);		\
	}


/*******************************************************************************
 *
 *
 *	Table type helpers
 *
 *
 ******************************************************************************/


/*
 *
 * Function names
 *
 */

/**
 * @internal
 * @brief
 * Generate a function name.
 *
 * @param	ttspec	Table type spec.
 * @param	base	Function name base.
 */
#define SHT_FN_NAME(ttspec, base)	SHT_CONCAT(SHT_FOSR_REQ(ttspec), base)

/**
 * @internal
 * @brief
 * Generate a hash function wrapper name.
 *
 * @param	ttspec	Table type spec.
 */
#define SHT_HF_NAME(ttspec)		SHT_FN_NAME(ttspec, _hash_wrapper_)

/**
 * @internal
 * @brief
 * Generate an equalify function wrapper name.
 *
 * @param	ttspec	Table type spec.
 */
#define SHT_EF_NAME(ttspec)		SHT_FN_NAME(ttspec, _eq_wrapper_)

/**
 * @internal
 * @brief
 * Generate a free function wrapper name.
 *
 * @param	ttspec	Table type spec.
 */
#define SHT_FF_NAME(ttspec)		SHT_FN_NAME(ttspec, _free_wrapper_)


/*
 *
 * Type names
 *
 */

/**
 * @internal
 * @brief
 * Generate a type-safe table type name.
 *
 * @param	ttspec Table type spec.
 */
#define SHT_HT_T(ttspec)		\
	struct SHT_CONCAT(SHT_FOSR_REQ(ttspec), _ht)

/**
 * @internal
 * @brief
 * Generate a type-safe iterator type name.
 *
 * @param	ttspec	Table type spec.
 */
#define SHT_ITER_T(ttspec)		\
	struct SHT_CONCAT(SHT_FOSR_REQ(ttspec), _iter)


/*
 *
 * Function storage class
 *
 * (Callback functions are always static.)
 *
 */

/**
 * @internal
 * @brief
 * Expands to nothing.
 */
#define SHT_FN_SC_PROBE_extern

/**
 * @internal
 * @brief
 * Expands to nothing if (pre-expanded) @p sc is `extern`, otherwise @p sc.
 *
 * > **NOTE**
 * >
 * > This macro relies on the fact that `SHT_FN_SC_PROBE_##sc` is not defined
 * > for any value of @p sc other than `extern`.
 *
 * @param	sc	The expanded storage class from the table type spec,
 *			with an empty value replaced by `static`.
 */
#define SHT_EXTERN_2_EMPTY_NX(sc)	SHT_IF_ELSE(sc, , SHT_FN_SC_PROBE_##sc)

/**
 * @internal
 * @brief
 * Forces expansion of @p sc before invoking SHT_EXTERN_2_EMPTY_NX().
 *
 * @param	sc 	The unexpanded storage class from the table type spec,
 *			with an empty value replaced by `static`.
 */
#define SHT_EXTERN_2_EMPTY(sc)		SHT_EXTERN_2_EMPTY_NX(sc)

/**
 * @internal
 * @brief
 * Expands to `static` if @p sc is empty; otherwise expands to @p sc.
 *
 * @param	sc	The storage class from the table type spec, which may be
 *			empty.
 */
#define SHT_EMPTY_2_STATIC(sc)		SHT_IF_ELSE(sc, static, sc)

/**
 * @internal
 * @brief
 * Generates the storage class for a generated function.
 *
 * Callback function wrappers are always `static`.  Other generated functions
 * default to `static`, if no storage class is specified in the table type spec.
 * but this can be changed by specifying a storage class of `extern`, which will
 * suppress the default `static`.  Any other storage class will be applied to
 * the generated functions unchanged.
 *
 * In summary:
 *
 * * (Nothing) → `static`
 * * `extern` → (nothing)
 * * (Anything else) → (unchanged)
 */
#define SHT_FN_SC(ttspec)						\
	SHT_EXTERN_2_EMPTY(						\
		SHT_EMPTY_2_STATIC(					\
			SHT_FOSR_OPT(ttspec)				\
		)							\
	)



/*******************************************************************************
 *
 *
 *	Table type macro
 *
 *
 ******************************************************************************/

/**
 * Generate types and functions for a type-safe hash table type.
 *
 * @param	ttspec	Table type spec &mdash; a 1- or 2-member tuple that
 *			contains an optional storage class (e.g., `static`)
 *			followed by a required name prefix.  If a storage class
 *			is specified, the tuple must be enclosed in parentheses.
 *			Thus, the following formats are allowed.
 *			* `prefix`
 *			* `(prefix)`
 *			* `(, prefix)`
 *			* `(storage_class, prefix)`
 *
 * @param	ktype	The type of the table's keys.  For example, if the
 *			type-safe hash and equality functions accept a
 *			`const char *` as their `key` argument, then @p ktype
 *			should be `char`.
 *
 * @param	etype	The type of the table's entries.  If the type-safe
 *			equality and free functions (if any) accept a
 *			`const struct foo_entry *`, then @p etype should be
 *			`struct foo_entry`.
 *
 * @param	hfspec	Hash function spec &mdash; a 1- or 2-member tuple that
 *			contains a required type-safe hash function name
 *			followed by an optional hash function context type.  For
 *			example, if the type-safe hash function accepts a
 *			`const uint32_t *` as its `context` argument, then the
 *			context type should be `const uint32_t`.  (Context
 *			pointers may be either `const`-qualified or non-`const`,
 *			depending on the needs of the application.)  If a
 *			context type is specified, the tuple must be enclosed in
 *			parentheses.  Thus, the following formats are allowed.
 *			* `function_name`
 *			* `(function_name)`
 *			* `(function_name, )`
 *			* `(function_name, context_type)`
 *
 * @param	efspec	Equality function spec &mdash; a 1- or 2-member tuple
 *			that contains a required type-safe equality function
 *			name followed by an optional context type. (See
 *			@p hfspec for the allowed formats.)
 *
 * @param	...	**Optional** free function spec &mdash; a 1- or 2-member
 *			tuple that contains a required (if the spec if present)
 *			type-safe free function name followed by an optional
 *			context type.  (See @p hfspec for the allowed formats.)
 */
#define SHT_TABLE_TYPE(ttspec, ktype, etype, hfspec, efspec, ...)	\
									\
	/* Entry size check */						\
	static_assert(sizeof(etype) <= SHT_MAX_ESIZE,			\
		      SHT_STR(Entry type (etype) too large));		\
									\
	/* Incomplete type that represents a table */			\
	SHT_HT_T(ttspec);						\
									\
	/* Incomplete type that represents an iterator */		\
	SHT_ITER_T(ttspec);						\
									\
	/* Hash function wrapper */					\
	SHT_MKHASHFN(							\
		SHT_HF_NAME(ttspec),			/* name */	\
		ktype,					/* ktype */	\
		SHT_FRSO_REQ(hfspec),			/* hashfn */	\
		SHT_FRSO_OPT(hfspec)			/* ...? */	\
	)								\
									\
	/* Equality function wrapper */					\
	SHT_MKEQFN(							\
		SHT_EF_NAME(ttspec),			/* name */	\
		ktype,					/* ktype */	\
		etype,					/* etype */	\
		SHT_FRSO_REQ(efspec),			/* eqfn */	\
		SHT_FRSO_OPT(efspec)			/* ...? */	\
	)								\
									\
	/* Free function wrapper, if necessary */			\
	__VA_OPT__(							\
		SHT_MKFREEFN(						\
			SHT_FF_NAME(ttspec),		/* name */	\
			etype,				/* etype */	\
			SHT_FRSO_REQ(__VA_ARGS__),	/* freefn */	\
			SHT_FRSO_OPT(__VA_ARGS__)	/* ...? */	\
		)							\
	)								\
									\
	/* sht_new_() wrapper */					\
	SHT_WRAP_NEW(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		SHT_FN_NAME(ttspec, _new),		/* name */	\
		etype,					/* etype */	\
		SHT_HF_NAME(ttspec),			/* hashfn */	\
		SHT_EF_NAME(ttspec),			/* eqfn */	\
		__VA_OPT__(SHT_FF_NAME(ttspec),)	/* freefn? */	\
		NULL					/* freefn? */	\
	)								\
									\
	/* sht_set_hash_ctx() wrapper */				\
	SHT_WRAP_SET_HASH_CTX(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _set_hash_ctx),	/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		SHT_FRSO_OPT(hfspec)			/* ...? */	\
	)								\
									\
	/* sht_set_eq_ctx() wrapper */					\
	SHT_WRAP_SET_EQ_CTX(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _set_eq_ctx),	/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		SHT_FRSO_OPT(efspec)			/* ...? */	\
	)								\
									\
	/* sht_set_free_ctx() wrapper, if necessary */			\
	__VA_OPT__(							\
		SHT_WRAP_SET_FREE_CTX(					\
			SHT_FN_SC(ttspec),		/* sc */	\
			SHT_FN_NAME(ttspec,		/* name */	\
				    _set_free_ctx),			\
			SHT_HT_T(ttspec),		/* ttype */	\
			SHT_FRSO_OPT(__VA_ARGS__)	/* ...? */	\
		)							\
	)								\
									\
	/* sht_set_lft() wrapper */					\
	SHT_WRAP_SET_LFT(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _set_lft),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_set_psl_limit() wrapper */				\
	SHT_WRAP_SET_PSL_LIMIT(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _set_psl_limit),	/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_init() wrapper */					\
	SHT_WRAP_INIT(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _init),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_free() wrapper */					\
	SHT_WRAP_FREE(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _free),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_add() wrapper */						\
	SHT_WRAP_ADD(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _add),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_set() wrapper */						\
	SHT_WRAP_SET(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _set),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_get() wrapper */						\
	SHT_WRAP_GET(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _get),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_size() wrapper */					\
	SHT_WRAP_SIZE(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _size),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_empty() wrapper */					\
	SHT_WRAP_EMPTY(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _empty),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_delete() wrapper */					\
	SHT_WRAP_DELETE(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _delete),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype					/* ktype */	\
	)								\
									\
	/* sht_pop() wrapper */						\
	SHT_WRAP_POP(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _pop),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_replace() wrapper */					\
	SHT_WRAP_REPLACE(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _replace),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_swap() wrapper */					\
	SHT_WRAP_SWAP(							\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _swap),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		ktype,					/* ktype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_iter_new() wrapper */					\
	SHT_WRAP_ITER_NEW(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_new),		/* name */	\
		SHT_HT_T(ttspec),			/* ttype */	\
		SHT_ITER_T(ttspec)			/* itype */	\
	)								\
									\
	/* sht_iter_free() wrapper */					\
	SHT_WRAP_ITER_FREE(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_free),	/* name */	\
		SHT_ITER_T(ttspec)			/* itype */	\
	)								\
									\
	/* sht_iter_next() wrapper */					\
	SHT_WRAP_ITER_NEXT(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_next),	/* name */	\
		SHT_ITER_T(ttspec),			/* itype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_iter_delete() wrapper */					\
	SHT_WRAP_ITER_DELETE(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_delete),	/* name */	\
		SHT_ITER_T(ttspec)			/* itype */	\
	)								\
									\
	/* sht_iter_replace() wrapper */				\
	SHT_WRAP_ITER_REPLACE(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_replace),	/* name */	\
		SHT_ITER_T(ttspec),			/* itype */	\
		etype					/* etype */	\
	)								\
									\
	/* sht_get_err() wrapper */					\
	SHT_WRAP_GET_ERR(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _get_err),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_get_msg() wrapper */					\
	SHT_WRAP_GET_MSG(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _get_msg),		/* name */	\
		SHT_HT_T(ttspec)			/* ttype */	\
	)								\
									\
	/* sht_iter_err() wrapper */					\
	SHT_WRAP_ITER_ERR(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_err),		/* name */	\
		SHT_ITER_T(ttspec)			/* itype */	\
	)								\
									\
	/* sht_iter_msg() wrapper */					\
	SHT_WRAP_ITER_MSG(						\
		SHT_FN_SC(ttspec),			/* sc */	\
		SHT_FN_NAME(ttspec, _iter_msg),		/* name */	\
		SHT_ITER_T(ttspec)			/* itype */	\
	)


#endif		// SHT_TS_H
