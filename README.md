# Operating Systems Coursework — Kernel Internals, Container Isolation, GPU Memory

Practical work from two courses at Sharif University of Technology, Department of Computer
Engineering (instructor: Dr. Hossein Asadi).

The two halves of this repository approach memory and process management from opposite ends.
The **Operating Systems** half is hands-on and bottom-up: I extend a real kernel
(MIT's xv6-riscv) and then build a Linux container runtime from raw syscalls, so the same
concepts — address spaces, scheduling, isolation, locking — appear once from inside the
kernel and once from on top of it. The **Advanced Operating Systems** half is research-facing
and asks where those mechanisms break down on GPUs: address translation across a shared
CPU–GPU address space, TLB coherence and shootdown, and the KV-cache allocators that modern
LLM serving systems layer on top of hardware paging.

```
os/                       Operating Systems (Spring 1404) — eight assignment sets
  HW1/ … HW8/
    xv6/                  my modified xv6-riscv tree for that assignment
    zocker/               container work for that assignment
advanced-os/              Advanced Operating Systems (Fall 1405) — in progress
  HW1/
README.md
```

Each assignment had a theory section as well; this repository is the practical work only.

---

# Part I — Operating Systems

Two tracks run in parallel through all eight assignments.

**xv6** is [xv6-riscv](https://github.com/mit-pdos/xv6-riscv), MIT's teaching kernel for
RISC-V. Each assignment adds something real to it: a system call, a scheduler, a memory
optimization, a lock.

**zocker** is a container runtime written from scratch. It begins as a skeleton that merely
forks a child process, and each assignment adds one isolation primitive, until it can run a
command inside its own PID namespace, mount namespace, UTS namespace and filesystem, on a
rootfs extracted from a real Docker image, assembled as an OverlayFS mount. Assignments 1
and 2 explore Docker itself before the runtime work starts at assignment 3.

---

### HW1 — Getting the kernel and the container world up

**xv6.** Built and booted xv6 under QEMU, then added a user-space program to the system: a
new source file under `user/`, registered in `UPROGS` in the `Makefile`, callable from the
xv6 shell. Small, but it forces you to see where the user/kernel boundary is drawn in the
build itself.

**Containers.** Pulled `alpine:3.22.2`, ran containers, attached shells with `docker exec`,
and compared the PID of the same `sleep` process as seen from inside the container and from
the host. That mismatch is the observation the entire rest of the container track is built
on. Also covered running the daemon without `sudo`, and listing stopped containers.

*Concepts:* boot flow under QEMU, user/kernel split, build system, images vs. containers vs.
registries, the first glimpse of PID namespaces.

---

### HW2 — Seeing the syscall path, then measuring the isolation tax

**xv6.** Two parts. First, debugging the kernel with GDB: breaking on `syscall()`, walking
the backtrace to find the caller, printing `struct proc` for the current process, reading
`p->trapframe->a7` to recover the syscall number, and inspecting the privileged `sstatus`
register to determine which mode the CPU came from. Second, implementing a `top` system
call: kernel-side code that walks the process table and returns each process's PID, the
command it was launched with, and its memory footprint, plus the user program that calls it
and formats the output.

**Containers.** A structural and performance comparison of bare metal, a virtual machine,
and Docker as execution environments.

*Concepts:* trap and syscall dispatch, the trapframe, RISC-V privilege modes, copying data
from kernel to user space, virtualization overhead.

---

### HW3 — Process trees, and the first real namespace

**xv6.** Extended the previous assignment into an iterator-style system call:

```c
int next_process(int before_pid, struct process_data *proc);
```

It returns the first process whose PID exceeds `before_pid`, filling in PID, parent ID, heap
size, state and name, and returns 0 when the table is exhausted. On top of it, `top --tree`
reconstructs the process hierarchy. The interesting part is that the kernel hands back
child→parent edges while drawing a tree needs parent→child, so the inversion has to happen
in user space over a full pass.

**zocker.** The container's first process now runs in a fresh PID namespace and sees itself
as PID 1. Implemented with `unshare(2)` followed by `fork(2)` rather than `clone(2)` —
`CLONE_NEWPID` only takes effect for the *next* child, which is exactly why the two-step
version is simpler here. That requires `CAP_SYS_ADMIN`, so the `Makefile` was changed to
grant the capability with `setcap` instead of demanding `sudo` at every run.

*Concepts:* process table iteration, kernel→user struct copying, stateless iterator APIs,
PID namespaces, Linux capabilities, `/proc/<pid>/ns`.

---

### HW4 — Context switching by hand, on both sides

**xv6.** Implemented user-level threading (`uthread`): a `context` struct holding the
callee-saved RISC-V registers, a `thread` struct with its own stack and state, and the
four functions around them — `thread_init`, `thread_create`, `thread_schedule` and
`thread_yield`. `thread_schedule` round-robins over the table, switches context into the
next runnable thread, and exits when none remains. Three threads interleave their output and
terminate cleanly.

**zocker.** Even inside its own PID namespace, `ps` was still listing host processes, because
`ps` reads `/proc`, which was still the host's. Fixed by unsharing the mount namespace and
mounting a private `/proc` — which then broke `df` on the host, because mount namespaces
inherit propagation from their parent, so the fix had to use the right `mount(2)` flags to
stay contained. Then added a UTS namespace per container and `sethostname(2)` so the
container reports its own name.

*Concepts:* context switching and callee-saved register conventions, per-thread stacks,
cooperative scheduling, mount namespaces and mount propagation, `/proc` as the source of
process metadata, UTS namespaces.

---

### HW5 — Proportional-share scheduling, and filesystem confinement

**xv6.** Implemented lottery scheduling. A `ticket` field was added to `struct proc`,
initialized at process creation and inherited across `fork`. The scheduler in
`kernel/proc.c` now sums the tickets of all `RUNNABLE` processes, draws a random number, and
walks the table until the cumulative ticket count passes it. A `settickets(pid, n)` system
call assigns tickets and returns −1 on an invalid PID or ticket count. A test program forks
four children with deliberately skewed ticket ratios, has each spin on a counter, and
compares the resulting counters against the ratios — which is how you tell a working
proportional-share scheduler from one that merely runs.

**zocker.** Until this point a shell inside the container could read the host's files
freely. Used `chroot(2)` to move the container's root to `/tmp/zocker/<name>`, cutting off
that access, and examined how Docker derives its own container root via the overlay driver's
merged directory.

*Concepts:* probabilistic scheduling, fairness as a measurable property, ticket inheritance
semantics, randomness inside the kernel, `chroot` and filesystem namespacing, single-core
testing (`CPUS=1`) to make scheduling behaviour observable.

---

### HW6 — Allocator semantics, and booting a container on a real image

**xv6.** Added `realloc` and `calloc` to the xv6 user-space allocator in `user/umalloc.c`,
matching the standard's edge cases rather than just the common path: `realloc(NULL, n)`
behaves as `malloc(n)`, `realloc(ptr, 0)` frees and returns NULL, and otherwise the old
contents are copied into a new block before the old one is released. `calloc` allocates
`num × size` bytes and zeroes them. A test program exercises the boundary cases, not only
the happy path.

**zocker.** Exported a real container's filesystem with `docker export`, compared its root
directories against a live container (notably `/proc`, which is empty in the export because
it is a kernel-populated mount, not image content), extracted it into a directory, and
extended `run_container` so a `--base-dir` flag selects which rootfs to boot a container on.
At this point zocker runs real Alpine userland.

*Concepts:* heap allocator internals, block headers and `sbrk`, C standard library
semantics as a specification, image rootfs vs. runtime filesystem, pseudo-filesystems.

---

### HW7 — Copy-on-write fork, and layered image builds

**xv6.** The largest kernel change in the course: replacing eager copying in `fork` with
copy-on-write. `uvmcopy()` now maps the parent's physical pages into the child's page table
instead of allocating new ones, clearing `PTE_W` in both so any write traps. `usertrap()`
recognizes these faults, allocates a fresh page with `kalloc()`, copies the contents,
repoints the faulting process's PTE and restores write permission. Because a physical page
can now have several referrers, `kalloc.c` gained per-page reference counting so a page is
freed only on the last release, and `copyout()` was taught to perform the same copy when the
kernel writes into a COW page on a process's behalf. COW mappings are marked using the
RISC-V software-reserved PTE bits defined in `kernel/riscv.h`. Verified with `cowtest` and
the full `usertests` suite.

**zocker.** A script that builds a `Dockerfile` layer by layer, materializing the filesystem
diff each instruction produces.

*Concepts:* page tables and page faults, lazy allocation, PTE permission bits, reference
counting for shared physical memory, the kernel-write path into user memory, layered
filesystems.

---

### HW8 — Reader–writer locking, and OverlayFS

**xv6.** The global `ticks` counter is read by `sys_pause` and `sys_uptime` under
`tickslock`, a plain spinlock — which is correct but needlessly serializes readers that
cannot interfere with each other. Implemented `rwspinlock` in `kernel/spinlock.c` and
`kernel/spinlock.h`: many concurrent readers, at most one writer, no reader admitted while a
writer holds the lock, and writer priority so that a stream of readers cannot starve a
waiting writer. The hard part is the interleavings — a reader that checks for a pending
writer and then acquires has to guarantee nothing changed in between, which is where GCC's
atomic builtins come in. Verified with `rwlktest` and `usertests -q`.

**zocker.** Replaced the plain container root directory with a real OverlayFS mount —
`lowerdir` holding the read-only image layers, `upperdir` capturing writes, `workdir` for
atomic operations, `merged` as the container's view — and investigated `docker commit` as
the operation that turns an upper layer back into an image.

*Concepts:* multicore synchronization, reader vs. writer starvation, atomic primitives and
memory ordering, lock granularity as a performance decision, union filesystems and copy-up.

---

## Building and running

**xv6** (per assignment, inside `os/HW<n>/xv6/`):

```bash
make qemu                 # boot xv6; Ctrl-A then X to quit
CPUS=1 make qemu          # single core — needed to observe the lottery scheduler
make qemu-gdb             # then, in a second terminal: gdb-multiarch
```

Inside the xv6 shell: `top`, `top --tree`, `uthread`, `cowtest`, `rwlktest`, `usertests -q`.

**zocker** (inside `os/HW<n>/zocker/`):

```bash
make
./zocker run --name demo 'sh'
./zocker run --name demo --base-dir /tmp/zocker/alpine-rootfs 'sh'
```

Requirements: QEMU 6.2 or newer (xv6-riscv will not boot on older versions), a RISC-V
cross-toolchain (`gcc-riscv64-unknown-elf`, `gdb-multiarch`), and Docker. The container work
is Linux-only — namespaces, `chroot` and OverlayFS have no macOS or WSL equivalent.

---

# Part II — Advanced Operating Systems

This course moves the same questions onto GPUs and LLM serving systems, where memory
management happens at three layers that do not line up with each other:

- **Hardware address translation** — GPU page tables, TLBs, page-table walkers, and the cost
  of keeping translations coherent in an address space shared with the CPU.
- **Driver and runtime** — CUDA Unified Memory and the CUDA Virtual Memory Management API:
  page migration, oversubscription, and explicit mapping and unmapping of physical memory.
- **Application-level allocation** — logical KV-cache blocks in systems like vLLM, which
  implement their own paging on top of memory the GPU already paginates.

A claim that holds at one layer frequently does not survive a move to another, and the
assignments are built around exactly that tension: when does freeing a KV-cache block
actually change a hardware page table, when does a GPU TLB miss stay hidden behind warp
parallelism, and when is deferring an invalidation still safe.

**Status: in progress.** The current assignment covers GPU memory-management layers, KV-cache
allocator behaviour, remapping correctness in heterogeneous CPU–GPU systems, TLB
invalidation policy, a comparative study of the literature, and a research-opportunity memo.
What is published here so far is the reading list; the analysis follows once the assignment
has been graded.

### Reading list

**GPU address translation and page management**

- Power, Hill, Wood — *Supporting x86-64 Address Translation for 100s of GPU Lanes*, HPCA 2014
- Ausavarungnirun et al. — *Mosaic: A GPU Memory Manager with Application-Transparent Support for Multiple Page Sizes*, MICRO 2017
- Shin et al. — *Scheduling Page Table Walks for Irregular GPU Applications*, ISCA 2018
- Vesely et al. — *Observations and Opportunities in Architecting Shared Virtual Memory for Heterogeneous Systems*, ISPASS 2016
- Yoon, Lowe-Power, Sohi — *Filtering Translation Bandwidth with Virtual Caching*, ASPLOS 2018
- Jaleel, Ebrahimi, Duncan — *DUCATI: High-performance Address Translation by Extending TLB Reach of GPU-accelerated Systems*, TACO 2019
- Yan, Lustig, Nellans, Bhattacharjee — *Translation Ranger: Operating System Support for Contiguity-Aware TLBs*, ISCA 2019

**TLB coherence and shootdown**

- Yan, Veselý, Cox, Bhattacharjee — *Hardware Translation Coherence for Virtualized Systems*, ISCA 2017
- Amit — *Optimizing the TLB Shootdown Algorithm with Page Access Tracking*, USENIX ATC 2017
- Kumar et al. — *LATR: Lazy Translation Coherence*, ASPLOS 2018
- Amit, Tai, Wei — *Don't Shoot Down TLB Shootdowns!*, EuroSys 2020

**Unified Memory, oversubscription and page migration**

- Shao et al. — *Oversubscribing GPU Unified Virtual Memory: Implications and Suggestions*, ICPE 2022
- Zhang et al. — *G10: Enabling An Efficient Unified GPU Memory and Storage Architecture with Smart Tensor Migrations*, MICRO 2023
- Kim et al. — *MOST: Memory Oversubscription-Aware Scheduling for Tensor Migration on GPU Unified Storage*, IEEE CAL 2025
- Shin, Bang, Park, Kim — *ARIADNE: Adaptive UVM Management for Efficient GPU Memory Oversubscription*, HPCA 2026

**KV-cache and virtual memory in LLM serving**

- Kwon et al. — *Efficient Memory Management for Large Language Model Serving with PagedAttention*, SOSP 2023
- Prabhu et al. — *vAttention: Dynamic Memory Management for Serving LLMs without PagedAttention*, ASPLOS 2025
- Xu et al. — *vTensor: Flexible Virtual Tensor Management for Efficient LLM Serving*, arXiv 2024
- Zheng et al. — *SGLang: Efficient Execution of Structured Language Model Programs*, NeurIPS 2024
- Qin et al. — *Mooncake: Trading More Storage for Less Computation*, USENIX FAST 2025
- Cheng et al. — *LMCache: An Efficient KV Cache Layer for Enterprise-Scale LLM Inference*, arXiv 2025
- Cheong, Hwang, Kim, Lee — *KVGuard: A KV Cache Safety Checker for LLM Serving Systems*, arXiv 2026

**Documentation and measurement tooling**

- NVIDIA — *CUDA Programming Guide: Unified Memory*
- NVIDIA — *CUDA Driver API: Virtual Memory Management* (`cuMemAddressReserve`, `cuMemCreate`, `cuMemMap`, `cuMemSetAccess`, `cuMemUnmap`, `cuMemGetAllocationGranularity`)
- NVIDIA — *Nsight Systems User Guide*

One limitation worth stating plainly: there is often no direct counter for GPU TLB misses or
invalidations, and CPU `perf` counters do not measure GPU-side translation events. Several
claims in this area can only be inferred, never directly observed, and keeping that
distinction visible is part of what the assignment asks for.

---

## Attribution and licensing

The xv6 trees are derived from [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) by MIT
PDOS and remain under its original MIT license, preserved unchanged in each assignment
directory. The `zocker` skeleton was provided by the course staff; the isolation mechanisms
described above are my implementations. Papers cited in Part II belong to their authors —
nothing here reproduces their results, and anything quantitative in my notes carries a
pointer to the specific figure, table or page it came from.

This is coursework published for reference. If you are currently taking either course,
please write your own solutions.
