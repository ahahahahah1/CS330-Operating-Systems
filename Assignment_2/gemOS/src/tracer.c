#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////



int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
	// ARGS:
	// access_bit follows R=1, W=2, X=4, S=8
	// RETURNS :
	// -EINVAL on invalid access
	// 0 on valid access

	if(!buff) { // points to NULL
		return -EINVAL;
	}
	unsigned long start_addr, end_addr;
	start_addr = buff;
	end_addr = buff + count - 1;
	int i; 		// i is for iterating

	struct exec_context* ctx = get_current_ctx();
	if(!ctx) { // Invalid process
		return -EINVAL;
	}
	struct mm_segment* mms = ctx->mms;
	for(i = 0; i < MAX_MM_SEGS; i++) { // checking if buff lies in mm_segment
		if(i != MM_SEG_STACK) { // checking if buff lies in region other than stack
			if(start_addr >= mms[i].start && end_addr <= mms[i].next_free - 1) {
				if(access_bit & ctx->mms[i].access_flags) { // success
					return 0;
				}
				else { // incorrect access
					return -EINVAL;
				}
			}
		}
		else { 		// checking if buff lies in stack
			if(start_addr >= ctx->mms[i].start && end_addr <= ctx->mms[i].end - 1) {
				if(access_bit & ctx->mms[i].access_flags) { 
					return 0;
				}
				else { // incorrect access
					return -EINVAL;
				}
			}
		}
	}
	// printk("Finished checking in mms\n");
	struct vm_area *vm_segs = ctx->vm_area;
	// printk("Initialized vm_segs\n");
	while(vm_segs != NULL) {
		// printk("At the beginning of the loop\n");
		// printk("For current segment, vm_start = %x\n", vm_segs->vm_start);
		// printk("and vm_end = %x\n", vm_segs->vm_end - 1);
		if(start_addr >= vm_segs->vm_start && end_addr <= vm_segs->vm_end - 1) {
			// printk("Found a valid region, checking access flags\n");
			if(access_bit & vm_segs->access_flags) {
				return 0;
			}
			else { // incorrect access
				return -EINVAL;
			}
		}
		// printk("At the end of body of loop\n");
		vm_segs = vm_segs->vm_next;
	}
	return -EINVAL; // not found in any memory region
}

