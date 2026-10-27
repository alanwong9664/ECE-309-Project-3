# Project 3 walkthrough

Build commands and notes for reviewing the three P3 implementations.

## 1. Establish the baseline

Open Ubuntu/WSL in this project directory. The instructor's P2 reference is used
because this workspace did not contain your P2 implementation. The specification
permits that choice without a P3 score penalty.

```bash
cmake -S . -B build -DUSE_P2_REFERENCE=ON -DMH_WERROR=ON
cmake --build build -j 4
./build/test_p2
```

Checkpoint: all 14 P2 regression tests pass. Sanitizers are enabled by default.
This baseline helps distinguish an old problem from a new P3 defect.

## 2. Follow one request through the existing program

Read these files in order:

1. `src/harness/tool_dispatcher.cpp`: looks up a name and invokes its tool.
2. `src/tools/builtin_tools.cpp`: registers four tools at startup.
3. `include/tools/tool.h`: defines the common interface and failure result.
4. `include/tools/tool_registry.h`: declares the registry API and our buckets.

For a `read_file` request, the dispatcher calls `find("read_file")`, then
`invoke()`. The provided file tool calls our `Workspace::resolve`, then the
provided symlink check, then reads the file. The harness returns the tool result
to the model and asks the model again without taking another user turn.

## 3. Understand the registry before the smaller components

Read `src/tools/tool_registry.cpp` alongside its tests:

```bash
./build/test_p3 reg_
./build/miniharness --tools
```

An entry holds a cached name and an owning pointer. A hash selects one bucket;
lookup scans that bucket's entries. Two names can share a bucket without being
equal, so comparing the actual key is essential.

The table starts with 16 buckets and grows before the next insertion would make
size greater than bucket count. Every key needs a new bucket index after growth.
Rehash first allocates destination capacity, then moves entries, then swaps the
bucket arrays. It does not change the number of registered tools.

Trace a 17th insertion on paper: capacity grows from 16 to 32, the 16 old entries
are redistributed, then the new tool is inserted. A tool's pointer remains the
same because the owning pointer moves while its heap object stays put.

Measured for 10,000 tools: 16,384 buckets, load factor 0.61035156, longest chain 6.
The weak hash comparison has a longest chain of 9,000 at the same capacity.

Questions to explain aloud:

- Why is a collision different from a duplicate?
- Who destroys a rejected duplicate? Who destroys a registered tool?
- Why does changing the bucket count require rehashing every key?
- Why is occasional O(n) rehashing compatible with amortized expected O(1) add?
- Which object would move if we stored tools directly instead of owning pointers?

## 4. Review workspace validation

Read `src/tools/workspace.cpp`, then run:

```bash
./build/test_p3 ws_
```

The ordering is deliberate: reject empty/NUL-containing input, reject rooted
paths, normalize the components, then reject a leading `..`. A filename like
`my..notes.txt` is legal because it is not a parent-directory component.
`a/../b` normalizes to `b`, while `a/../../b` normalizes to `../b` and is rejected.

`resolve()` does no filesystem I/O and accepts paths that do not exist yet.
Symbolic links need the separate provided check. The symlink test demonstrates
why both stages are necessary.

## 5. Review the calculator

Read `src/tools/calculator_eval.cpp`, then run:

```bash
./build/test_p3 calc_
```

One vector acts as the values stack; another acts as the operators stack.
An incoming operator first applies pending operators with higher or equal
precedence. Parentheses form a barrier until the closing parenthesis arrives.
Applying an operator pops its right operand first, then its left operand.

Trace `2 * (3 + 4) - 5` using both stacks. Then try `10 - 4 - 3` and explain
why changing the precedence comparison from `>=` to `>` would produce 9.
Loops make long expressions and deep nesting independent of call-stack depth.

## 6. Verify the whole system

```bash
ctest --test-dir build --output-on-failure
./build/test_loop
./build/miniharness --script scripts/tools_demo.script
```

The demo writes `workspace/notes/summary.txt`; the end-to-end tests use temporary
workspace copies. Expected results: 28 P3 tests, 14 P2 tests, nine tool-loop
tests, and seven end-to-end sessions pass. CTest groups these into four targets.

Before submission, review the 500–800-word design log and add your authorship
details. `github.txt` contains the repository URL. Refresh the ZIP after any
edits and upload the final files to the repository.
