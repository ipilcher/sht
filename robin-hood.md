# Robin Hood hashing

This library implements a "Robin Hood" hash table &mdash; an open-addressing
hash table that uses a variation of linear probing to minimize the variance in
the number of positions that must be examined in order to find a key or to
determine that the key is not present in the table.

## Probe Sequence Length

Probe sequence length (PSL) is a core concept of Robin Hood hashing.

The probe sequence length of an entry stored in the table is the distance
between the entry's ideal position in the table and its actual position.  For
example, consider an entry whose hash value is `0x81f62af3`.  If that entry is
stored in a hash table with 16 buckets, its ideal position is 3, where it would
have a PSL of 0.  If it is actually stored at position 5, because of hashing
collisions¹, its PSL is 2.

The PSL concept is also applied when searching the table &mdash; either to
retrieve the value associated with a key or to find a position at which  a new
key can be added to the table.  The hypothetical PSL of the key is 0 when its
ideal position is being checked, and it is incremented each time that the search
algorithm advances to the next position.  Thus, when considering a particular
position within the table, the hypothetical PSL of the key is equal to the
(possibly hypothetical) PSL of an entry storing that key at that position.

## Insertion algorithm

The insertion algorithm operates as follows.

1. The ideal position of the key to be inserted (the "candidate key") is
   computed, and the search for a position at which the key can be stored
   begins at that position.  At its ideal position, the candidate's PSL is 0.

2. If the position being examined (the current position) is empty, store the
   key at that position and stop.

3. If the PSL of the entry at the current position is equal to the
   candidate's PSL, check whether the entry's key is equal to the candidate's
   key.

   This check can be skipped if it is known that the candidate key is not
   present in the table.

   If the keys are equal, then the candidate key is already present in the
   hash table at the current position.  Update the value associated with the
   key (if desired) and stop.

4. If the PSL of the entry at the current position is less than the candidate's
   PSL, the candidate is stored at the current position, displacing its current
   occupant.  The term "Robin Hood" hashing derives from the fact that the
   poorer (farther from its ideal position) candidate is said to "steal" the
   position for the richer (closer to its ideal position) occupant.

   1. The occupant of the current position is copied to a temporary location.
   2. The candidate is copied to the current position.
   3. The former occupant of the current position (in its temporary location)
      becomes the new candidate.
   4. The new candidate PSL value is simply the PSL value that the (newly
      displaced) candidate had when it was stored at the current position.

   Continue with the next step, but skip the check for duplicate keys (step
   3) until the insertion completes.

5. Advance to the next position in the table and increment the candidate PSL.

6. Return to step 2.

## Invariants

### Notation

This analysis of the behavior of Robin Hood hash table will use the following
notation.

| Symbol|                     Meaning                         |
|:-----:|-----------------------------------------------------|
|   *c* |The candidate (a new or displaced entry)             |
|   *p* |The current position of the search                   |
|   *o* |The occupant of the current position                 |
| *h(x)*|The hash value of *x*                                |
| *i(x)*|The ideal position of *x*                            |
| *d(x)*|The distance of *x* from its ideal position (its PSL)|

## Table rotation

Analysis of the search and insertion properties of Robin Hood hash tables is
simplified if the "wrap around" aspect of their operation can be ignored.  This
can be achieved through a technique called "table rotation."

Consider this table, which uses the standard calculation to derive each entry's
ideal position:

* *i(x) = h(x) mod table_size*

|Position [*p]|Hash [*h(o)*]|Ideal position [*i(o)*]|PSL [*d(o)*]|   Key  |
|:-----------:|:-----------:|:---------------------:|:----------:|--------|
|      0      |  `f5940e9f` |            7          |      1     |Ross    |
|      1      |  `5e4138f0` |            0          |      1     |Alice   |
|      2      |  `d5718291` |            1          |      1     |Bob     |
|      3      |  `9f98979a` |            2          |      1     |Susan   |
|      4      |  `e15086ec` |            4          |      0     |Frank   |
|      5      |             |                       |            |        |
|      6      |             |                       |            |        |
|      7      |  `4837b98f` |            7          |      0     |Steve   |

