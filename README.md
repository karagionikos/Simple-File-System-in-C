# Simple-File-System-in-C
## Overview

SimpleFS is a small filesystem implemented in C.
It simulates a block-based filesystem using a virtual disk image
(`disk.img`) instead of interacting with a real disk.

The project demonstrates the core concepts of filesystem design:
- disk layout management
- block allocation
- inode-based file storage
- directories
- file operations
- mounting/unmounting
- persistence
---

## Project Structure
After downloading the four files, your project should look like this.
```
simplefs/
├── src/
│   ├── fs.h      ← structs, constants, public API
│   ├── fs.c      ← filesystem engine
│   └── main.c    ← interactive shell
└── Makefile
```

### `fs.h`
Defines the filesystem interface and data structures.

### `fs.c`
Contains the filesystem implementation.

### `main.c`
Provides an interactive command shell.

---

# SimpleFS — Setup Guide

## Requirements
| Tool | Minimum version | Check with |
|------|----------------|------------|
| GCC  | 4.9+           | `gcc --version` |
| GNU Make | any        | `make --version` |
 
No external libraries required. Works on Linux and macOS. On Windows, use WSL.
---

## Build
 
```bash
cd simplefs
make
```
This produces a single binary: `./simplefs`.
---

## Run
 
```bash
./simplefs
```

On first run, `disk.img` (the 2 MB simulated disk) does not exist yet, so the filesystem auto-formats one:

On every subsequent run, the existing `disk.img` is mounted, and your files persist.
---

## Disk image

To start completely fresh, either run `mkfs` inside the shell or delete the image:
 
```bash
rm disk.img
```
---
 
## Makefile targets
 
| Target | Command | What it does |
|--------|---------|--------------|
| Build | `make` | Compiles all sources |
| Run | `make run` | Builds and launches the shell |
| Clean | `make clean` | Removes binaries, object files, and `disk.img` |
| Valgrind | `make valgrind` | Runs under Valgrind for memory leak checking |
---

## Mention. It was made with 40% vibecoding as a fun side self-educational project.

