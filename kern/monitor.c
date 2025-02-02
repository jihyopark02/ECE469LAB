// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/assert.h>
#include <inc/memlayout.h>
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/env.h>
#include <kern/kdebug.h>
#include <kern/monitor.h>
#include <kern/trap.h>

#define CMDBUF_SIZE 80 // enough for one VGA text line

struct Command {
  const char *name;
  const char *desc;
  // return -1 to force monitor to exit
  int (*func)(int argc, char **argv, struct Trapframe *tf);
};

// LAB 1: add your command to here...
static struct Command commands[] = {
    {"help", "Display this list of commands", mon_help},
    {"kerninfo", "Display information about the kernel", mon_kerninfo},
    {"backtrace",
     "Display, for each eip, the function name, source file name, and line "
     "number corresponding to that eip",
     mon_backtrace},
    {"si", "Run one next instruction and trap back to the monitor", mon_si},
};

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf) {
  int i;

  for (i = 0; i < ARRAY_SIZE(commands); i++)
    cprintf("%s - %s\n", commands[i].name, commands[i].desc);
  return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf) {
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

int mon_backtrace(int argc, char **argv, struct Trapframe *tf) {
  // LAB 1: Your code here.
  // HINT 1: use read_ebp().
  // HINT 2: print the current ebp on the first line (not current_ebp[0])
  uint32_t pbp;
  pbp = read_ebp();

  uint32_t *current_ebp = (uint32_t *)pbp;

  cprintf("Stack Backtrace:\n");
  // cprintf("pbp: %x\n", pbp);
  // cprintf("current_ebp[0]: %x\n", current_ebp[0]);
  struct Eipdebuginfo info;

  while (current_ebp > 0) {
    uint32_t eip = current_ebp[1];
    uint32_t arg1 = current_ebp[2];
    uint32_t arg2 = current_ebp[3];
    uint32_t arg3 = current_ebp[4];
    uint32_t arg4 = current_ebp[5];
    uint32_t arg5 = current_ebp[6];

    // format: ebp f0109e58  eip f0100a62  args 00000001 f0109e80 f0109e98
    // f0100ed2 00000031
    cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
            current_ebp, eip, arg1, arg2, arg3, arg4, arg5);
    debuginfo_eip((uintptr_t)eip, &info);
    // formrat:       kern/init.c:18: test_backtrace+56
    cprintf("         %s:%d: ", info.eip_file, info.eip_line);
    cprintf("%.*s", info.eip_fn_namelen, info.eip_fn_name);
    cprintf("+%d\n", eip - info.eip_fn_addr);

    current_ebp = (uint32_t *)current_ebp[0];
    // current_ebp = (uint32_t*) pbp; // increment ebp
  }

  return 0;
}

int mon_si(int argc, char **argv, struct Trapframe *tf) {
  if (tf == NULL) {
    return 0;
  }

  tf->tf_eflags |= 0x1 << 8;

  if (curenv == NULL) {
    return 0;
  }

  env_run(curenv);
  return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int runcmd(char *buf, struct Trapframe *tf) {
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
    if (argc == MAXARGS - 1) {
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

void monitor(struct Trapframe *tf) {
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
