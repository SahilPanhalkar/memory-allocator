# Simple Memory Allocator in C

A small educational memory allocator that demonstrates how dynamic memory
allocation can work internally on Linux.

## Features

- `malloc()` — allocates a block of memory.
- `free()` — marks a block as free so it can be reused; attempts to release
  the final block back to the process system break.
- `calloc()` — allocates memory and initializes it to zero.
- `realloc()` — resizes an allocation by reusing it when it is already large
  enough, or allocating a new block and copying the contents.
- First-fit search through a linked list of allocation headers.
- Header metadata stores each block's size, free state, and next-block pointer.
- Mutex protection around allocator metadata.
- Alignment through `max_align_t`.

## How it works

Each allocation reserves space for a header followed by the requested user
memory:

```text
+----------------------+---------------------------+
| Allocation metadata  | Memory available to user  |
+----------------------+---------------------------+
```

The allocator first searches for a free block that is large enough. If none is
available, it requests more memory using `sbrk()`. The header lets the allocator
find the block's size and status when `free()` or `realloc()` is called.

## Requirements

- Linux
- GCC
  
On Ubuntu/WSL, compile the shared library with:

```bash
gcc -Wall -Wextra -Wpedantic -fPIC -shared memalloc.c -o memalloc.so -pthread
```

This command builds the shared library; it does not by itself prove that every
allocator behavior works correctly.




