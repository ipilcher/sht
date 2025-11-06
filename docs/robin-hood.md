# Robin Hood hashing

This library implements a "Robin Hood" hash table &mdash; an open-addressing
hash table that uses a variation of linear probing to minimize the variance in
the number of positions that must be examined in order to find a key or to
determine that the key is not present in the table.

## Probe Sequence Length

Probe sequence length (PSL) is a core concept of Robin Hood hashing.

In an open-addressing hash table, the PSL of an entry is the number of probes &mdash;
in addition to the initial probe of its ideal position &mdash; required to locate an
entry in the table.  For example, an entry with a hash value of `0x81f62af3`,
stored in a hash table with 16 buckets, has an ideal position of 3
(`0x81f62af3 % 16 = 3)`.  At that position, its PSL is 0, because no additional
probes are required to locate it in the table.  If it were instead stored at
position 7, it would have a PSL of 4, because because 4 additional probes would
be required to locate it (3 → 4 → 5 → 6 → 7).

PSL can also be thought of as the distance between an entry's actual and ideal
positions, but the wrap-around behavior of hash tables must be accounted for.
Consider an entry with a hash value of `0x49a338ff` stored at position 1 in a
table with 16 buckets.  The entry's ideal position is 15, and its PSL is 2
(15 → 0 → 1).  Simple subtraction will not yield the correct result
(`1 - 15 = -14`).  To account for potential wrap-around, the following formula
can be used.

*PSL = (actual_position - ideal_position + table_size) mod table_size*

This formula can also  be used to calculate a **hypothetical**
PSL value for any key (whether present in the table or not) at
any position.  I.e., what would the PSL of this key be if it were stored
at that position.

*PSL_h = (hypothetical_position - ideal_position + table_size) mod table_size*

The Robin Hood insertion and search algorithms depend heavily on PSLs (actual
and hypothetical), although they are calculated step-by-step instead of using
the formulas above.

## Insertion algorithm

The insertion algorithm operates as follows.

1. The ideal position of the key to be inserted (the "candidate key") is
   computed, and the search for a suitable storage position begins there.  At
   its ideal position, the candidate's (hypothetical) PSL is 0.

2. If the position being examined (the current position) is empty, store the
   candidate at that position and stop.

3. If the PSL of the entry at the current position is equal to the
   candidate's PSL, check whether the entry's key is equal to the candidate
   key.

   **This check can be skipped if it is known that the candidate key is not
   present in the table.**

   If the keys are equal, then the candidate key is already present in the
   hash table at the current position.  Update the value associated with the
   key (if desired) and stop.

4. If the PSL of the entry at the current position is less than the candidate's
   PSL, the candidate is stored at the current position, displacing its current
   occupant.  The term "Robin Hood" hashing derives from the fact that the
   poorer (farther from its ideal position) candidate "steals" the position from
   the richer (closer to its ideal position) occupant.

   1. The occupant of the current position is copied to a temporary location.
   2. The candidate is copied to the current position.
   3. The former occupant of the current position (in its temporary location)
      becomes the new candidate.
   4. The new candidate PSL value is simply the PSL value of the (newly
      displaced) candidate at the current position.  (It is now hypothetical
      rather than actual.)

   Continue with the next step, but skip the check for duplicate keys (step
   3) for the remainder of the insertion process.

5. Advance to the next position in the table, incrementing the candidate PSL.
   (If the current position is the last position in the table, advance to the
   first position.)

6. Return to step 2.

> **NOTE**
>
> Once the ideal position of a key has been determined, the Robin Hood algorithm
> no longer relies on absolute positions of entries.  Instead it advances one
> position at a time &mdash; wrapping around as needed &mdash; while considering
> only the PSLs of the current candidate and the entries that it encounters.
>
> It effectively treats the table as a circular buffer, where the PSLs represent
> differences in relative positions.

## Bucket groups

The algorithm has the effect of grouping entries with equal ideal positions
together.  These groupings are called "bucket groups," and each bucket group
within a table is defined by the common ideal position of its members.

Consider this table (which has a very high load factor in order to better
illustrate the bucket group concept).

|Position|   Hash   |Ideal position|PSL|  Key |
|:------:|:--------:|:------------:|:-:|------|
|    0   |`6bf0ba1c`|      12      | 4 |Maria |
|    1   |`f5940e9f`|      15      | 2 |Ross  |
|    2   |`4837b98f`|      15      | 3 |Steve |
|    3   |`5e4138f0`|       0      | 3 |Alice |
|    4   |`0a240e30`|       0      | 4 |Alvaro|
|    5   |`d5718291`|       1      | 4 |Bob   |
|    6   |`77924041`|       1      | 5 |Ian   |
|    7   |`81f62af3`|       3      | 4 |Karen |
|    8   |          |              |   |      |
|    9   |`1111f939`|       9      | 0 |Monica|
|   10   |`9f98979a`|      10      | 0 |Susan |
|   11   |`0ef1713b`|      11      | 0 |Phoebe|
|   12   |`01d0f9eb`|      11      | 1 |Joey  |
|   13   |`8dfaf8ec`|      12      | 1 |Paul  |
|   14   |`e15086ec`|      12      | 2 |Frank |
|   15   |`75bb7c3c`|      12      | 3 |Rachel|

This table contains 8 bucket groups.

* Group 0 &mdash; Alice and Alvaro,
* Group 1 &mdash; Bob and Ian,
* Group 3 &mdash; Karen,
* Group 9 &mdash; Monica,
* Group 10 &mdash; Susan,
* Group 11 &mdash; Phoebe and Joey,
* Group 12 &mdash; Paul, Frank, Rachel, and Maria, and
* Group 15 &mdash; Ross and Steve.

Note that many of the groups have been shifted, so that even the first member of
those groups is not at its ideal position.

* Group 12 has been pushed to position 13, to make room the second member of
  group 11.
* Group 12 also has 4 members, pushing group 15 all the way to position 1.
* Groups 0, 1, and 3 have been pushed to positions 3, 5, and 7.

Despite the shifts, the bucket groups are still ordered within the table.

* Begin at the group with the lowest ideal position, group 0 at position 3.
* Group 1 begins at position 5, and group 3 begins at position 7.
* Groups 9, 10, and 11 all begin at their respective ideal positions.
* Group 12 begins at position 13.
* Group 15 begins at position 1 (which is after position 13 when advancing
  through the table and wrapping around).

A number of patterns can be observed in the PSLs of the group members.

* Every entry is stored as closely as possible to its ideal position (in the
  forward direction).  Entries are only shifted away from their ideal position
  in order to accomodate entries from earlier bucket groups or other entries
  from their own group.

* For entries within the same bucket group, the PSL of each member after the
  first is 1 greater than the PSL of the preceding member.  This reflects the
  fact that each successive member is 1 position farther away from its ideal
  position, so 1 more probe is required in order to locate it.

* If the PSL does not increase by 1 when advancing from one occupied position to
  another, it indicates that the entry at the new position belongs to a later
  bucket group (a group with a later ideal position) than the entry in the
  preceding position.

* The PSL cannot increase by more than one when advancing.  If it did, it would
  mean that the entry in the later position was a member of an earlier bucket
  group.

## Insertion revisited

The Robin Hood insertion algorithm described above ensures that this bucket
group structure is maintained.

Insertion always starts at an entry's ideal position and proceeds forward from
there.  Thus, entries are always inserted at or after their ideal position.
(An entry cannot have a negative PSL.)

When the algorithm considers the current position, there are 5 possibilities
(or 4 possibilities, one of which has 2 options).

1. The position is unoccupied.  The candidate will be stored at the current
   position.

2. The occupant's PSL is greater than the candidate's PSL (at that position).
   The occupant's larger PSL indicates that it belongs to an earlier bucket
   group than the candidate.

   The algorithm will skip the current position and advance to the next
   position.  The candidate is pushed away from its ideal position in order to
   maintain the continuity of the earlier bucket group.

3. The occupant's PSL is equal to that of the candidate.  The equal PSLs
   indicate that the candidate and the occupant are members of the same bucket
   group.

   1. If the keys of the candidate and the occupant are also equal, then the key
      was already stored in the table at the current position.  The value
      associated with the key may or may not be updated; either way, the
      structure of the table is unaffected.

   2. If the keys of the candidate and the occupant are not equal, the algorithm
      will skip the current position and advance to the next position.

      (This behavior is not strictly required.  Entries within the same bucket
      group could displace one another without affecting the continuity of the
      group.  This would result in all existing members of the group being
      shifted by one position.  Instead, the algorithm skips over the existing
      members of the group until it finds either an empty position or the
      beginning of a later group.)

4. The occupant's PSL is less than the candidate's PSL.  This indicates that
   the occupant belongs to a later bucket group than the candidate.

   The candidate will be stored at the current position, and the current
   occupant will be displaced, becoming the new candidate.  The result is that
   the former candidate has been stored at the end of its bucket group.  The
   displaced former occupant will eventually be stored at the end of its own
   group.

Thus, a candidate is always stored at the end of its bucket group (which may
also be the beginning), displacing the first member of the next bucket group if
necessary.

> **NOTE**
>
> As described, the insertion algorithm will loop forever if the table is
> completely full and the key to be inserted is not a duplicate.  It will keep
> wrapping around, searching for an empty bucket in which to store either the
> new entry or a displaced entry.
>
> In practice, the table should never be allowed to become completely full.
> (I.e., its load factor threshold should never be set to 100%.)

## Searching

Searching for an existing key is performed just like any other linear probing
hash table, except that the search can end as soon as it encounters an entry
from a later bucket group than that of the key for which it is searching.

In other words:

1. Start at the ideal position of the subject key.  The key's hypothetical PSL
   is 0 at this position.

2. If the current position is empty, stop.  The key is not present in the table.

3. If the PSL of the entry at the current position is greater than the key's
   PSL, advance to the next position, incrementing the key's PSL.  (The entry
   belongs to an earlier bucket group than the subject key.)

4. If the PSL of the entry at the current position is equal to the key's PSL,
   check whether the entry's key is equal to the subject key.

   1. If the keys are equal, stop.  The current entry is the result of the
      search.
   2. If the keys are not equal, advance to the next position, incrementing the
      key's PSL.

5. If the PSL of the entry at the current position is less than the key's PSL,
   stop.  The key is not present in the table.  (The entry belongs to a later
   bucket group than the subject key.  If the subject key were present in the
   table, the search would have already found it.)

## Deletion

The deletion algorithm is straightforward.  After an entry is removed from the
table, it may be necessary to shift some entries "down" by one position in
order to maintain the bucket group structure.

1. Starting at the first position after the position of the deleted entry, scan
   forward (wrapping around) until a position that should not be moved is found.
   This is the first position that is either empty or contains an entry with a
   PSL of zero.

2. Shift the entries that need to be moved (if any), accounting for wrap-around.

3. Adjust the PSLs of the entries that were moved; each of them is now 1
   position closer to its ideal position.