long trace_buffer_close(struct file *filep)
{
	//TODO: Ask sir about the freeing process (part 2 testcase4) error
	if(filep == NULL || filep->type != TRACE_BUFFER || filep->trace_buffer == NULL) {
		return -EINVAL;
	}
	if(filep->trace_buffer->buffer == NULL || filep->inode != NULL) {
		return -EINVAL;
	}

	struct trace_buffer_info* trace_buffer = filep->trace_buffer;
	// printk("In trace buffer close, before freeing, mode = %d\n", trace_buffer->mode);
	os_page_free(USER_REG, trace_buffer->buffer);
	os_free(trace_buffer, sizeof(struct trace_buffer_info));
	// printk("In trace buffer close, after freeing, mode = %d\n", trace_buffer->mode);
	os_free(filep->fops, sizeof(struct fileops));
	os_free(filep, sizeof(struct file));
	// filep = NULL;
	return 0;
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER || filep->trace_buffer == NULL) {
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}
	if(filep->trace_buffer->buffer == NULL) {
		return -EINVAL;
	}

	if(filep->mode != O_READ && filep->mode != O_RDWR) {
		return -EINVAL;
	}
	if(is_valid_mem_range((unsigned long) buff, count, 2) == -EINVAL) {
		return -EBADMEM;
	}
	
	struct trace_buffer_info* trace_buffer = filep->trace_buffer;
	for(int bytes_read = 0; bytes_read < count; bytes_read++) {
		if(trace_buffer->unused_size == TRACE_BUFFER_MAX_SIZE) {
			//reached max capacity for write
			return bytes_read;
		}
		buff[bytes_read] = trace_buffer->buffer[trace_buffer->RD_OFFSET];
		trace_buffer->RD_OFFSET = (trace_buffer->RD_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
		trace_buffer->unused_size++;
	}
	return count;
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER || filep->trace_buffer == NULL) {
		return -EINVAL;
	}
	if(count < 0) {
		return -EINVAL;
	}
	if(filep->trace_buffer->buffer == NULL) {
		return -EINVAL;
	}

	if(filep->mode != O_WRITE && filep->mode != O_RDWR) {
		return -EINVAL;
	}
	if(is_valid_mem_range((unsigned long) buff, count, 1) == -EINVAL) {
		return -EBADMEM;
	}

	struct trace_buffer_info* trace_buffer = filep->trace_buffer;

	for(int bytes_written = 0; bytes_written < count; bytes_written++) {
		if(!trace_buffer->unused_size) {
			//reached max capacity for write
			return bytes_written;
		}
		trace_buffer->buffer[trace_buffer->WR_OFFSET] = buff[bytes_written];
		trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
		trace_buffer->unused_size--;
	}
	return count;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	int fd = 0;
	struct file** FD_table_array = current->files;

	if(mode != O_READ && mode != O_WRITE && mode != O_RDWR) {
		return -EINVAL;
	}

	while(fd < MAX_OPEN_FILES) {
		if(FD_table_array[fd] == NULL) 
			break;
		fd++;
	}
	if(fd == MAX_OPEN_FILES) {
		return -EINVAL;
	}
	
	struct file* new_trace_buffer = (struct file*) os_alloc(sizeof(struct file));
	if(!new_trace_buffer) {
		return -ENOMEM;
	}
	FD_table_array[fd] = new_trace_buffer;
	
	//initializing the members of new file object
	new_trace_buffer->type = TRACE_BUFFER;
	new_trace_buffer->mode = mode;
	new_trace_buffer->offp = 0;
	new_trace_buffer->ref_count = 1;
	new_trace_buffer->inode = NULL;
	
	struct fileops* FUNC_MAP = (struct fileops*) os_alloc(sizeof(struct fileops));
	if(!FUNC_MAP) {
		return -ENOMEM;
	}
	FUNC_MAP->read = trace_buffer_read;
	FUNC_MAP->write = trace_buffer_write;
	FUNC_MAP->close = trace_buffer_close;
	FD_table_array[fd]->fops = FUNC_MAP;
	// FUNC_MAP->lseek = NULL;

	struct trace_buffer_info* new_trace_buffer_info = (struct trace_buffer_info*) os_alloc(sizeof(struct trace_buffer_info));
	if(!new_trace_buffer_info) {
		return -ENOMEM;
	}

	new_trace_buffer->trace_buffer = new_trace_buffer_info;

	// initializing the members of trace_buffer_info
	new_trace_buffer_info->mode = mode;
	new_trace_buffer_info->RD_OFFSET = 0;
	new_trace_buffer_info->WR_OFFSET = 0;
	new_trace_buffer_info->unused_size = TRACE_BUFFER_MAX_SIZE;
	new_trace_buffer_info->buffer = (char*) os_page_alloc(USER_REG);
	if(!new_trace_buffer_info->buffer) {
		return -ENOMEM;
	}
	return fd;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////



