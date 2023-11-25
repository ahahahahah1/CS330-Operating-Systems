#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */
#define FOUR_KB (4 * 1024)
#define MASK 0xFFFFFFFFULL << 12 // this mask will be used to extract the pfn from the table entries

int print_binary(u64 num) {
    int i;
    for (i = 63; i >= 0; i--) {
        // u64 mask = 1ULL << i;
        if (num & (1ULL << i)) {
            printk("1");
        } else {
            printk("0");
        }
        if(i % 4 == 0) {
            printk("_");
        }
    }
    printk("\n");
}

u64 extract_ith_bit(u64 *pte_entry, int i) {
    if (i < 0 || i >= 64) {
        // bit position out of range. Must be between 0 and 63
        return -EINVAL;
    }
    u64 mask = 1ULL << i;
    return (*pte_entry & mask) >> i;
}

// HELPER FUNCTIONS FOR mmap()

struct vm_area* allocate_and_initialize_VMA(u64 start, u64 end, u32 access_flags) {
    struct vm_area *new_node = os_alloc(sizeof(struct vm_area));
    new_node->vm_start = start;
    new_node->vm_end = end;
    new_node->access_flags = access_flags;
    new_node->vm_next = NULL;
    
    stats->num_vm_area++; // updating the count of number of vm_areas whenever a new block is created

    return new_node;
}

int merge_and_update(struct vm_area* prev, struct vm_area* new_node, struct vm_area* next) {
    u32 counter = 0;
    if(next != NULL) {
        if(new_node->vm_end == next->vm_start && new_node->access_flags == next->access_flags) {
            new_node->vm_end = next->vm_end;
            new_node->vm_next = next->vm_next;
            stats->num_vm_area--;
            os_free(next, sizeof(struct vm_area));
            counter = counter + 1;
        }
    }
    if(prev != NULL && prev->vm_start != MMAP_AREA_START) {
        if(prev->vm_end == new_node->vm_start && prev->access_flags == new_node->access_flags) {
            prev->vm_end = new_node->vm_end;
            prev->vm_next = new_node->vm_next;
            stats->num_vm_area--;
            os_free(new_node, sizeof(struct vm_area));
            counter = counter + 2;
        }
    }
    return counter;               // gcc shutup!
}

long find_valid_unmapped_region(struct exec_context* current, u64 length, u32 prot) {
    // returns the starting address of our block after any merging, if applicable
    struct vm_area* prev = current->vm_area;
    struct vm_area* curr = prev->vm_next;
    struct vm_area* new_node = NULL;
    long allocated_pointer = -EINVAL;
    while(curr != NULL) {
        if(length <= curr->vm_start - prev->vm_end) {
            // can allocate a block in this region
            new_node = allocate_and_initialize_VMA(prev->vm_end, prev->vm_end + length, prot);
            allocated_pointer = new_node->vm_start;
            prev->vm_next = new_node;
            new_node->vm_next = curr;
            merge_and_update(prev, new_node, curr);
            return allocated_pointer;
        }
        prev = curr;
        curr = prev->vm_next;
    }
    // reached the end of the list but no block in between of sufficient size
    if(prev->vm_end + length <= MMAP_AREA_END) {
        new_node = allocate_and_initialize_VMA(prev->vm_end, prev->vm_end + length, prot);
        prev->vm_next = new_node;
        new_node->vm_next = curr;
        allocated_pointer = new_node->vm_start;
        merge_and_update(prev, new_node, curr);
        return allocated_pointer;
    }
    return allocated_pointer; // if control reaches here, allocated pointer is -1
}


// HELPER FUNCTIONS FOR munmap() and mprotect()
int free_VMA(struct vm_area *curr) {
    os_free(curr, sizeof(struct vm_area));
    stats->num_vm_area--;
    return 0; // gcc shutup!
}

int flush_tlb(u64 addr) {
    asm volatile ("invlpg (%0);" 
                  :: "r"(addr) 
                  : "memory");
    return 0; // gcc shutup!
}

