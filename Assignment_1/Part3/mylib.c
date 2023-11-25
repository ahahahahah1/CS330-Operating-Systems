#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FOUR_MB (unsigned long) (4 * 1024 * 1024)
void* head = NULL;

unsigned long Least_Int_Func(unsigned long size, unsigned long unit) { //least integer function (ceil() isn't working)
	if(size / unit == (double) size/unit) {
		return size / unit;
	}
	else {
		return (size / unit) + 1; 
	}
}

void *memalloc(unsigned long size) {
	if(size == 0) return NULL;

	if(size >= FOUR_MB) { 
		//new chunk of some multiple of 4MB to be created
		unsigned long multiple = Least_Int_Func(size + 8, FOUR_MB); //size with 8 bytes for storing the SIZE
		unsigned long REQUESTED_MEMORY = multiple * FOUR_MB;
		void* ptr = mmap(NULL, REQUESTED_MEMORY, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, 0, 0);
		if(ptr == MAP_FAILED) {
			perror("mmap");
			return NULL;
        }
		unsigned long *MEM_BLK_SIZE_PTR = ptr;
		*(MEM_BLK_SIZE_PTR) = (Least_Int_Func(size, 8) * 8) + 8; //add 8 for 8 bytes of size

		if(!(REQUESTED_MEMORY - *(MEM_BLK_SIZE_PTR) < 24)) {
			//split has to be created and added to the list of free memory
			unsigned long *NEXT_CHUNK_SIZE_PTR = MEM_BLK_SIZE_PTR + *(MEM_BLK_SIZE_PTR)/8;
			unsigned long remaining_size = REQUESTED_MEMORY - *(MEM_BLK_SIZE_PTR);
			*(NEXT_CHUNK_SIZE_PTR) = remaining_size;
			void **PREV = (void **)(NEXT_CHUNK_SIZE_PTR + 2);
			void **NEXT = (void **) (NEXT_CHUNK_SIZE_PTR + 1);
			*(PREV) = NULL;
			*(NEXT) = head;
			if(head != NULL) {
				unsigned long **PREV_FOR_HEAD = (unsigned long **)(head + 16); //moving 16 bytes ahead
				*(PREV_FOR_HEAD) = NEXT_CHUNK_SIZE_PTR;
			}
			head = (void *) NEXT_CHUNK_SIZE_PTR;
		}
		return (void *)(MEM_BLK_SIZE_PTR + 1);
	}

	unsigned long REQUIRED_MEM_SIZE = Least_Int_Func(size + 8, 8) * 8;
	if(REQUIRED_MEM_SIZE < 24) {
		REQUIRED_MEM_SIZE = 24;
	}

	//traverse list of all free blocks to check if any suitable
	void **current = head;
	while(current != NULL) {
		//check if any chunk works, if yes allocate and return
		//during allocation, check for padding requirements
		//if entire block to go, adjust the LL
		//if some portion of the block to go, split, then adjust the LL (new chunk at head of LL), update the size
		unsigned long *CURR_BLK_SIZE_PTR = (unsigned long *)current;
		if(*(CURR_BLK_SIZE_PTR) >= REQUIRED_MEM_SIZE) {
			void **NEXT = (void **) (CURR_BLK_SIZE_PTR + 1);
			void **PREV = (void **) (CURR_BLK_SIZE_PTR + 2);

			//check whether entire block can be allocated or split is required
			if(*(CURR_BLK_SIZE_PTR) - REQUIRED_MEM_SIZE < 24) {
				//no split required, only LL to be updated

				if(*(NEXT) != NULL) {
					void **NEXT_CHUNK = (void **) *(NEXT);
					NEXT_CHUNK += 2; //prev ptr of next chunk (sizeof(void **) = 8 so only need to add 2 not 16)
					*(NEXT_CHUNK) = *(PREV);
				}

				if(*(PREV) != NULL) {
					void **PREV_CHUNK = (void **) *(PREV);
					PREV_CHUNK += 1; //next ptr of prev chunk
					*(PREV_CHUNK) = *(NEXT);
				}
				else {
					head = *(NEXT); //head of the list to be updated since 1st node is being removed
				}

			}
			else {
				//split required + LL to be updated
				unsigned long *SPLIT_BLK_PTR = CURR_BLK_SIZE_PTR + REQUIRED_MEM_SIZE/8;
				*(SPLIT_BLK_PTR) = *(CURR_BLK_SIZE_PTR) - REQUIRED_MEM_SIZE;
				*(CURR_BLK_SIZE_PTR) = REQUIRED_MEM_SIZE;

				void **NEW_CHUNK_NEXT_PTR = (void **) (SPLIT_BLK_PTR + 1);
				void **NEW_CHUNK_PREV_PTR = (void **) (SPLIT_BLK_PTR + 2);
				*NEW_CHUNK_NEXT_PTR = *NEXT;
				*NEW_CHUNK_PREV_PTR = *PREV;
				
				if(*(NEXT) != NULL) {
					void **NEXT_CHUNK = (void **) *(NEXT);
					NEXT_CHUNK += 2; //prev ptr of next chunk
					*(NEXT_CHUNK) = (void *) SPLIT_BLK_PTR;
				}
				if(*(PREV) != NULL) {
					void **PREV_CHUNK = (void **) *(PREV);
					PREV_CHUNK += 1; //next ptr of prev chunk
					*(PREV_CHUNK) = (void *) SPLIT_BLK_PTR;
				}
				else { //chunk of first node is being removed thus head to be updated
					head = (void *) SPLIT_BLK_PTR; //head of the list to be updated since 1st node is being removed
				}
			}
			return (void *) (CURR_BLK_SIZE_PTR + 1);
		}
		current = (void **) *(current + 8); //go to the next free chunk
	}

	//none found, request chunk from mmap and allocate, add remaining portion (if any) to the list.
	void *ptr = mmap(NULL, FOUR_MB, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, 0, 0);
	if(ptr == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	unsigned long *NEW_BLK = (unsigned long *) ptr;
	if(!(FOUR_MB - REQUIRED_MEM_SIZE < 24)) { //need to split and add a chunk to free memory list
		*NEW_BLK = REQUIRED_MEM_SIZE;
		unsigned long *NEW_CHUNK = NEW_BLK + REQUIRED_MEM_SIZE/8;
		*(NEW_CHUNK) = FOUR_MB - REQUIRED_MEM_SIZE;
		void **NEW_CHUNK_NEXT_PTR = (void **)(NEW_CHUNK + 1);
		void **NEW_CHUNK_PREV_PTR = (void **)(NEW_CHUNK + 2);
		*(NEW_CHUNK_PREV_PTR) = NULL;
		*(NEW_CHUNK_NEXT_PTR) = head;
		head = (void *) NEW_CHUNK;
	}
	else {
		*NEW_BLK = FOUR_MB; //entire block allocated
	}
	return (void *) (NEW_BLK + 1);
}

int memfree(void *ptr)
{
	int prev_found = 0, next_found = 0;
	unsigned long *CURR = ptr - 8;
	void *free_list_node = head;
	
	while(free_list_node != NULL) {
		unsigned long FREE_LIST_NODE_SIZE = *(unsigned long *)free_list_node;
		void **NEXT = (void **)(free_list_node + 8);
		void **PREV = (void **)(free_list_node + 16);
		//check if left or right
		if(CURR + *CURR/8 == free_list_node) { // new memory chunk = CURR + free_list_node
			*CURR += FREE_LIST_NODE_SIZE;
			if(*NEXT != NULL) {
				void **NEXT_CHUNK = (void **) *(NEXT);
				NEXT_CHUNK += 2; //prev ptr of next chunk
				*(NEXT_CHUNK) = (void *) *PREV;
			}
			if(*(PREV) != NULL) {
				void **PREV_CHUNK = (void **) *(PREV);
				PREV_CHUNK += 1; //next ptr of prev chunk
				*(PREV_CHUNK) = (void *) *NEXT;
			}
			next_found = 1;
			if(free_list_node == head) { //temporarily reallocate head to head->next so that at the end things become okay
				head = *(NEXT);
			}
		}
		else if(free_list_node + FREE_LIST_NODE_SIZE == CURR) { // new memory chunk = free_list_node + CURR
			unsigned long *FREE_LIST_NODE_PTR = (unsigned long *)free_list_node;
			*FREE_LIST_NODE_PTR += *CURR;
			CURR = FREE_LIST_NODE_PTR;

			if(*NEXT != NULL) {
				void **NEXT_CHUNK = (void **) *(NEXT);
				NEXT_CHUNK += 2; //prev ptr of next chunk
				*(NEXT_CHUNK) = *(PREV);
			}
			if(*(PREV) != NULL) {
				void **PREV_CHUNK = (void **) *(PREV);
				PREV_CHUNK += 1; //next ptr of prev chunk
				*(PREV_CHUNK) = (void *) *NEXT;
			}
			prev_found = 1;
		}

		if(prev_found && next_found) {
			break;
		}
		void **free_list_node_next = (void **) (free_list_node + 8);
		free_list_node = *free_list_node_next;
	}

	void **NEW_NEXT = (void **)(CURR + 1);
	void **NEW_PREV = (void **)(CURR + 2);
	*NEW_PREV = NULL;
	*NEW_NEXT = head;
	if(head != NULL) {
		void **PREV_OF_HEAD = (void **)(head + 16);
		*PREV_OF_HEAD = CURR;
	}
	head = CURR;
	if(*NEW_NEXT == head) {
		*NEW_NEXT = NULL;
	}
	if(*NEW_PREV == head) {
		*NEW_PREV = NULL;
	}
	return 0;
}