int find_num_params(u64 syscall_num) {
	if(
		syscall_num == SYSCALL_GETPID ||
	   	syscall_num == SYSCALL_GETPPID ||
		syscall_num == SYSCALL_FORK || 
		syscall_num == SYSCALL_VFORK || 
		syscall_num == SYSCALL_CFORK ||
		syscall_num == SYSCALL_PHYS_INFO || 
		syscall_num == SYSCALL_STATS || 
		syscall_num == SYSCALL_GET_USER_P ||
		syscall_num == SYSCALL_GET_COW_F
	) {
		return 0;
	}
	else if(
		syscall_num == SYSCALL_EXIT ||
		syscall_num == SYSCALL_CONFIGURE ||
		syscall_num == SYSCALL_DUMP_PTT ||
		syscall_num == SYSCALL_SLEEP ||
		syscall_num == SYSCALL_PMAP ||
		syscall_num == SYSCALL_DUP ||
		syscall_num == SYSCALL_CLOSE ||
		syscall_num == SYSCALL_TRACE_BUFFER	
	) {
		return 1;
	}
	else if(
		syscall_num == SYSCALL_SIGNAL ||
		syscall_num == SYSCALL_EXPAND ||
		syscall_num == SYSCALL_CLONE ||
		syscall_num == SYSCALL_MUNMAP ||
		syscall_num == SYSCALL_OPEN ||
		syscall_num == SYSCALL_DUP2 ||
		syscall_num == SYSCALL_STRACE
	) {
		return 2;
	}
	else if(
		syscall_num == SYSCALL_MPROTECT ||
		syscall_num == SYSCALL_READ ||
		syscall_num == SYSCALL_WRITE ||
		syscall_num == SYSCALL_LSEEK ||
		syscall_num == SYSCALL_READ_STRACE ||
		syscall_num == SYSCALL_READ_FTRACE
	) {
		return 3;
	}
	else if(
		syscall_num == SYSCALL_MMAP ||
		syscall_num == SYSCALL_FTRACE
	) {
		return 4;
	}
	else {
		return -EINVAL; // invalid syscall number
	}
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	if(syscall_num == SYSCALL_END_STRACE || syscall_num == SYSCALL_START_STRACE) { // should not be traced
		return 0;
	}
	struct exec_context* current = get_current_ctx();
	if(!current || !current->st_md_base) { // invalid context or start_strace() has not been called
		return 0;
	}
	if(!current->st_md_base->is_traced) {
		return 0; // currently not tracing syscalls!
	}
	if(current->files[current->st_md_base->strace_fd] == NULL || current->files[current->st_md_base->strace_fd]->type != TRACE_BUFFER) { // invalid fd
		return 0;
	}
	int found_node_flag = 0;
	struct strace_head* head = current->st_md_base;
	int NUM_PARAMS = find_num_params(syscall_num);
	u64 param_args[5] = {syscall_num, param1, param2, param3, param4};
	struct file* filep = current->files[head->strace_fd];

	switch(head->tracing_mode) {
		case FULL_TRACING: {
			if(NUM_PARAMS != -EINVAL) { // valid syscall num
				found_node_flag = 1;
			}
			// valid syscall_num, to be pushed onto the trace_buffer
		}
		break;

		case FILTERED_TRACING: {
			struct strace_info* list_node = head->next;
			while(list_node != NULL) {
				if(list_node->syscall_num == syscall_num) {
					// valid syscall_num, to be pushed onto the trace_buffer
					found_node_flag = 1;
					break;
				}
				list_node = list_node->next;
			}
		}
		break;
	}
	
	if(found_node_flag) { // create a new entry in the trace_buffer for this syscall
		int bytes_written;
		struct trace_buffer_info* trace_buffer = filep->trace_buffer;

		for(int i = 0; i < NUM_PARAMS + 1; i++) {
			for(bytes_written = 0; bytes_written < (sizeof(u64)); bytes_written++) {
				if(!trace_buffer->unused_size) {
					//reached max capacity for write
					return bytes_written;
				}
				trace_buffer->buffer[trace_buffer->WR_OFFSET] = ((char*)(param_args + i))[bytes_written];
				trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
				trace_buffer->unused_size--;
			}
			if(bytes_written != sizeof(u64)) {
				return 0;
			}
		}
	}
	return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	//TODO: Run cases about list for adding and removing
	// ASSUMPTIONS:
	// 1. st_md_base->count stores the length of the traced list (irrelevant in FULL_TRACING)
	if(!current) { // invalid process
		return -EINVAL;
	}
	if(action != ADD_STRACE && action != REMOVE_STRACE) { // invalid action
		return -EINVAL;
	}
	if(current->st_md_base == NULL) { // start_strace has not been called! Or end_strace was called
		current->st_md_base = os_alloc(sizeof(struct strace_head));
		if(!current->st_md_base) {
			return -EINVAL;
		}
		current->st_md_base->count = 0;
		current->st_md_base->is_traced = 0;
		current->st_md_base->next = current->st_md_base->last = NULL;
		current->st_md_base->strace_fd = current->st_md_base->tracing_mode = -EINVAL;
	}
	struct strace_head* head = current->st_md_base;
	
	// checking if syscall_num is valid
	int num_args = find_num_params(syscall_num);
	if(num_args == -EINVAL) {
		return -EINVAL; // invalid syscall num / start_strace or end_strace called - not to be traced
	}

	if(head->tracing_mode == FULL_TRACING) { // Not filtered tracing!
		if(action == ADD_STRACE) {
			return 0; // We are trying to add to FULL_TRACING, not exactly an error
		}
		return -EINVAL; // We are trying to remove from FULL_TRACING mode which is an error case
	}
	if(head->count >= STRACE_MAX && action == ADD_STRACE) {
		return -EINVAL; // Already at max capacity!
	}

	switch(action) {
		case ADD_STRACE: {
			struct strace_info* strace_list = head->next;
			while(strace_list != NULL) { // checking if this syscall is already in list
				if(strace_list->syscall_num == syscall_num) {
					return -EINVAL;
				}
				strace_list = strace_list->next;
			}
			struct strace_info* new_node = os_alloc(sizeof(struct strace_info));
			if(!new_node) {
				return -EINVAL;
			}
			new_node->syscall_num = syscall_num;
			new_node->next = NULL;
			head->count++;

			if(!head->next) { // first node of the list
				head->next = new_node;
			}
			else { // list already exists
				head->last->next = new_node;
			}
			head->last = new_node;
			return 0;
		}

		case REMOVE_STRACE: {
			struct strace_info* prev_node = NULL;
			struct strace_info* current_node = head->next; // first node of the list
			while(current_node != NULL) {
				if(current_node->syscall_num == syscall_num) { // found the node
					if(prev_node) {
						prev_node->next = current_node->next;
					}
					else {
						head->next = current_node->next;
					}
					if(current_node == head->last) {
						head->last = prev_node;
					}
					current_node->next = NULL;
					current_node->syscall_num = 0;
					os_free(current_node, sizeof(struct strace_info));
					head->count--;
					return 0;
				}
				prev_node = current_node;
				current_node = current_node->next;
			}
			return -EINVAL; // node with given syscall not found!
		}
	}
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	if(filep == NULL || filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if(filep->trace_buffer == NULL || filep->trace_buffer->buffer == NULL) {
		return -EINVAL;
	}
	if(count < 0) {
		// malicious user, tmkc
		return -EINVAL;
	}
	int NUM_PARAMS = 0;
	u32 k; // bytes read
	u32 bytes_read = 0;
	u64 syscall_num;

	for(int i = 0; i < count; i++) {
		k = trace_buffer_read(filep, (buff + bytes_read), sizeof(u64));
		if(k != sizeof(u64)) { // completely finished reading the trace_buffer
			return bytes_read;
		}
		syscall_num = *(u64*)(buff + bytes_read);
		// printk("syscall_num = %d\n", syscall_num);
		bytes_read += k;
		NUM_PARAMS = find_num_params(syscall_num);
		// printk("argc = %d\n", NUM_PARAMS);

		for(int j = 0; j < NUM_PARAMS; j++) {
			k = trace_buffer_read(filep, (buff + bytes_read), sizeof(u64));
			if(k != sizeof(u64)) {
				return -EINVAL;
			}
			bytes_read += k;
		}
	}
	return bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	// ASSUMPTION : FD contains a valid trace buffer.
	// TODO: Implement forking based calls
	if(tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING) { // invalid tracking_mode
		return -EINVAL;
	}
	if(current == NULL || fd < 0) { // invalid process
		return -EINVAL;
	} 
	if(current->files[fd] == NULL || current->files[fd]->type != TRACE_BUFFER) { // invalid fd
		return -EINVAL;
	}
	struct strace_head* head = NULL;
	if(current->st_md_base != NULL) { // strace has been called previously for the same process!
		head = current->st_md_base;
	}
	else {
		head = os_alloc(sizeof(struct strace_head));
		if(!head) { // unsuccessful memory allocation
			return -EINVAL;
		}
		current->st_md_base = head;
		head->next = head->last = NULL;
	}
	head->count = 0;
	head->is_traced = 1; // syscalls are being traced
	head->strace_fd = fd;
	head->tracing_mode = tracing_mode;
	// head->next = head->last = NULL;
	return 0;
}

int sys_end_strace(struct exec_context *current)
{
	if(current == NULL || current->st_md_base == NULL) { // invalid context / start_strace has never been called!
		return -EINVAL;
	}
	if(current->files[current->st_md_base->strace_fd] == NULL || current->files[current->st_md_base->strace_fd]->type != TRACE_BUFFER) {
		// file in FD table is invalid / file type is not trace buffer
		return -EINVAL;
	}
	struct strace_head* head = current->st_md_base;
	struct strace_info* current_node = head->next;
	struct strace_info* next_list_node = NULL;

	while(current_node != NULL) {
		next_list_node = current_node->next;
		os_free(current_node, sizeof(struct strace_info));
		current_node = next_list_node;
	}
	// os_free(head, sizeof(struct strace_head)); 
	head->count = 0;
	head->is_traced = 0;
	head->strace_fd = -EINVAL;
	head->tracing_mode = -EINVAL;
	head->next = head->last = NULL;
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////



long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	if(!ctx || fd_trace_buffer < 0) {
		return -EINVAL; // invalid process
	}
	if(ctx->files[fd_trace_buffer] == NULL || ctx->files[fd_trace_buffer]->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if(ctx->files[fd_trace_buffer]->trace_buffer->buffer == NULL) {
		return -EINVAL;
	} 
	
	switch(action) {
		case ADD_FTRACE: {
			if(ctx->ft_md_base == NULL) { // no node in the list yet
				ctx->ft_md_base = os_alloc(sizeof(struct ftrace_head));
				if(!ctx->ft_md_base) {
					return -EINVAL; // error in allocating memory
				}
				ctx->ft_md_base->count = 0;
				ctx->ft_md_base->next = ctx->ft_md_base->last = NULL; 
			}
			struct ftrace_head* head = ctx->ft_md_base;

			if(head->count >= FTRACE_MAX) {
				return -EINVAL; // already at max capacity
			}
			struct ftrace_info* current_node = head->next; // first node of the list
			while(current_node != NULL) { // search if address already exists in the list
				if(current_node->faddr == faddr) {
					return -EINVAL;
				}
				current_node = current_node->next;
			}
			// doesn't exist in list already
			struct ftrace_info* new_node = os_alloc(sizeof(struct ftrace_info));
			if(!new_node) {
				return -EINVAL;
			}
			new_node->capture_backtrace = 0;
			for(int i = 0; i < 4; i++) {
				new_node->code_backup[i] = INV_OPCODE;
			}
			new_node->faddr = faddr;
			new_node->num_args = nargs;
			new_node->fd = fd_trace_buffer;
			new_node->next = NULL;
			
			// attaching this new node to the list
			head->count++;
			if(!head->next) {
				head->next = new_node;
			}
			else {
				head->last->next = new_node;
			}
			head->last = new_node;
		}
		break;
		case REMOVE_FTRACE: {
			if(ctx->ft_md_base == NULL) {
				return -EINVAL;
			}
			int faddr_present = 0;
			struct ftrace_info* list_node = ctx->ft_md_base->next;
			struct ftrace_info* prev_node = NULL;
			while(list_node != NULL) { // search if address already exists in the list
				if(list_node->faddr == faddr) {
					faddr_present = 1;
					break;
				}
				prev_node = list_node;
				list_node = list_node->next;
			}
			if(!faddr_present) {
				return -EINVAL; // function not in list added!
			}
			if(list_node->code_backup[0] != INV_OPCODE) {
				// tracing is enabled, need to disable it first
				u8* first_instr_ptr = (u8*) list_node->faddr;
				for(int i = 0; i < 4; i++) { // manipulating the address space
					*(first_instr_ptr + i) = list_node->code_backup[i];
					list_node->code_backup[i] = INV_OPCODE;
				}
			}
			// tracing is disabled, now updating the traced list
			if(!prev_node) { // first node of list is being deleted
				ctx->ft_md_base->next = list_node->next;
			}
			else {
				prev_node->next = list_node->next;
			}
			if(ctx->ft_md_base->last == list_node) { // last node is being deleted
				ctx->ft_md_base->last = prev_node;
			}
			// can delete the node now
			ctx->ft_md_base->count--;
			list_node->next = NULL;
			list_node->faddr = 0;
			os_free(list_node, sizeof(struct ftrace_info));
		}
		break;
		case ENABLE_FTRACE: {
			if(ctx->ft_md_base == NULL) {
				return -EINVAL;
			}
			int faddr_present = 0;
			struct ftrace_info* list_node = ctx->ft_md_base->next;
			while(list_node != NULL) { // search if address already exists in the list
				if(list_node->faddr == faddr) {
					faddr_present = 1;
					break;
				}
				list_node = list_node->next;
			}
			if(!faddr_present) {
				return -EINVAL; // function not in list added!
			}
			if(list_node->code_backup[0] != INV_OPCODE) { 
				return 0; // tracing was already enabled!
			}
			u8* first_instr_ptr = (u8*) list_node->faddr;
			for(int i = 0; i < 4; i++) { // manipulating the address space
				list_node->code_backup[i] = *(first_instr_ptr + i);
				*(first_instr_ptr + i) = INV_OPCODE;
			}
		}
		break;
		case DISABLE_FTRACE: {
			if(ctx->ft_md_base == NULL) {
				return -EINVAL;
			}
			int faddr_present = 0; // flag to check whether function already exists
			struct ftrace_info* list_node = ctx->ft_md_base->next;
			while(list_node != NULL) { // search if address already exists in the list
				if(list_node->faddr == faddr) {
					faddr_present = 1;
					break;
				}
				list_node = list_node->next;
			}
			if(!faddr_present) {
				return -EINVAL; // function not in list added!
			}
			if(list_node->code_backup[0] == INV_OPCODE) { 
				return 0; // tracing is already disabled!
			}
			u8* first_instr_ptr = (u8*) list_node->faddr;
			for(int i = 0; i < 4; i++) { // manipulating the address space
				*(first_instr_ptr + i) = list_node->code_backup[i];
				list_node->code_backup[i] = INV_OPCODE;
			}
		}
		break;
		case ENABLE_BACKTRACE: {
			if(ctx->ft_md_base == NULL) { // list doesn't exist!
				return -EINVAL;
			}
			int faddr_present = 0; // flag to check whether function already exists
			struct ftrace_info* list_node = ctx->ft_md_base->next;
			while(list_node != NULL) { // search if address already exists in the list
				if(list_node->faddr == faddr) {
					faddr_present = 1;
					break;
				}
				list_node = list_node->next;
			}
			if(!faddr_present) {
				return -EINVAL; // function not in list added!
			}
			u8* first_instr_ptr = (u8*) list_node->faddr;
			if(list_node->code_backup[0] == INV_OPCODE) {
				// we enable ftrace for the function!
				for(int i = 0; i < 4; i++) { // manipulating the address space
					list_node->code_backup[i] = *(first_instr_ptr + i);
					*(first_instr_ptr + i) = INV_OPCODE;
				}
			}
			list_node->capture_backtrace = 1; // enabling backtracing
		}
		break;
		case DISABLE_BACKTRACE: {
			if(ctx->ft_md_base == NULL) {
				return -EINVAL;
			}
			int faddr_present = 0; // flag to check whether function already exists
			struct ftrace_info* list_node = ctx->ft_md_base->next;
			while(list_node != NULL) { // search if address already exists in the list
				if(list_node->faddr == faddr) {
					faddr_present = 1;
					break;
				}
				list_node = list_node->next;
			}
			if(!faddr_present) {
				return -EINVAL; // function not in list added!
			}
			if(list_node->code_backup[0] != INV_OPCODE) {
				// we first need to disable ftrace for the function
				u8* first_instr_ptr = (u8*) list_node->faddr;
				for(int i = 0; i < 4; i++) { // manipulating the address space
					*(first_instr_ptr + i) = list_node->code_backup[i];
					list_node->code_backup[i] = INV_OPCODE;
				}
			}
			list_node->capture_backtrace = 0;
		}
		break;
		default: { // not a valid action
			return -EINVAL;
		}
	}
    return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
	// printk("Handle ftrace fault is called!\n");
	// printk("First argument: %d, second argument: %d\n", regs->rdi, regs->rsi);
	struct exec_context* current = get_current_ctx();
	if(!current || !current->ft_md_base) {
		return -EINVAL;
	}
	struct ftrace_info* list_node = current->ft_md_base->next;

	while(list_node != NULL) {
		if(list_node->faddr == regs->entry_rip) {
			break;
		}
		list_node = list_node->next;
	}
	if(!list_node) {
		return -EINVAL;
	}

	// Now we push required contents of the function to the trace_buffer
	int N_PARAMS = list_node->num_args;
	int bytes_written, fd = list_node->fd;
	u64 param_args[7] = {regs->entry_rip, regs->rdi, regs->rsi, regs->rdx, regs->rcx, regs->r8, regs->r9};
	
	if(fd < 0 || current->files[fd] == NULL) {
		return -EINVAL;
	}
	if(current->files[fd]->type != TRACE_BUFFER || current->files[fd]->trace_buffer == NULL) {
		return -EINVAL;
	}
	struct trace_buffer_info* trace_buffer = current->files[fd]->trace_buffer;

	for(int i = 0; i < N_PARAMS + 1; i++) {
		for(bytes_written = 0; bytes_written < (sizeof(u64)); bytes_written++) {
			if(!trace_buffer->unused_size) {
				//reached max capacity for write
				return 0;
			}
			trace_buffer->buffer[trace_buffer->WR_OFFSET] = ((char*)(param_args + i))[bytes_written];
			trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			trace_buffer->unused_size--;
		}
		if(bytes_written != sizeof(u64)) {
			return -EINVAL;
		}
	}

	if(list_node->capture_backtrace) {
		// backtracing is enabled
		for(bytes_written = 0; bytes_written < (sizeof(u64)); bytes_written++) { // writing the addr of 1st instruction of function
			if(!trace_buffer->unused_size) {
				//reached max capacity for write
				return 0;
			}
			trace_buffer->buffer[trace_buffer->WR_OFFSET] = ((char*)(param_args))[bytes_written];
			trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			trace_buffer->unused_size--;
		}
		if(bytes_written != sizeof(u64)) {
			return -EINVAL;
		}
		
		// now we push the return addresses
		u64 RAX = *(u64*)regs->entry_rsp; // storing the return address
		u64 RBP = regs->rbp; // storing the base pointer

		while(RAX != END_ADDR) {
			for(bytes_written = 0; bytes_written < (sizeof(u64)); bytes_written++) {
			if(!trace_buffer->unused_size) {
				//reached max capacity for write
				return 0;
			}
			trace_buffer->buffer[trace_buffer->WR_OFFSET] = ((char*)(&RAX))[bytes_written];
			trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			trace_buffer->unused_size--;
			}
			if(bytes_written != sizeof(u64)) {
				return -EINVAL;
			}
			RAX = *(u64*)(RBP + sizeof(u64));
			RBP = *(u64*)RBP;
		}
		// at the end of this loop, RAX should contain END_ADDR
		for(bytes_written = 0; bytes_written < (sizeof(u64)); bytes_written++) {
			if(!trace_buffer->unused_size) {
				//reached max capacity for write
				return 0;
			}
			trace_buffer->buffer[trace_buffer->WR_OFFSET] = ((char*)(&RAX))[bytes_written];
			trace_buffer->WR_OFFSET = (trace_buffer->WR_OFFSET + 1) % TRACE_BUFFER_MAX_SIZE;
			trace_buffer->unused_size--;
		}
		if(bytes_written != sizeof(u64)) {
			return -EINVAL;
		}
	}

	// executing first 2 instructions of the function in handle_ftrace itself
	// first instruction: "push %rbp" (this instruction is of 1 byte)
	regs->entry_rsp -= 8; // creating space on the stack
	u64* RSP = (u64*) regs->entry_rsp;
	*RSP = regs->rbp;

	// second instruction: "mov %rbp, %rsp" (this instruction is of 3 bytes)
	regs->rbp = regs->entry_rsp;

	// now increasing the instruction pointer to the third instruction (4 bytes ahead) of the function for normal functionality
	regs->entry_rip += 4;
	return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if(!filep || filep->type != TRACE_BUFFER) {
		return -EINVAL;
	}
	if(filep->trace_buffer->buffer == NULL) {
		return -EINVAL;
	}
	struct exec_context* current = get_current_ctx();
	if(!current) {
		return -EINVAL;
	}
	if(current->ft_md_base == NULL || current->ft_md_base->next == NULL) {
		return -EINVAL;
	}
	struct ftrace_head* head = current->ft_md_base;
	struct ftrace_info* list_node = NULL;
	u32 NUM_PARAMS = 0, k = 0, bytes_read = 0;
	u64 faddr;

	for(int i = 0; i < count; i++) {
		k = trace_buffer_read(filep, (buff + bytes_read), sizeof(u64));
		if(k != sizeof(u64)) {
			return bytes_read;
		}
		
		// we now find the number of arguments of the function
		faddr = *(u64*)(buff + bytes_read);
		bytes_read += sizeof(u64);
		list_node = head->next;
		while(list_node != NULL) {
			if(list_node->faddr == faddr) {
				break;
			}
			list_node = list_node->next;
		}
		if(!list_node) {
			return -EINVAL;
		}
		NUM_PARAMS = list_node->num_args;

		for(int j = 0; j < NUM_PARAMS; j++) {
			k = trace_buffer_read(filep, (buff + bytes_read), sizeof(u64));
			if(k != sizeof(u64)) {
				return -EINVAL;
			}
			bytes_read += sizeof(u64);
		}
		if(list_node->capture_backtrace) {
			int end_flag = 0;
			do {
				k = trace_buffer_read(filep, (buff + bytes_read), sizeof(u64));
				if(k != sizeof(u64)) {
					return -EINVAL;
				}
				if(*(u64*)(buff + bytes_read) == END_ADDR) { // need to undo this address since needn't return to user space
					end_flag = 1;
					for(int j = 0; j < sizeof(u64); j++) {
						buff[bytes_read + j] = 0;
					}
				}
				else {
					bytes_read += sizeof(u64);
				}
			} 
			while(end_flag != 1);
		}
	}
	return bytes_read;
}
