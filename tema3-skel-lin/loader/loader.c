#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "exec_parser.h"

static so_exec_t *exec;
static struct sigaction old_action;
static int page_size;
static int file_des;

static int get_segment(uintptr_t faulty)
{
	int result = -1;

	for (int i = 0; i < exec->segments_no; ++i) {
		int start_segment = exec->segments[i].vaddr;
		int end_segment = exec->segments[i].mem_size + start_segment;

		if (faulty >= start_segment && faulty < end_segment) {
			result = i;
			break;
		}
	}
	return result;
}

static void signal_handler(int signum, siginfo_t *info, void *context)
{
	char *p, *validate;
	uintptr_t page_fault_addr;
	int page_fault_segment;

	page_fault_addr = (uintptr_t)info->si_addr;

	page_fault_segment = get_segment(page_fault_addr);

	if (page_fault_segment == -1)
		old_action.sa_sigaction(signum, info, context);


	uintptr_t culprit =
	 (page_fault_addr - exec->segments[page_fault_segment].vaddr) /
	  page_size;

	validate = (char *)exec->segments[page_fault_segment].data;
	if (validate[culprit] != 1) {
		validate[culprit] = 1;

		int size_helper = page_size * culprit;
		int size_to_read =
		 size_helper + page_size -
		 exec->segments[page_fault_segment].file_size;
		int mem_size =
		 size_helper + page_size -
		 exec->segments[page_fault_segment].mem_size;

		if (size_to_read > 0) {
			size_to_read =
			 exec->segments[page_fault_segment].file_size -
			 size_helper;
			if (size_to_read < 0)
				size_to_read = 0;

		} else
			size_to_read = page_size;

		if (mem_size < 0)
			mem_size =
			 exec->segments[page_fault_segment].mem_size %
			 page_size;

		else
			mem_size = page_size;

		if (size_to_read != 0) {
			p = mmap(
			(void *)exec->segments[page_fault_segment].vaddr +
			 size_helper,
			 page_size,
			 PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			 -1,
			 0
			 );

			lseek(
			 file_des,
			 exec->segments[page_fault_segment].offset +
			 size_helper,
			 SEEK_SET
			 );
			read(file_des, p, size_to_read);

			if (mem_size - size_to_read > 0)
				memset(p + size_to_read,
				 0,
				 page_size - size_to_read);

			mprotect(
			 p,
			 page_size,
			 exec->segments[page_fault_segment].perm
			 );

		} else
			p = mmap(
			 (void *)exec->segments[page_fault_segment].vaddr +
			 size_helper,
			 page_size,
			 exec->segments[page_fault_segment].perm,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			 -1,
			 0
			 );
		return;
	}
	old_action.sa_sigaction(signum, info, context);
}

int so_init_loader(void)
{
	struct sigaction my_action;
	int check;

	my_action.sa_sigaction = signal_handler;
	sigemptyset(&my_action.sa_mask);
	sigaddset(&my_action.sa_mask, SIGSEGV);
	my_action.sa_flags = SA_SIGINFO;

	check = sigaction(SIGSEGV, &my_action, &old_action);
	if (check == -1)
		fprintf(stderr, "error in sigaction\n");

	return 0;
}

int so_execute(char *path, char *argv[])
{
	int i, number_of_pages;

	file_des = open(path, O_RDONLY);

	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	page_size = getpagesize();

	for (i = 0; i < exec->segments_no; i++) {
		number_of_pages = exec->segments[i].mem_size / page_size + 1;
		exec->segments[i].data = calloc(number_of_pages, sizeof(char));
	}

	so_start_exec(exec, argv);

	close(file_des);
	return 0;
}
