/* See COPYRIGHT for copyright information. */

#include <inc/assert.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/syscall.h>
#include <kern/trap.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void sys_cputs(const char *s, size_t len) {
  // Check that the user has permission to read memory [s, s+len).
  // Destroy the environment if not.

  // LAB 3: Your code here.
  user_mem_assert(curenv, (const void *)s, len, PTE_U | PTE_P);

  // Print the string supplied by the user.
  cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int sys_cgetc(void) { return cons_getc(); }

// Returns the current environment's envid.
static envid_t sys_getenvid(void) { return curenv->env_id; }

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int sys_env_destroy(envid_t envid) {
  int r;
  struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *env;
	int result = env_alloc(&env, curenv->env_id); // this should return 0 if successful

	if (result < 0) {
		return result;
	}

	env -> env_status = ENV_NOT_RUNNABLE;
	env -> env_tf = curenv -> env_tf;
	env -> env_tf.tf_regs.reg_eax = 0;

	return env->env_id;
	//panic("sys_exofork not implemented");

}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env *env;
	int result;

	if(status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}

	result = envid2env(envid, &env, 1);

	if (result < 0) {
		return result;
	}

	env -> env_status = status;
	return 0;

	//panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	struct Env *env;

	int result = envid2env(envid, &env, 1);

	if(result < 0) {
		return -E_BAD_ENV;
	}

	env->env_tf = *tf; // set envid's trap frame to tf
	env->env_tf.tf_cs |= 3; // CPL 3
	env->env_tf.tf_eflags &= ~(FL_IOPL_MASK); // IOPL 0
	env->env_tf.tf_eflags |= FL_IF; // INTERRUPTS

	return 0;
	//panic("sys_env_set_trapframe not implemented");
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *env;
	int result;

	result = envid2env(envid, &env, 1);

	if(result < 0) {
		return result;
	}

	env -> env_pgfault_upcall = func;
	//panic("sys_env_set_pgfault_upcall not implemented");
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env *env;
	struct PageInfo *page;
	int result;

	if(((uintptr_t) va >= UTOP) || (uintptr_t) va % PGSIZE != 0) {
		return -E_INVAL;
	}
	if((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P)) {
		return -E_INVAL;
	}

	page = page_alloc(ALLOC_ZERO);

	if(page == NULL) {
		return -E_NO_MEM;
	}

	result = envid2env(envid, &env, 1);

	if(result < 0) {
		return -E_BAD_ENV;
	}

	int insert;
	insert = page_insert(env -> env_pgdir, page, va, perm);

	if(insert == -E_NO_MEM) {
		page_free(page);
		return insert;
	}

	return 0;
	//panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *srcenv;
	struct Env *dstenv;
	int result_src;
	int result_dst;


	if((uintptr_t) srcva >= UTOP || (uintptr_t) srcva % PGSIZE != 0 || (uintptr_t) dstva >= UTOP || (uintptr_t) dstva % PGSIZE != 0) {
		return -E_INVAL;
	}

	if((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P)) {
		return -E_INVAL;
	}

	result_src = envid2env(srcenvid, &srcenv, 1);
	result_dst = envid2env(dstenvid, &dstenv, 1);

	if(result_src < 0 || result_dst < 0) {
		return -E_BAD_ENV;
	}

	struct PageInfo *page;
	pte_t *page_perm;

	page = page_lookup(srcenv -> env_pgdir, srcva, &page_perm);

	if(page == NULL) {
		return -E_INVAL;
	}

	if((perm & PTE_W) && !(*page_perm & PTE_W)) {
		return -E_INVAL;
	}

	int insert;
	insert = page_insert(dstenv -> env_pgdir, page, dstva, perm);

	if(insert == -E_NO_MEM) {
		return -E_NO_MEM;
	}

	return 0;
	//panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	if(((uint32_t) va >= UTOP) || (uint32_t) va % PGSIZE != 0) {
		return -E_INVAL;
	}

	struct Env *env;

	int result = envid2env(envid, &env, 1);

	page_remove(env -> env_pgdir, va);

	return 0;
	//panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_try_send not implemented");

	struct Env* env;
	int r;

	if ((r = envid2env(envid, &env, 0))) {
		return -E_BAD_ENV;
	}

	if (!(env -> env_ipc_recving)) {
		return -E_IPC_NOT_RECV;
	}

	uintptr_t src_va = (uintptr_t) srcva;

	if ((src_va < UTOP) && (src_va % PGSIZE)) {
		return -E_INVAL;
	}

	if ((src_va < UTOP) && (perm & ~PTE_SYSCALL)) {
		return -E_INVAL;
	}

	int set_perm = 0;

	if (src_va < UTOP) {
		pte_t *pte_store;
		struct PageInfo *page = page_lookup(curenv -> env_pgdir, srcva, &pte_store);

		if (page == NULL) {
			return -E_INVAL;
		}

		if (!(*pte_store & PTE_W) && (perm & PTE_W)) {
			return -E_INVAL;
		}

		r = page_insert(env -> env_pgdir, page, env -> env_ipc_dstva, perm);

		if (r < 0) {
			return r;
		}

		set_perm = 1;
	}

	env -> env_ipc_recving = 0;
	env -> env_ipc_from = curenv -> env_id;
	env -> env_ipc_value = value;
	env -> env_ipc_perm = set_perm;
	env -> env_status = ENV_RUNNABLE;

	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_recv not implemented");
	if ((uintptr_t) dstva < UTOP) {
		if ((uintptr_t) dstva % PGSIZE) {
			return -E_INVAL;
		}

		curenv -> env_ipc_dstva = dstva;
	} else {
		curenv -> env_ipc_dstva = NULL;
	}

	curenv -> env_status = ENV_NOT_RUNNABLE;
	curenv -> env_ipc_recving = 1;

	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3,
                uint32_t a4, uint32_t a5) {
  // Call the function corresponding to the 'syscallno' parameter.
  // Return any appropriate return value.
  // LAB 3: Your code here.

  int32_t ret_val = 0;

  switch (syscallno) {
  case SYS_cputs:
    sys_cputs((const char *)a1, (size_t)a2);
    return 0;
  case SYS_cgetc:
    ret_val = sys_cgetc();
    break;
  case SYS_getenvid:
    ret_val = sys_getenvid();
    break;
  case SYS_env_destroy:
    ret_val = sys_env_destroy((envid_t)a1);
    break;
	case SYS_yield:
		sys_yield();
		return 0;
  case SYS_exofork:
    ret_val = sys_exofork();
		break;
  case SYS_env_set_status:
    ret_val = sys_env_set_status((envid_t)a1, (int)a2);
		break;
  case SYS_page_alloc:
    ret_val = sys_page_alloc((envid_t) a1, (void*) a2, (int) a3);
		break;
  case SYS_page_map:
    ret_val = sys_page_map((envid_t) a1, (void*) a2, (envid_t) a3, (void*) a4, (int) a5);
		break;
  case SYS_page_unmap:
    ret_val = sys_page_unmap((envid_t) a1, (void*) a2);
		break;
  case SYS_env_set_pgfault_upcall:
    ret_val = sys_env_set_pgfault_upcall((envid_t) a1, (void*) a2);
		break;
	case SYS_ipc_try_send:
		ret_val = sys_ipc_try_send((uint32_t) a1, (uint32_t) a2, (void *) a3, (unsigned int) a4);
		break;
	case SYS_ipc_recv:
		ret_val = sys_ipc_recv((void *) a1);
		break;
	case SYS_env_set_trapframe:
		ret_val = sys_env_set_trapframe((envid_t) a1, (struct Trapframe *) a2);
		break;
  default:
    return -E_INVAL;
  }

  //sys_yield();
  return ret_val;
}