int walk_v2p(u64 deletion_start, u64 deletion_end, struct exec_context* current, u32 calling_func, u32 access_bit) {
    // calling func = 0 if unmap is callng it, 1 if mprotect is calling it
    struct vm_area* vm = current->vm_area;
    int offsets[4];
    int i; // iterator to compute offsets
    u64* CR3;
    u64* pte_table_address;
    u64 new_pfn;
    u64 temp; // would be used to temporarily store the deletion_start to compute the offsets
    while(vm != NULL && deletion_start < deletion_end) {
        if(deletion_start >= vm->vm_start && deletion_start < vm->vm_end) {
            temp = deletion_start >> 12;
            for(i = 3; i >= 0; i--) {
                offsets[i] = temp & ((1 << 9) - 1);
                temp = temp >> 9;
            }
            CR3 =  NULL;
            pte_table_address = NULL;
            new_pfn = current->pgd;
            for(i = 0; i < 4; i++) {
                CR3 = (u64*) osmap(new_pfn);
                pte_table_address = CR3 + offsets[i];
                if((*pte_table_address & 1) == 0) {
                    // entry is not present so virtual->physical mapping was not created
                    break;
                }
                new_pfn = (*pte_table_address & (MASK)) >> 12;
            }
            if(i == 4) {
                // physical entry was found corresponding to the vm_area
                if(calling_func == 0) { // the function has been called from unmap()
                    put_pfn((u32)new_pfn);
                    if(get_pfn_refcount((u32)new_pfn) == 0) {
                        os_pfn_free(USER_REG, new_pfn);
                    }
                    *pte_table_address = 0x0; // resetting the value
                }
                else { // the function has been called from mprotect()
                    *pte_table_address = *pte_table_address & (~(1 << 3)); // clearing the third bit
                    if(get_pfn_refcount(new_pfn) == 1) {
                        // only one reference to the pfn, so we can change the access bit
                        *pte_table_address = *pte_table_address | (access_bit << 3); // setting the third bit to the access_bit
                    }
                }
                flush_tlb(deletion_start); // flushing it from the cache
            }
            deletion_start += FOUR_KB;
        }
        else {
            vm = vm->vm_next;
        }
    }
    return 0;   // gcc shutup!
}

// HELPER FUNCTIONS FOR do_cfork()
int copy_v2p(u32 parent_pgd, u32 child_pgd, u64 start_addr, u64 end_addr) {
    u64 child_pfn, parent_pfn;
    u64 temp;
    u64 offsets[4];
    int j;
    u64 *parent_pte_table_address = NULL, *child_pte_table_address = NULL;
    u64 *parent_CR3 = NULL, *child_CR3 = NULL;
    while(start_addr < end_addr) {
        temp = start_addr >> 12;
        for(j = 3; j >= 0; j--) {
            offsets[j] = temp & ((1 << 9) - 1);
            temp = temp >> 9;
        }
        child_pfn = child_pgd;
        parent_pfn = parent_pgd;
        for(j = 0; j < 4; j++) {
            child_CR3 = (u64*) osmap(child_pfn);
            parent_CR3 = (u64*) osmap(parent_pfn);
            child_pte_table_address = child_CR3 + offsets[j];
            parent_pte_table_address = parent_CR3 + offsets[j];
            if((*parent_pte_table_address & 1) == 1) {
                // table entry present in parent, need to allocate it in child
                if(j == 3) {
                    // disable write on parent and copy the value to child
                    *parent_pte_table_address = *parent_pte_table_address & (~(1 << 3));
                    *child_pte_table_address = *parent_pte_table_address;
                    // get pfn from the pte_table_address
                    child_pfn = (*child_pte_table_address & (MASK)) >> 12;
                    get_pfn(child_pfn); // incrementing ref_count by 1
                }
                else if((*child_pte_table_address & 1) == 0) {
                    child_pfn = os_pfn_alloc(OS_PT_REG);
                    *child_pte_table_address = *parent_pte_table_address & (~(MASK));
                    *child_pte_table_address = *child_pte_table_address | (child_pfn << 12);
                }
            }
            else {
                break;
            }
            parent_pfn = (*parent_pte_table_address & (MASK)) >> 12;
            child_pfn = (*child_pte_table_address & (MASK)) >> 12;
        }
        start_addr += FOUR_KB;
    }
    return 0; // gcc shutup!
}

