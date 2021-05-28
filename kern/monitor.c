// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

// LAB 1: add your command to here...
static struct Command commands[] = {
	{"help", "Display this list of commands", mon_help},
	{"kerninfo", "Display information about the kernel", mon_kerninfo},
	{"backtrace", "Display detailed information about the kernel", mon_backtrace},
	{"showmappings", "Display physical page mappings for range of VA", showmappings},
	{"setperm", "Set permissions on a specific VA. Usage: setperm 0xVA [P|U|W] [0|1]", setperm},
	{"dumpmem_v", "Dump memory based on virtual address. Usage: dumpmem_v 0xVA 0xSIZE", dumpmem_v},
	{"dumpmem_p", "Dump memory based on physical address. Usage: dumpmem_p 0xPA 0xSIZE", dumpmem_p},
	{"showmappings", "SHow mapping in range. Usage: showmappings 0xSTART 0xEND", showmappings}};

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
			ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	struct Eipdebuginfo info;
	uint32_t *ebp;

	ebp = (uint32_t *)read_ebp();
	cprintf("Stack backtrace: \n");
	while (ebp)
	{
		uint32_t eip = ebp[1];
		cprintf("ebp %x  eip %x  args", ebp, ebp[1]);

		for (int i = 2; i <= 6; i++)
			cprintf(" %08x", ebp[i]);

		cprintf("\n");

		debuginfo_eip(eip, &info);

		cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen,
				info.eip_fn_name, info.eip_fn_addr);
		ebp = (uint32_t *)*ebp;
	}
	return 0;
}

uint32_t
hex2int(char *in)
{
	in += 2; //Remove the "0x"
	uint32_t res = 0;
	while (*in)
	{
		uint8_t byte = *in++;
		// transform hex character to the 4bit equivalent number, using the ascii table indexes
		if (byte >= '0' && byte <= '9')
			byte = byte - '0';
		else if (byte >= 'a' && byte <= 'f')
			byte = byte - 'a' + 10;
		else if (byte >= 'A' && byte <= 'F')
			byte = byte - 'A' + 10;

		// shift 4 to make space for new digit, and add the 4 bits of the new digit
		res = (res << 4) | (byte & 0xF);
	}
	return res;
}

int showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc <= 2)
	{
		cprintf("Not enough arguments, example: showmappings 0xSTART 0xEND\n");
		return 1;
	}
	int beg = hex2int(argv[1]);
	int end = hex2int(argv[2]);
	for (int addr = beg; addr <= end; addr += PGSIZE)
	{
		pte_t *pte = pgdir_walk(kern_pgdir, (void *)addr, 0);
		if (!pte)
		{
			cprintf("VA 0x%08x has no page associated with this address\n", addr);
		}
		else
		{
			cprintf("VA: 0x%08x: PTE_W: %x PTE_U: %x PTE_P: %x\n", addr, (*pte & PTE_W) >> 1,
					(*pte & PTE_U) >> 2, *pte & PTE_P);
		}
	}
	return 0;
}

int setperm(int argc, char **argv, struct Trapframe *tf)
{
	if (argc <= 3)
	{
		cprintf("Not enough arguments, usage: setperm 0xVA [P|U|W] [0|1]. One permission at a time\n");
		return 1;
	}
	//Get the virtual address
	uintptr_t va = hex2int(argv[1]);

	//Get page table entry
	pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);

	uint32_t new_perm;
	switch ((char)argv[2][0])
	{
	case 'P':
		new_perm = PTE_P;
		break;
	case 'U':
		new_perm = PTE_U;
		break;
	case 'W':
		new_perm = PTE_W;
		break;
	default:
		cprintf("Invalid permission type\n");
		return 1;
	}

	cprintf("Old perms at 0x%08x: PTE_W: %x PTE_U: %x PTE_P: %x\n", va, !!(*pte && PTE_W),
			(*pte & PTE_U) >> 2, *pte && PTE_P);
	if (argv[3][0] == '1')
	{
		*pte |= new_perm;
	}
	else if (argv[3][0] == '0')
	{
		*pte &= ~new_perm;
	}
	else
	{
		cprintf("Invalid set type, either 0 (clear) or 1 (set) \n");
		return 1;
	}

	cprintf("New perms at 0x%08x: PTE_W: %x PTE_U: %x PTE_P: %x\n", va, !!(*pte & PTE_W),
			(*pte & PTE_U) >> 2, *pte & PTE_P);

	return 0;
}

int dumpmem_v(int argc, char **argv, struct Trapframe *tf)
{
	if (argc <= 2)
	{
		cprintf("Not enough arguments, usage: dumpmem_v 0xVA 0xSIZE\n");
		return 1;
	}
	// Number of addresses to dump
	uint32_t size = hex2int(argv[2]);
	//Starting address
	uintptr_t *addr = (uint32_t *)hex2int(argv[1]);

	cprintf("VA:\t\t Value\n");

	for (int i = 0; i < size; i++)
	{
		cprintf("%08x:\t %08x\n", addr + i, addr[i]);
	}

	return 0;
}

int dumpmem_p(int argc, char **argv, struct Trapframe *tf)
{
	if (argc <= 2)
	{
		cprintf("Not enough arguments, usage: dumpmem_v 0xVA 0xSIZE\n");
		return 1;
	}
	// Number of addresses to dump
	uint32_t size = hex2int(argv[2]);
	//Starting address
	uintptr_t *addr = (uintptr_t *)KADDR(hex2int(argv[1]));

	cprintf("PA:\t\t Value\n");

	for (int i = 0; i < size; i++)
	{
		cprintf("%08x:\t %08x\n", PADDR(addr + i), addr[i]);
	}

	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
