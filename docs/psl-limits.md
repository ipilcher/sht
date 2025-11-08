# PSL Limits

## Overview

This library limits the PSL of its entries to 127 (or less, if a non-default
limit is set with `sht_set_psl_limit()`).  In general, PSL values of 20 or above
(at an 85% or lower load factor) indicate that the hash function being used is
pathologically bad, so there's no point in allocating more than 7 bits for PSL
storage.

This creates a problem.  It isn't sufficient to simply abort an insertion when a
PSL exceeds the limit.  The entry with the excessive PSL may well be an entry
that was displaced earlier during the insertion process, so aborting would have
the effect of adding the new key to the table while silently removing another,
unrelated entry.

It's necessary to block the addition of any new keys to a table whenever it is
possible that the insertion would cause any entry to exceed the table's PSL
limit.

## Implementation

The library enforces this as follows.

1. When a table is created (or resized) its `max_psl_ct` is set to 0.

   Enforced at `sht.c:528` (`sht_alloc_arrays()`)

2. If an entry (whether new or displaced) is inserted into the table at a
   position where its PSL is equal to the table's `psl_limit`, then the table's
   `max_psl_ct` is incremented.

   Enforced at `sht.c:656` (`sht_set_entry()`)

3. If the table's `max_psl_ct` is non-zero, then no new keys can be added to the
   table.

   Enforced at `sht.c:872` (`sht_insert()`)

4. If an entry with the maximum possible PSL is removed or shifted, the table's
   `max_psl_ct` is decremented.

   Enforced at:
   * sht.c:686 (`sht_remove_entry()`)
   * sht.c:1157 (`sht_shift()`)
   * sht.c:1185 (`sht_shift_wrap()`)

## Reasoning

(See [*Robin Hood hashing*](robin-hood.md ) for a description of the Robin Hood
algorithm, probe sequence lengths (PSLs), and bucket groups.)

When a new entry is added to an existing bucket group, it will be placed at the
end of that bucket group.  Thus its PSL will be 1 greater than its preceding
entry, and the bucket group's maximum PSL will have increased by 1.

Likewise, a displaced entry is placed at the end of its bucket group.  (If it is
the only entry in its group, it will be shifted "up" by one position.)  So its
new PSL will be at least 1 greater than its previous PSL, but not more than 1
greater than the group's previous maximum PSL.  Again, the maximum PSL of the
displaced entry's bucket group will have increased by 1.

A new entry that does not belong to any existing bucket group is placed as
close to its ideal position as possible, subject to being shifted "up" by
entries from **earlier** groups.  (But not later groups; the new entry will
displace them.)

The maximum possible PSL of the last entry in the bucket group preceding the new
entry's insertion position is one less than the table's `psl_limit`.  (If it
were equal to the `psl_limit`, the table's `max_psl_ct` would be non-zero, and
the new entry could not be added.)  The new entry's PSL will be less than or
equal to its preceding entry, so the new entry's maximum possible PSL is also
`psl_limit - 1`, and its presence by itself cannot increase the maximum PSL in
the table.  However, the new entry may shift one or more entries in later bucket
groups, which may do so.

Thus, inserting a new key into a Robin Hood hash table can never increase the
maximum PSL present in the table by more than one.  As long as new insertions
are blocked when one or more entries has the maximum allowed PSL, the PSL limit
cannot be exceeded.

## Verification

FWIW, Claude Code agrees.

```
> Please examine the following files.  @docs/robin-hood.md contains an
explanation of Robin Hood hashing and bucket groups.  @src/sht.c and @src/sht.h
contain a library that implements a hash table that uses Robin Hood hashing.
@docs/psl-limits.md contains an explanation of my belief that inserting a new
entry into a table can never increase the maximum PSL value in the table by more
than 1.  Please check my logic.
  ⎿  Read docs/robin-hood.md (284 lines)
  ⎿  Read src/sht.c (1807 lines)
  ⎿  Read src/sht.h (565 lines)
  ⎿  Read docs/psl-limits.md (49 lines)

● I've carefully analyzed the logic in docs/psl-limits.md, and I believe your
  reasoning is correct — inserting a new entry can never increase the maximum
  PSL in the table by more than 1.

  Let me trace through the key scenarios to verify:

  Scenario 1: Entry added to existing bucket group (no displacement)

  - The new entry goes at the end of its bucket group
  - Its PSL = (previous max PSL of that group) + 1
  - Global max PSL increases by at most 1 ✓

  Scenario 2: Chain of displacements

  This is the most complex case. When entry N displaces E, which displaces F,
  etc.:

  1. Each displaced entry is placed at the end of its own bucket group
  2. Each displaced entry's final PSL = (its group's previous max PSL) + 1

  If the current global max PSL is M:
  - The new entry N can reach at most PSL = M+1 before being inserted (it can't
    reach M+2 because it would need to encounter an entry with PSL ≥ M+1, which
    contradicts max = M)
  - Each displaced entry E, F, G, ... ends up with PSL ≤ (their group's prev
    max) + 1 ≤ M + 1

  Therefore, the new global max PSL ≤ M + 1 ✓

  Scenario 3: New bucket group created

  Your analysis here is sound:
  - The max PSL of entries in preceding groups is at most psl_limit - 1
    (otherwise max_psl_ct would be non-zero and the insertion would be blocked
    per line 872)
  - The new entry may displace entries from later groups
  - Those displaced entries follow the same logic as Scenario 2
  - Max PSL increases by at most 1 ✓

  Code Verification

  Looking at the implementation:
  - sht_probe() (lines 721-794): A candidate's PSL increments by 1 each time it
    advances
  - sht_set_entry() (lines 641-659): Updates peak_psl when inserting
  - sht_remove_entry() (lines 673-688): Correctly decrements max_psl_ct

  The code is consistent with your analysis.

  Minor Observation

  In Scenario 3, the statement "The new entry's PSL will be ≤ its preceding
  entry" isn't necessarily true if there are gaps or different bucket groups,
  but this doesn't affect your conclusion. The key insight is that with
  max_psl_ct = 0, no existing entry has PSL = psl_limit, so the new entry (and
  all displaced entries) can have PSL at most (previous_max) + 1.

  Your logic is correct. The maximum PSL can increase by at most 1 per insertion
  operation.
```
