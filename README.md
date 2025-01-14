# xv6
- Xv6 is a teaching operating system developed in the summer of 2006 for MIT's operating systems course. Below, I have implemented a few system calls and tested scheduling policies in the give Xv6 source code. The user needs to install the Xv6 and can follow the below provided guide for the same. https://pdos.csail.mit.edu/6.S081/2024/tools.html
- It was a group assignment and shared here is my team partner for the same.
[Prasoon Dev](https://github.com/prasoondev)
## Copy-On-Write FORK
- Implemented COW Fork with the following requirements
    - When the parent process forks, instead of making a full copy of all the memory pages, we’ll have both processes share the same pages initially.
    - These shared pages will be marked as read-only and flagged as “copy-on-write.” This means the processes can share memory until one of them tries to make changes.
    - If a process tries to write to one of these shared pages, the RISC-V CPU will raise a page-fault exception.
    - At that point, the kernel will jump in to make a duplicate of the page just for that process and map it as read/write, allowing the process to modify its own copy without affecting the other.
- Syntax
    - The changes have been made to the `initial-xv6` provided. To run the shell:
        ```
        make clean
        make qemu
        ```
    - To test `lazytest`, inside the xv6-shell:
        ```
        $ lazytest
        ```
- Changes Made:
    - In `kernel/riscv.h`, defined following macros
        ```
        #define PTE_C (1L << 8)
        #define NUM_PREF_COUNT (PHYSTOP - KERNBASE) / PGSIZE
        #define PA2REFIDX(pa) ((PGROUNDDOWN(pa) - KERNBASE) / PGSIZE)
        #define PA2CNT(pa) prefcnt.count[PA2REFIDX((uint64) pa)]
        ```
        - `PTE_C` is a defined flag for checking/setting COW pages.
        - `NUM_PREF_COUNT` defines the maximum number of reference counts possible based on number of total pages.
        - `PA2REFIDX(pa)` defines the conversion for getting the reference count index of given physical address of page.
        - `PA2CNT(pa)` defines the array element in reference count array given a physical address.
    - In `kernel/vm.c`,
        -  modified `uvmcopy()` to handle the following cases:
            - Remove the write permission from the PTE if it is set and set PTE_C for handling the PTE as COW page.
                ```
                if (*pte & PTE_W) {
                *pte &= ~PTE_W; // for copy on write: clear parent's PTE_W
                *pte |= PTE_C; // for copy on write: set PTE_C
                }
                flags = PTE_FLAGS(*pte);
                ```
            - Commented out memory allocation to and used `mem = (char *) pa` to ensure both the old and new page tables point to the same physical page .
            - Incremented reference count on the physical page `pa`. This is essential in COW, as the system needs to keep track of how many references (processes) are pointing to each shared page.
                ```
                prefcnt_inc(pa);
                ```
        - modified `copyout()` to handle the following cases:
            - The copyout function is responsible for copying data from the kernel to user space, specifically to the destination virtual address `dstva` in the user’s address space. 
            - Checks is the page at `va0` is a COW page by using `uncopied_cow` function.
                ```
                if (uncopied_cow(pagetable, va0)) {
                    if (cow_alloc(pagetable, va0) == -1)
                        return -1;
                }
                ```
            - If the page is a COW page, `cow_alloc()` function is called for duplication of a COW page, to ensure that the process when trying to `memmove` (make changes to the shared page), does not do so for all processes sharing the same page.
            - If `cow_alloc()` fails (returns -1), `copyout` exits with an error because the write cannot proceed without a private writable copy of the page.
        - added function `uncopied_cow()` that is responsible for determining whether a given virtual address `va` is marked for copy-on-write in the specified pagetable. It returns 0 in cases of invalid virtual address range, the PTE not existing, the PTE having not set `PTE_V` and `PTE_U` flags and if the referenced page is a COW page by `PTE_C`. It returns 1 if the conditions are true.
        - added function `cow_alloc()` that  performs the actual duplication of a COW page, allocating a new page for the current process to modify while leaving the original page intact for any other process sharing it. The function:
            - aligns the `va` to the nearest page boundary.
            - uses the `walk()` function to find PTE for this address and returns -1 if not found.
            - extracts the `pa` (physical address) and sets the flags using `PTE_FLAGS(*pte)`.
            - adds the `PTE_W` flag to the page to make it writable.
            - allocates memory for a duplicate page using `kalloc()`. If allocation fails, -1 is returned.
            - copies the contents from the original page `pa` to new page `mem` using `memmove`.
            - unmaps the original COW page at va by using `uvmunmap`, and maps the new private page at the same virtual address va with the updated writable flags using `mappages`. If remapping fails, it frees the newly allocated page and returns -1.
    - In `kernel/kalloc.c`,
        -  Added spinlock along wih the `count` array to keep track of how many processes are currently using the page.
            ```
            struct {
            struct spinlock lock;
            uint count[NUM_PREF_COUNT];
            } prefcnt;
            ```
        - Initialised the lock in `kinit()`
            ```
            initlock(&prefcnt.lock, "prefcnt");
            ```
        - Initialised the `count` values to 1 in `freerange()` and `kalloc()` to establish a baseline that ensures correct memory management and deallocation in a COW setup. Initially every page is assumed to have a single user, and hence `count` array is initalised to 1.
        - Decremented the `count` of the page in `kfree()`. It means that if the process calls `kfree()` for a particular page, it is no longer referencing it and thus count for the page is decremented. If the `count` reaches 0, it means that the page is no longer referenced by any processes and thus is freed from the memory list.
        - Added helper function `prefcnt_inc` that is used to increment the reference count of a particular page with given `pa` along witgh proper spinlock usage.
    - In `kernel/trap.c`,
        - In `usertrap`, case of page fault when a process tries to write on a shared COW page is handled. This page fault happens as the COW page is set to read-only until the process tries to do a write operation on it. The condition also checks if the page fault happens on a COW page. If both conditions are satified, `cow_alloc()` is called to allocate a new physical page, copy the contents of the original page to this new page, and update the PTE for this process to point to the new copy (with PTE_W set). If `cow_alloc()` fails and returns -1, the process is killed as it cannot proceed without its own writable copy of the page.
        ```
        else if((r_scause() == 15) && uncopied_cow(p->pagetable, r_stval()))
        {
            if (cow_alloc(p->pagetable, r_stval()) < 0)
            p->killed = 1;
        }    
        ```
    - In `kernel/defs.h`, declared the functions used througout:
        ```
        // kalloc.c
        void            prefcnt_inc(uint64 pa);
        // vm.c
        int             cow_alloc(pagetable_t pagetable, uint64 va);
        int             uncopied_cow(pagetable_t pagetable, uint64 va);

        ```
    - In `Makefile`, added the `$U/_lazytest\` under `UPROGS` section.
    - Added provided `lazytest.c` in `user`.
- Results:
    - The code passes the provided `lazytest.c` and `usertests`. The output for `lazytest` is 
        ```
        $ lazytest
        simple: ok
        simple: ok
        three: ok
        three: ok
        three: ok
        file: ok
        ALL COW TESTS PASSED
        ```
## Analysis (Report)
Page Fault Frequency
- Added a `cowtest` that contains 2 tests `readtest` and `writetest`.
    ```
    void read_test(int *array, int size) {
        int sum = 0;
        for (int i = 0; i < size; i++) {
            sum += array[i]; // Read-only access
        }
    }
    void write_test(int *array, int size) {
        for (int i = 0; i < size; i++) {
            array[i] = i; // Write access, triggers COW
        }
    }
    ```
- During `read_test`, minimal page_faults were recorded which is expected as the process accesses shared pages only.
- During `write_test`, a large amount of page_faults were encountered since xv6 COW is set up to share pages read-only and copy them on write, each write should trigger a page fault and allocate a new page for the child.
- The result was analysed by using a global counter and running `cowtest`.
- For given `cowtest`, an anerage of 5 page faults were counted during `read_test` and 12 for `write_test`. 
Brief Analysis
- The benefits of COW fork are as follows:
    - `Efficiency in Memory Usage`: COW allows multiple processes to share the same physical memory pages after a fork operation until one of them modifies a page. This means that instead of duplicating memory pages for each forked process, the operating system can allocate a single copy saving memory.

    - `Reduced Overhead`: When a process forks, the overhead of duplicating the entire address space is eliminated.
- Improvements on COW fork:
    - `Better Algorithms`: Instead of allocating, everytime a write operation happens and causing a page fault, we can implement better algorithms to decide whether the page needs to be a COW page or not.
    - `Granular Page Sharing`:  Instead of sharing entire pages, the system could implement a more granular approach, allowing shared access to individual data structures or objects within a page which could reduce the overhead caused by multiple page faults.
## References:
- MIT LAB COW FORK https://pdos.csail.mit.edu/6.828/2024/labs/cow.html
- Lab6 Copy-on-Write Fork for xv6 紀錄 https://hackmd.io/@xv6/BywizQX8h
- [MIT 6.s081] Xv6 Lab6 COW 实验记录 https://ttzytt.com/2022/07/xv6_lab6_record/