Now consider the following table.  It is functionally equivalent to the previous
table, but the ideal position calculation has been changed slightly.

* *i(x) = [h(x) + 1] mod table_size*

|Position [*p]|Hash [*h(o)*]|Ideal position [*i(o)*]|PSL [*d(o)*]|  Key   |
|:-----------:|:-----------:|:---------------------:|:----------:|--------|
|      0      |  `4837b98f` |            0          |      0     |Steve   |
|      1      |  `f5940e9f` |            0          |      1     |Ross    |
|      2      |  `5e4138f0` |            1          |      1     |Alice   |
|      3      |  `d5718291` |            2          |      1     |Bob     |
|      4      |  `9f98979a` |            3          |      1     |Susan   |
|      5      |  `e15086ec` |            5          |      0     |Frank   |
|      6      |             |                       |            |        |
|      7      |             |                       |            |        |

Assume that one wishes to understand the search or insertion behavior for a key
whose hash value is `b081fd57`.  In the first table, that key's ideal position
will be 7, which will immediately wrap around to position 0.  In the second
table, the key's ideal position is 0, and the search proceeds from there without
wrapping around, making the analysis simpler.

This can be generalized to any ideal position calculation which adjusts the
hash value by some constant value (*A*).

* *i(x) = [h(x) + A] mod table_size*

This allows a hash table to be rotated by whatever number of positions is most
convenient for a given analysis.  The rotated table can then be treated as a
continuous, sequential buffer, **as long as the operation being analyzed can be
guaranteed not to wrap around the rotated table**.

Note that this library never actually performs bucket rotation, nor is it
explicitly used in this analysis.  Instead, this section shows that a hash table
can be treated as a sequential buffer, beginning at any position (subject to the
no wrap-around restriction).

## Bucket groups

The Robin Hood insertion algorithm has the effect of grouping entries with
equal ideal positions together.  These groupings are called "bucket groups," and
each bucket group within a table is identified by its common ideal position.

Consider this table.

|Position [*p]|Hash [*h(o)*]|Ideal position [*i(o)*]|PSL [*d(o)*]|  Key   |
|:-----------:|:-----------:|:---------------------:|:----------:|--------|
|      0      |  `4837b98f` |           15          |      1     |Steve   |
|      1      |  `49a338ff` |           15          |      2     |Chandler|
|      2      |  `5e4138f0` |            0          |      2     |Alice   |
|      3      |  `d5718291` |            1          |      2     |Bob     |
|      4      |  `77924041` |            1          |      3     |Ian     |
|      5      |  `81f62af3` |            3          |      2     |Karen   |
|      6      |             |                       |            |        |
|      7      |             |                       |            |        |
|      8      |             |                       |            |        |
|      9      |  `1111f939` |            9          |      0     |Monica  |
|     10      |  `9f98979a` |           10          |      0     |Susan   |
|     11      |  `0ef1713b` |           11          |      0     |Phoebe  |
|     12      |  `01d0f9eb` |           11          |      1     |Joey    |
|     13      |  `e15086ec` |           12          |      1     |Frank   |
|     14      |  `75bb7c3c` |           12          |      2     |Rachel  |
|     15      |  `f5940e9f` |           15          |      0     |Ross    |

This table contains 8 bucket groups:

* Bucket group 0 (identified by the ideal position of its sole member) &mdash;
  Alice,
* Bucket group 1 &mdash; Bob and Ian,
* Bucket group 3 &mdash; Karen,
* Bucket group 9 &mdash; Monica,
* Bucket group 10 &mdash; Susan,
* Bucket group 11 &mdash; Phoebe and Joey,
* Bucket group 12 &mdash; Frank and Rachel, and
* Bucket group 15 &mdash; Ross, Steve, and Chandler.

(But recall that the table could be rotated to make analysis easier.  Rotating
the table by 7 positions would change the groups' ideal positions and
identifiers as follows: 0 → 7, 3 → 10, 9 → 0, 10 → 1, 11 → 2, 12 → 3, and
15 → 6.)