/**
 * mprotect System call Implementation.
 */
long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if(!current) {
        return -EINVAL;
    }
    if(addr < MMAP_AREA_START || addr > MMAP_AREA_END) {
        return -EINVAL;
    }
    if(length < 0) {
        return -EINVAL;
    }
    if(prot != PROT_READ && prot != (PROT_READ | PROT_WRITE)) {
        return -EINVAL;
    }
    if(current->vm_area == NULL) {
        return -EINVAL;
    }
    struct vm_area *prev = current->vm_area;
    struct vm_area *curr = prev->vm_next;
    struct vm_area *new_node = NULL;
    struct vm_area *temp = NULL;

    if(length % FOUR_KB) {
        length = (length / FOUR_KB + 1) * FOUR_KB;
    }
    u64 protection_start = addr;
    u64 protection_end = addr + length;
    
    if(protection_start < MMAP_AREA_START + FOUR_KB) {
        protection_start = MMAP_AREA_START + FOUR_KB;
    }
    // if prot = 3, we need to pass 1 to the function (since bit has to be set to 1)
    // if prot = 1, we need to pass 0 to the function (since bit has to be set to 0)
    walk_v2p(protection_start, protection_end, current, 1, prot == 3 ? 1 : 0);
    while(curr != NULL) {
        if(curr->vm_start >= protection_end) {
            return 0; // protection of all eligible nodes has been changed
        }
        if(protection_start <= curr->vm_start) { //start of the node has to be changed
            if(curr->vm_end > protection_end) { // part of the node to be updated, new node would be created
                new_node = allocate_and_initialize_VMA(protection_end, curr->vm_end, curr->access_flags);
                new_node->vm_next = curr->vm_next;
                curr->vm_next = new_node;
                curr->vm_end = protection_end;
                curr->access_flags = prot;
                merge_and_update(prev, curr, new_node); // check if can be merged with prev (and new_node if PROT remains unchanged)
                return 0; // all specified regions have been considered
            }
            else { // entire node to be updated
                curr->access_flags = prot;
                int counter = merge_and_update(prev, curr, curr->vm_next);
                switch (counter) { // based on different merging scenarios
                    case 2:
                    case 3: { // merging has been performed with the left side => curr ptr has been lost
                        curr = prev;
                    }
                }
            }
        }
        else if(protection_start > curr->vm_start && protection_start < curr->vm_end) {
            if(protection_end < curr->vm_end) { // need to split into 3
                new_node = allocate_and_initialize_VMA(protection_end, curr->vm_end, curr->access_flags); // rightmost node
                new_node->vm_next = curr->vm_next;
                curr->vm_next = new_node;
                curr->vm_end = new_node->vm_start;
                // new_node merging needn't be considered as access_flag for this has not changed
                new_node = allocate_and_initialize_VMA(protection_start, protection_end, prot);
                new_node->vm_next = curr->vm_next;
                curr->vm_next = new_node;
                curr->vm_end = new_node->vm_start;
                // merging needs to be considered if PROT is the same
                merge_and_update(curr, new_node, new_node->vm_next);
                return 0;
            }
            else { // need to be split into 2
                new_node = allocate_and_initialize_VMA(protection_start, curr->vm_end, prot);
                new_node->vm_next = curr->vm_next;
                curr->vm_next = new_node;
                curr->vm_end = new_node->vm_start;
                merge_and_update(curr, new_node, new_node->vm_next);
            }
        }
        prev = curr;
        curr = prev->vm_next;
    }
    return 0;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    if(!current) {
        return -EINVAL;
    }
    if(addr!= 0 && (addr >= MMAP_AREA_END || addr <= MMAP_AREA_START)) {
        return -EINVAL;
    }
    if(flags != MAP_FIXED && flags != 0) {
        return -EINVAL;
    }
    if(prot != PROT_READ && prot != (PROT_READ | PROT_WRITE)) {
        return -EINVAL;
    }
    if(length <= 0 || length > 2 * 1024 * 1024) {
        return -EINVAL;
    }

    // Aligning length to a multiple of 4KB
    if(length % FOUR_KB) {
        length = (length / FOUR_KB + 1) * FOUR_KB;
    }
    if(addr + length > MMAP_AREA_END) { // this allocation cannot be done
        return -EINVAL;
    }
    
    // check if list exists, if not first declare the dummy node
    if(current->vm_area == NULL) {
        struct vm_area *VMA = allocate_and_initialize_VMA(MMAP_AREA_START, MMAP_AREA_START + FOUR_KB, 0x0);
        current->vm_area = VMA;
    }
    struct vm_area* prev = current->vm_area;
    struct vm_area* curr = prev->vm_next;
    struct vm_area* new_node = NULL;
    long allocated_pointer = 0;
    
    // Check based on flag -> if MAP_FIXED
    if(flags == MAP_FIXED) {
        // address is binding
        if(addr == 0) { //address cannot be NULL
            return -EINVAL;
        }

        while(curr != NULL && addr > curr->vm_start) {
            prev = curr;
            curr = curr->vm_next;
        }
        
        if(curr == NULL) {
            if(addr < prev->vm_end) {
                return -EINVAL;
            }
            // this is now the last node of the list
            new_node = allocate_and_initialize_VMA(addr, addr + length, prot);
        }
        else {
            if(addr < prev->vm_end || addr + length > curr->vm_start) {
                // some part of required chunk is already occupied
                return -EINVAL;
            }
            // entire chunk can be succesfully allocated
            new_node = allocate_and_initialize_VMA(addr, addr + length, prot);
        }
        prev->vm_next = new_node;
        new_node->vm_next = curr;

        // look at the possibility of merging
        allocated_pointer = new_node->vm_start; // this needs to be stored since new_node value can get updated during merge
        merge_and_update(prev, new_node, curr);
    }
    else {
        // address is only a hint, not binding
        if(addr == 0) {
            // allocate at the first available position
            allocated_pointer = find_valid_unmapped_region(current, length, prot); // it should add the node and consider merging
        }
        else {
            // some preference, not binding
            while(curr != NULL && addr > curr->vm_start) { // iterating through the list
                prev = curr;
                curr = curr->vm_next;
            }
            if(curr == NULL) { // addr is bigger than start of the last node
                if(addr < prev->vm_end) { 
                    // allocate at the first available position
                    allocated_pointer = find_valid_unmapped_region(current, length, prot); // it should add the node and consider merging
                }
                else {
                    new_node = allocate_and_initialize_VMA(addr, addr + length, prot); // this needs to be merged
                    prev->vm_next = new_node;
                    new_node->vm_next = curr;
                    allocated_pointer = new_node->vm_start;
                    merge_and_update(prev, new_node, curr);
                }
            }
            else { // addr lies somewhere in between the list
                if(addr < prev->vm_end || addr + length > curr->vm_start) {
                    // some part of required chunk is already occupied
                    // allocate at the first available position
                    allocated_pointer = find_valid_unmapped_region(current, length, prot);
                }
                else {
                    new_node = allocate_and_initialize_VMA(addr, addr + length, prot); // this needs to be merged
                    prev->vm_next = new_node;
                    new_node->vm_next = curr;
                    allocated_pointer = new_node->vm_start;
                    merge_and_update(prev, new_node, curr);
                }
            }
        }
    }
    return allocated_pointer;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if(!current) {
        return -EINVAL;
    }
    if(addr < MMAP_AREA_START || addr > MMAP_AREA_END) {
        return -EINVAL;
    }
    if(length < 0) {
        return -EINVAL;
    }
    if(current->vm_area == NULL) {
        return -EINVAL;
    }
    struct vm_area *prev = current->vm_area;
    struct vm_area *curr = prev->vm_next;
    struct vm_area *temp = NULL; // it will temporarily store curr->next whenever curr is freed

    // Aligning length to a multiple of 4KB
    if(length % FOUR_KB) {
        length = (length / FOUR_KB + 1) * FOUR_KB;
    }
    u64 deletion_start = addr;
    u64 deletion_end = addr + length;
    
    if(deletion_start < MMAP_AREA_START + FOUR_KB) {
        deletion_start = MMAP_AREA_START + FOUR_KB;
    }
    walk_v2p(deletion_start, deletion_end, current, 0, -1); // -1 since access_bit is irrelevant when called from unmap()

    while(curr != NULL) {
        if(curr->vm_start >= deletion_end) {
            return 0; // all required nodes have been deleted
        }
        if(deletion_start <= curr->vm_start) { //start of the node has to be changed 
            if(curr->vm_end <= deletion_end) { // entire node has to be deleted => next of prev also has to be updated
                temp = curr->vm_next;
                prev->vm_next = curr->vm_next;
                free_VMA(curr);
                curr = temp;
                continue; // since prev shouldn't be updated in this case
            }
            else { // part of the node to be deleted, start to be updated
                curr->vm_start = deletion_end;
            }
        }
        else if(deletion_start >= curr->vm_start && deletion_start < curr->vm_end) {
            if(curr->vm_end > deletion_end) { // splitting of node is required
                struct vm_area *new_node = allocate_and_initialize_VMA(deletion_end, curr->vm_end, curr->access_flags);
                new_node->vm_next = curr->vm_next;
                curr->vm_next = new_node;
            }
            curr->vm_end = deletion_start; // in either case when splitting required or not, need to update the end
        }
        prev = curr;
        curr = prev->vm_next;
    }

    return 0;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    // printk("518. Page fault called\n");
    if(!current || !osmap(current->pgd)) { // invalid process or PGD isn't valid
        return -EINVAL;
    }

    struct vm_area *vm = current->vm_area;
    int flag = 0;
    while(vm != NULL) {
        if(addr >= vm->vm_start && addr < vm->vm_end) {
            flag = 1;
            break;
        }
        vm = vm->vm_next;
    }
    if(flag == 0) {
        return -EINVAL; // no node corresponding to the addr
    }

    if(error_code == 0x7) {
        if(vm->access_flags == PROT_READ) {
            return -EINVAL;
        }
        else {
            return handle_cow_fault(current, addr, vm->access_flags);
        }
    }
    if(error_code != 0x4 && error_code != 0x6) {
        return -EINVAL;
    }

    if(error_code == 0x6 && vm->access_flags == PROT_READ) {
        return -EINVAL;
    }

    u64 offsets[4]; // this will store the offset values in the order that they appear in addr
    addr = addr >> 12; // removing initial 12 bits
    for(int i = 3; i >= 0; i--) {
        offsets[i] = addr & ((1 << 9) - 1);
        addr = addr >> 9;
    }

    u64 *CR3 =  NULL;
    u64* pte_table_address = NULL;
    u64 new_pfn = current->pgd;
    for(int i = 0; i < 4; i++) {
        CR3 = (u64*) osmap(new_pfn);
        pte_table_address = CR3 + offsets[i];
        if((*pte_table_address & 1) == 0) {
            // table entry is not present, must allocate it
            if(i == 3) {
                new_pfn = os_pfn_alloc(USER_REG);
            }
            else {
                new_pfn = os_pfn_alloc(OS_PT_REG);
            }
            *pte_table_address = *pte_table_address & (~(MASK)); // clearing all 32 bits in between to 0
            *pte_table_address = *pte_table_address | (new_pfn << 12); // setting the 32 bits to the new PFN entry
        }
        // setting present, access and read/write bits and extracting the pfn number for next pfn
        *pte_table_address = *pte_table_address | 0x1;
        if(i != 3) {
            *pte_table_address = *pte_table_address | 0x8; // other than last level decoding
        }
        else {
            *pte_table_address = *pte_table_address & (~(1 << 3)); // clearing the 4th bit
            *pte_table_address = *pte_table_address | (vm->access_flags << 3); // setting it to the access flag
        }
        *pte_table_address = *pte_table_address | 0x10;
        new_pfn = (*pte_table_address & MASK) >> 12;
    }
    return 1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */

