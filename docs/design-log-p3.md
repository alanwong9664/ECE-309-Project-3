# Project 3 Design Log
**P2 code used:** Instructor reference, enabled with `USE_P2_REFERENCE=ON`.
**Tools used:** OpenAI Codex generated the P3 implementation and tests and drafted
this log. Builds and checks ran in WSL2 Ubuntu using GCC, CMake, CTest,
AddressSanitizer, and UndefinedBehaviorSanitizer.

## 1. Hash table
The registry uses separate chaining. Each bucket is a vector of entries, and
each entry stores a name and a `std::unique_ptr<Tool>`. If two names hash to
the same bucket, lookup checks the entries there for an exact match. Chaining
keeps this straightforward without needing to wrap a search around the table
as linear probing would. Storing the name also avoids repeated calls to the
tool's virtual `name()` function.

The hash function is 64-bit FNV-1a. It uses every byte in order, so names with
the same letters in a different order can produce different hashes. The hash
function returns its full value. Taking that value modulo the bucket count
selects the bucket.

The table starts with 16 buckets and allows a load factor up to 1.0. Before
an insertion would exceed that limit, the bucket count doubles and every entry
gets a new bucket index. Null pointers and duplicate names are rejected first.
A duplicate leaves the original tool in place and destroys the incoming one.

Inserting `tool_0` through `tool_9999` produced these results:

| Measurement | FNV-1a | Weak `s[0] + s.size()` hash |
|---|---:|---:|
| Bucket count | 16,384 | 16,384 |
| Load factor | 0.61035156 | 0.61035156 |
| Longest chain | 6 | 9,000 |

The weak hash was measured separately using the same names and bucket count.
There are 9,000 names with four-digit suffixes, all with the same first letter
and length. That explains why they all end up in one bucket with the weak hash.

A resize takes O(n), but doubling leaves room for many insertions before the
next resize. Adding the work across successive resizes gives a geometric sum,
so insertion is amortized expected O(1) for bounded-length names. A poor hash
can still make lookup O(n) by putting everything into one chain.

## 2. Pointer stability
Rehashing moves entries and their owning pointers, not the tool objects on the
heap. A `Tool*` returned by `find()` therefore stays valid until the registry is
destroyed. Rehash also reserves space in every new bucket before moving entries,
so failure to allocate that space leaves the old table intact. The scale test
saves each tool's address and checks it after resizing. A separate test counts
destructor calls to check ownership and duplicate rejection.

## 3. Calculator trace
The calculator has a numbers stack and an operations stack. Each works like a
pile of plates: push adds to the top; pop removes from it.

Here is the trace for `2 * (3 + 4) - 5`. The rightmost item is the top; a dash
means empty.

| Input | Action | Numbers | Operations |
|---|---|---|---|
| `2` | Store 2 | `2` | — |
| `*` | Store multiplication | `2` | `*` |
| `(` | Mark a group | `2` | `* (` |
| `3` | Store 3 | `2 3` | `* (` |
| `+` | Store addition | `2 3` | `* ( +` |
| `4` | Store 4 | `2 3 4` | `* ( +` |
| `)` | Add 3+4; remove `(` | `2 7` | `*` |
| `-` | Multiply 2*7; store subtraction | `14` | `-` |
| `5` | Store 5 | `14 5` | `-` |
| End | Subtract 14-5 | `9` | — |

Multiplication has higher priority than subtraction. Each operation pops its
right number, then its left, and pushes the result. Equal-priority operations
run left to right.

## 4. Errors as values
If the model requests a missing file, the tool returns a result with `ok=false`
and a message explaining the failure. The dispatcher sends this result back to
the model, which can request another file or explain the problem to the user.

This is what "errors as values" means: the returned data describes the failure.
A missing file is a normal possibility, so it uses the same result-handling
path as a successful request. Throwing an exception would instead interrupt
normal execution and require a catch handler.

Exceptions fit programming mistakes, such as treating a JSON number as a
string. The dispatcher still catches exceptions from tools as a safeguard.

## 5. What I would design differently

A useful addition would be a way to reserve space for an expected number of
tools. Callers that know their tool count could avoid several startup resizes.
The assignment fixes the public interface, so this version uses automatic
doubling instead.