## Insertion

Consider the insertion algorithm in light of table rotation and bucket groups.

The following are known.

* *i(x) = h(x) mod table_size + A*
* *d(o) = p - i(o)* ²

Now consider the case of inserting a new key (*k*).

1. The initial candidate is the new key, and the search begins at its ideal
   position.

   * *c := k*
   * *p := i(k)*

   Because the search for an appropriate position will proceed forward from this
   point, *i(k)* is the earliest position in the table at which *k* can be
   stored.²

2. At each position, there are multiple possibilities.

   1. The position may be empty.  The candidate will be placed in the current
      position, and the search will stop.

      * *o := c*
      * (stop)

   2. The position may be occupied by an entry whose PSL is greater than the
      candidate's PSL.

      * *d(o) > d(c)*

      The position will be skipped.  The entry that occupies the position is a
      member of a bucket group with an **earlier** ideal position.²

      * *d(o) = p - i(o)* and *d(c) = p - i(c)*
      * *p - i(o) > p - i(c)*
      * *-i(o) > -i(c)*
      * *i(o) < i(c)*

   3. The position may be occupied by an entry whose PSL is equal to the
      candidate's PSL.

      * *d(o) = d(c)*

      1. If the candidate is a new entry (not an entry that was displaced during
         the current insertion), and its key is be equal to that of the
         position's occupant, the occupant may or may not be replaced by the
         candidate.  Either way, the search will stop.

         * *o := c* (maybe)
         * (stop)

      2. Otherwise, the position will be skipped.  The entry that occupies the
         position is a member of the same bucket group as the candidate.

         * *d(o) = p - i(o)* and *d(c) = p - i(c)*
         * *p - i(o) = p - i(c)*
         * *-i(o) = -i(c)*
         * *i(o) = i(c)*

   4. The position may be occupied by an entry whose PSL is less than the
      candidate's PSL.

      * *d(o) < d(c)*

      The occupant is a member of a bucket group with a **later** ideal
      position.²

      * *d(o) = p - i(o)* and *d(c) = p - i(c)*
      * *p - i(o) < p - i(c)*
      * *-i(o) < -i(c)*
      * *i(o) > i(c)*

      The candidate will "steal" the position from its occupant.

      * *temp := o*
      * *o := c*
      * *c := temp*

3. The search will advance to next position and continue at step 2.

**Observations**

* A boundary between adjacent bucket groups (groups with no empty positions
  separating them) is identified by the fact that the PSL of the occupants
  does not increase by 1 when moving from one position to the next.

  * If *d(x+1) = d(x) + 1*, then *x* and *x+1* are members of the same bucket
    group.
  * If *d(x+1) < d(x) + 1*, then *x+1* is a member of a **later** bucket group.
    (The PSL of *x+1* is not greater than the PSL of *x*, because
    *i(x+1) > i(x)*.)

  *d(x+1) > d(x) + 1* cannot occur.  It would mean that *x+1* was a member of an
  **earlier** bucket group than *x*, but a candidate from an earlier bucket
  group would never be placed after a later group; the insertion algorithm will
  (if necessary) displace a member of the later group, because a member of a
  later group will always have a lower PSL than a member of an earlier group,
  when the two are being considered for the same position.  For the same reason,
  if the entry from an earlier group were stored first, the algorithm will not
  steal is position for a candidate from a later group.

* Entries are always inserted into the table at the end of their bucket group,
  either:

  * When an empty bucket is found, or
  * When an occupant's PSL is less than the PSL of the candidate at that
    position.

  The transition from *d(o) = d(c)* to *d(o) < d(c)* marks the pre-insertion
  boundary between the candidate's bucket group and a later bucket group.  See
  2(iv) above.



## Notes

1. A "hashing collision" refers to a situation in which the hash values of two
   or more keys, **modulo the size of the hash table**, are equal.  It is
   distinct from a hash collision, in which the hash values themselves are
   equal.  (All hash collisions are hashing collisions, but not all hashing
   collisions are hash collisions.)

2. Wrap-around ignored.  See [Table rotation](#table-rotation).