long do_cfork()
{
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
     /* Do not modify above lines
     * 
     * */   
     /*--------------------- Your code [start]---------------*/
    if(!new_ctx || !ctx) {
        return -EINVAL;
    }
    pid = new_ctx->pid; // storing the value before copying
    memcpy(new_ctx, ctx, sizeof(struct exec_context));
    new_ctx->pid = pid;
    new_ctx->ppid = ctx->pid;

    int i; // used as iterators to copy content

    // copy the files
    for(i = 0; i < MAX_OPEN_FILES; i++) {
        if(ctx->files[i] != NULL) {
            new_ctx->files[i] = ctx->files[i];
            new_ctx->files[i]->ref_count++;
        }
    }

    new_ctx->pgd = os_pfn_alloc(OS_PT_REG);

    for(i = 0; i < MAX_MM_SEGS; i++) {
        if(i == MM_SEG_STACK) {
            copy_v2p(ctx->pgd, new_ctx->pgd, new_ctx->mms[i].start, new_ctx->mms[i].end);
        }
        else {
            copy_v2p(ctx->pgd, new_ctx->pgd, new_ctx->mms[i].start, new_ctx->mms[i].next_free);
        }
    }

    struct vm_area *curr = ctx->vm_area;
    struct vm_area *prev = NULL;
    struct vm_area *new_node = NULL;
    new_ctx->vm_area = NULL;
    while(curr != NULL) {
        // copying the vm_areas and the v2p mapping
        new_node = allocate_and_initialize_VMA(curr->vm_start, curr->vm_end, curr->access_flags);
        if(new_ctx->vm_area == NULL) {
            new_ctx->vm_area = new_node;
        }
        else {
            prev->vm_next = new_node;
        }
        copy_v2p(ctx->pgd, new_ctx->pgd, curr->vm_start, curr->vm_end);
        prev = new_node;
        curr = curr->vm_next;
    }
    /*--------------------- Your code [end] ----------------*/

    /*
    * The remaining part must not be changed
    */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}



