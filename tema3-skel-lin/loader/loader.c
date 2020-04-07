/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "exec_parser.h"

static so_exec_t *exec;
static struct sigaction def_action;
int page_size;
int file_des;
char *page_idx;

void signal_handler(int signum, siginfo_t *info, void *context) {
	void *address, *tmp;
	int faulty = -1;
	int number_of_pages;
	int culprit;
	int size_helper;

	// checking the signal
	if (signum != SIGSEGV) {
		def_action.sa_sigaction(signum, info, context);
		// return;
	}

	// getting the address at which there is a page fault
	address = info->si_addr;

	// there are multiple segments which must be checked for faults 
	for (int i = 0; i < exec->segments_no; ++i) {
		if (*(int *)address >= exec->segments[i].vaddr && 
			*(int *)address < (exec->segments[i].vaddr + exec->segments[i].mem_size)) {
			faulty = i;
		}
	}

	if (faulty != -1) {
		// calculating the page for the address, taking the segment into account
		number_of_pages = (exec->segments[faulty].mem_size) / page_size;
		culprit = ((int) address - exec->segments[faulty].vaddr) / page_size;

		// checking for memory left on the side
		if (exec->segments[faulty].mem_size % page_size) {number_of_pages++;}
		if (!exec->segments[faulty].data) {
			exec->segments[faulty].data = calloc(number_of_pages, 1);
		}

		if (((int)exec->segments[faulty].data + culprit) == 1) {
			def_action.sa_sigaction(signum, info, context);
		} else {
			size_helper = page_size * culprit;
			memset(exec->segments[faulty].data + culprit, 1, 1);
			tmp = mmap((void *)exec->segments[faulty].vaddr + size_helper, page_size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			lseek(file_des, (void *)exec->segments[faulty].offset + size_helper, SEEK_SET);
			read(file_des, exec->segments[faulty].vaddr + size_helper, page_size);
			mprotect(tmp, page_size, exec->segments[faulty].perm);
			memset((void *)exec->segments[faulty].vaddr + exec->segments[faulty].file_size, 0, exec->segments[faulty].mem_size - exec->segments[faulty].file_size);
			free(exec->segments[faulty].data);
		}
	} else {
		def_action.sa_sigaction(signum, info, context);
		return;
	}
		
}


int so_init_loader(void)
{
	/* TODO: initialize on-demand loader */
	struct sigaction my_action;

	my_action.sa_sigaction = signal_handler;
	// initializing action
	sigemptyset(&my_action.sa_mask);
	// setting signal
	sigaddset(&my_action.sa_mask, SIGSEGV);
	my_action.sa_flags = SA_SIGINFO;

	int check = sigaction(SIGSEGV, &my_action, &def_action);
	// DIE(check == -1, "SIGSEGV action");
	return 0;
}

int so_execute(char *path, char *argv[])
{
	// opening the file
	file_des = open(path, O_RDONLY);
	page_size = getpagesize();

	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	// load & exec
	so_start_exec(exec, argv);
	so_init_loader();
 	// closing the file after the job is done
 	close(file_des);
	return 0;
}