/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    u32 access_bit = (access_flags - 1) / 2;

    if(!current) {
        return -EINVAL;
    }
    int i, offsets[4];
    u32 pfn = current->pgd;
    u64 *CR3, *pte_table_address;

    vaddr = vaddr >> 12;
    for(i = 3; i >= 0; i--) {
        offsets[i] = vaddr & ((1 << 9) - 1);
        vaddr = vaddr >> 9;
    }

    for(i = 0; i < 4; i++) {
        CR3 = (u64*) osmap(pfn);
        pte_table_address = CR3 + offsets[i];
        if((*pte_table_address & 1) == 0) {
            // entry is not present so virtual->physical mapping was not created, not a valid CoW fault
            return -1;
        }
        pfn = (*pte_table_address & (MASK)) >> 12;
    }
    if(get_pfn_refcount(pfn) == 1) {
        // change the access bit to the access bit passed as argument
        *pte_table_address = *pte_table_address & (~(1 << 3)); // clearing the 4th bit
        *pte_table_address = *pte_table_address | (access_bit << 3); // setting it to the access flag
    }
    else {
        // this is a CoW fault, copy to be created
        // decrement refcount of old pfn
        put_pfn(pfn);
        // create a new pfn and change the value in the pte_table_addr
        u32 copy_pfn = os_pfn_alloc(USER_REG);
        *pte_table_address = *pte_table_address & (~(MASK)); // clearing all 32 bits in between to 0
        *pte_table_address = *pte_table_address | (copy_pfn << 12); // setting the 32 bits to the new PFN entry
        // change the access bit to the access bit passed as argument
        *pte_table_address = *pte_table_address & (~(1 << 3)); // clearing the 4th bit
        *pte_table_address = *pte_table_address | (access_flags << 3); // setting it to the access flag
        // copy the contents from the old pfn to the new pfn
        memcpy(osmap(copy_pfn), osmap(pfn), FOUR_KB);
    }
    return 1;
}
