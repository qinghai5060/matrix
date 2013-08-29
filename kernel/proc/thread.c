#include <types.h>
#include <stddef.h>
#include <string.h>
#include "matrix/matrix.h"
#include "debug.h"
#include "hal/core.h"
#include "mm/mlayout.h"
#include "mm/mmu.h"
#include "mm/kmem.h"
#include "mm/malloc.h"
#include "mm/slab.h"
#include "proc/thread.h"
#include "proc/process.h"
#include "proc/sched.h"

/* Temporarily used thread id */
static tid_t _next_tid = 1;

/* Thread structure cache */
static slab_cache_t _thread_cache;

extern uint32_t read_eip();

static tid_t id_alloc()
{
	return _next_tid++;
}

void arch_thread_init(struct thread *t, void *kstack, void (*entry)())
{
	t->arch.esp = kstack;
	t->arch.ebp = 0;
	t->arch.eip = entry;
}

void arch_thread_switch(struct thread *curr, struct thread *prev)
{
	void *esp, *ebp, *eip;

	/* Get the stack pointer and base pointer first */
	asm volatile("mov %%esp, %0" : "=r"(esp));
	asm volatile("mov %%ebp, %0" : "=r"(ebp));

	/* Read the instruction pointer. We do some cunning logic here:
	 * One of the two things could have happened when this function exits
	 *   (a) We called the function and it returned the EIP as requested.
	 *   (b) We have just switched processes, and because the saved EIP is
	 *       essentially the instruction after read_eip(), it will seem as
	 *       if read_eip has just returned.
	 * In the second case we need to return immediately. To detect it we put
	 * a magic number in EAX further down at the end of this function. As C
	 * returns value in EAX, it will look like the return value is this
	 * magic number. (0x87654321).
	 */
	eip = (void *)read_eip();
	
	/* If we have just switched processes, do nothing */
	if (0x87654321 == (uint32_t)eip) {
		return;
	}

	/* Save the process context for previous process */
	if (prev) {
		prev->arch.eip = eip;
		prev->arch.esp = esp;
		prev->arch.ebp = ebp;
	}

	/* Switch to current process context */
	eip = curr->arch.eip;
	esp = curr->arch.esp;
	ebp = curr->arch.ebp;

	/* Switch the kernel stack in TSS to the process's kernel stack */
	set_kernel_stack(curr->kstack);

	DEBUG(DL_DBG, ("prev(%s:%x:%x:%x), curr(%s:%x:%x:%x)\n",
		       prev->name, prev->arch.eip, prev->arch.esp, prev->arch.ebp,
		       curr->name, curr->arch.eip, curr->arch.esp, curr->arch.ebp));

	/* Here we:
	 * [1] Disable interrupts so we don't get bothered.
	 * [2] Temporarily puts the new EIP location in EBX.
	 * [3] Loads the stack and base pointers from the new process struct.
	 * [4] Puts a magic number (0x87654321) in EAX so that above we can recognize
	 *     that we've just switched process.
	 * [5] Jumps to the location in EBX (remember we put the new EIP in there)
	 * Note that you can't change the sequence we set each register here. You
	 * can check the actual asm code generated by your compiler for the reason.
	 */
	asm volatile ("mov %0, %%ebx\n"
		      "mov %1, %%esp\n"
		      "mov %2, %%ebp\n"
		      "mov $0x87654321, %%eax\n"	/* read_eip() will return 0x87654321 */
		      "jmp *%%ebx\n"
		      :: "r"(eip), "r"(esp), "r"(ebp)
		      : "%ebx", "%esp", "%eax");
}

/**
 * Switch to the user mode
 * @param location	- User address to jump to
 * @param ustack	- User stack
 */
void arch_thread_enter_uspace(ptr_t entry, ptr_t ustack, ptr_t ctx)
{
	/* Setup our kernel stack, note that the stack was grow from high address
	 * to low address
	 */
	set_kernel_stack(CURR_THREAD->kstack);

	/* Push the arguments pointer to the stack */
	ustack -= sizeof(ptr_t);
	*((ptr_t *)ustack) = ctx;

	/* Setup a stack frame for switching to user mode.
	 * The code firstly disables interrupts, as we're working on a critical
	 * section of code. It then sets the ds, es and fs segment selectors
	 * to our user mode data selector - 0x23. Note that sti will not work when
	 * we enter user mode as it is a privileged instruction, we will set the
	 * interrupt flag to enable interrupt.
	 */
	asm volatile("cli\n"
		     "mov %1, %%esp\n"		/* Stack pointer */
		     "mov $0x23, %%ax\n"	/* Segment selector */
		     "mov %%ax, %%ds\n"
		     "mov %%ax, %%es\n"
		     "mov %%ax, %%fs\n"
		     "mov %%esp, %%eax\n"	/* Move stack to EAX */
		     "pushl $0x23\n"		/* Segment selector again */
		     "pushl %%eax\n"
		     "pushf\n"			/* Push flags */
		     "pop %%eax\n"		/* Enable the interrupt flag */
		     "orl $0x200, %%eax\n"
		     "pushl %%eax\n"
		     "pushl $0x1B\n"		/* Code segment */
		     "pushl %0\n"		/* Push the entry point */
		     "iret\n"
		     :: "m"(entry), "r"(ustack) : "%ax", "%esp", "%eax");
}

/* Thread entry function wrapper */
static void thread_wrapper()
{
	/* Upon switching to a newly-created thread's context, execution will
	 * jump to this function, rather than going back to the scheduler.
	 */
	sched_post_switch(TRUE);
	
	DEBUG(DL_DBG, ("entered thread(%s:%p) on CPU %d.\n",
		       CURR_THREAD->name, CURR_THREAD, CURR_CORE->id));

	/* Run the thread's main function and exit when it returns */
	CURR_THREAD->entry(CURR_THREAD->args);
	
	thread_exit();
}

/* Userspace thread entry function wrapper */
void thread_uspace_wrapper(void *ctx)
{
	struct thread_uspace_creation *info;

	ASSERT(ctx != NULL);
	
	info = (struct thread_uspace_creation *)ctx;
	
	arch_thread_enter_uspace(info->entry, info->esp, info->args);
}

static void thread_ctor(void *obj)
{
	struct thread *t = (struct thread *)obj;

	spinlock_init(&t->lock, "t-lock");
	
	t->ref_count = 0;
	
	LIST_INIT(&t->runq_link);
	LIST_INIT(&t->wait_link);
	LIST_INIT(&t->owner_link);

	init_timer(&t->sleep_timer, "t-slp-tmr", t);

	/* Initialize the death notifier */
	init_notifier(&t->death_notifier);
}

static void thread_dtor(void *obj)
{
	;
}

static boolean_t thread_interrupt_internal(struct thread *t, int flags)
{
	struct spinlock *l;
	boolean_t ret = FALSE;

	l = t->wait_lock;
	if (l) {
		spinlock_acquire(l);
	}
	
	spinlock_acquire(&t->lock);
	
	if ((t->state == THREAD_SLEEPING) &&
	    FLAG_ON(t->flags, THREAD_INTERRUPTIBLE_F)) {
		ret = TRUE;
	} else {
		SET_FLAG(t->flags, THREAD_INTERRUPTIBLE_F);
	}

	spinlock_release(&t->lock);

	if (l) {
		spinlock_release(l);
	}

	return ret;
}

static void thread_wake_internal(struct thread *t)
{
	ASSERT(t->state == THREAD_SLEEPING);

	/* Stop the timer */
	cancel_timer(&t->sleep_timer);

	/* Remove the thread from the list and wake it up */
	list_del(&t->wait_link);
	CLEAR_FLAG(t->flags, THREAD_INTERRUPTIBLE_F);
	t->wait_lock = NULL;

	t->state = THREAD_READY;
	sched_insert_thread(t);
}

static void thread_timeout(void *ctx)
{
	struct thread *t = ctx;
	struct spinlock *l;

	DEBUG(DL_DBG, ("thread(%s:%p:%d) timed out.\n", t->name, t, t->id));

	l = t->wait_lock;
	if (l) {
		spinlock_acquire(l);
	}
	
	spinlock_acquire(&t->lock);

	/* The thread could have been woken up already by another CPU */
	if (t->state == THREAD_SLEEPING) {
		t->sleep_status = -1;
		thread_wake_internal(t);
	}

	spinlock_release(&t->lock);

	if (l) {
		spinlock_release(l);
	}
}

int thread_create(const char *name, struct process *owner, int flags,
		  thread_func_t func, void *args, struct thread **tp)
{
	int rc = -1;
	struct thread *t;

	/* If no owner provided then kernel process is our owner */
	if (!owner) {
		owner = _kernel_proc;
	}

	/* Allocate a thread structure from our slab allocator */
	t = slab_cache_alloc(&_thread_cache);
	if (!t) {
		DEBUG(DL_INF, ("slab allocate thread failed.\n"));
		goto out;
	}

	/* Allocate an ID for the thread */
	t->id = id_alloc();

	strncpy(t->name, name, T_NAME_LEN - 1);
	t->name[T_NAME_LEN - 1] = 0;
	
	/* Allocate kernel stack for the process */
	t->kstack = kmalloc(KSTACK_SIZE, MM_ALIGN_F) + KSTACK_SIZE;
	if (!t->kstack) {
		DEBUG(DL_INF, ("kmalloc kstack failed.\n"));
		goto out;
	}
	memset((void *)((uint32_t)t->kstack - KSTACK_SIZE), 0, KSTACK_SIZE);

	/* Initialize the architecture-specific data */
	arch_thread_init(t, t->kstack, thread_wrapper);

	/* Initially set the CPU to NULL - the thread will be assigned to a
	 * CPU when thread_run() is called on it.
	 */
	t->core = NULL;

	t->state = THREAD_CREATED;
	t->flags = flags;
	t->priority = 16;
	t->ustack = 0;
	t->ustack_size = 0;
	t->entry = func;
	t->args = args;
	t->quantum = 0;
	t->wait_lock = NULL;

	/* Initialize signal handling state */
	t->pending_signals = 0;
	t->signal_mask = 0;
	memset(t->signal_info, 0, sizeof(t->signal_info));
	t->signal_stack.ss_sp = NULL;
	t->signal_stack.ss_size = 0;
	t->signal_stack.ss_flags = 0;

	/* Add the thread to the owner */
	process_attach(owner, t);
	rc = 0;

	DEBUG(DL_DBG, ("thread(%s:%p:%d) created.\n", t->name, t, t->id));

	if (tp) {
		/* Add a reference if the caller wants a pointer to the thread */
		atomic_inc(&t->ref_count);
		*tp = t;
	} else {
		/* Caller doesn't want a pointer, just start running it */
		thread_run(t);
	}

out:
	if (rc != 0) {
		if (t) {
			kfree(t);
		}
	}
	
	return rc;
}

int thread_sleep(struct spinlock *lock, useconds_t timeout, const char *name, int flags)
{
	int rc = -1;
	boolean_t state;

	if (!timeout) {
		rc = -1;
		goto cancel;
	}

	/* We are definitely going to sleep. Get the interrupt state to restore */
	state = lock ? lock->state : irq_disable();

	spinlock_acquire_noirq(&CURR_THREAD->lock);
	CURR_THREAD->sleep_status = 0;
	CURR_THREAD->wait_lock = lock;

	/* Start the timer if required */
	if (timeout > 0) {
		set_timer(&CURR_THREAD->sleep_timer, timeout, thread_timeout);
	}

	/* Release the specified lock */
	if (lock) {
		spinlock_release_noirq(lock);
	}

	/* Set the current thread to sleeping, it will be removed from runq by
	 * sched_reschedule()
	 */
	CURR_THREAD->state = THREAD_SLEEPING;
	sched_reschedule(state);
	
	return CURR_THREAD->sleep_status;

cancel:
	list_del(&CURR_THREAD->wait_link);
	if (lock) {
		spinlock_release(lock);
	}
	return rc;
}

void thread_wake(struct thread *t)
{
	spinlock_acquire(&t->lock);
	thread_wake_internal(t);
	spinlock_release(&t->lock);
}

void thread_run(struct thread *t)
{
	spinlock_acquire(&t->lock);
	
	ASSERT(t->state == THREAD_CREATED);

	t->state = THREAD_READY;
	sched_insert_thread(t);

	spinlock_release(&t->lock);
}

void thread_kill(struct thread *t)
{
	DEBUG(DL_DBG, ("killing thread(%s:%d).\n", t->name, t->id));
	
	if (t->owner != _kernel_proc) {
		thread_interrupt_internal(t, THREAD_KILLED_F);
	}
}

boolean_t thread_interrupt(struct thread *t)
{
	/* Cannot interrupt kernel threads */
	if (t->owner != _kernel_proc) {
		return thread_interrupt_internal(t, 0);
	} else {
		return FALSE;
	}
}

void thread_release(struct thread *t)
{
	void *kstack;
	struct process *p;

	if (atomic_dec(&t->ref_count) > 0) {
		return;
	}

	/* If the thread is running it will have a reference on it. Should not
	 * be in the running state for this reason
	 */
	ASSERT((t->state == THREAD_CREATED) || (t->state == THREAD_DEAD));
	ASSERT(LIST_EMPTY(&t->runq_link));

	p = t->owner;

	/* Detach from its owner */
	process_detach(t);

	/* Cleanup the thread */
	kstack = (void *)((uint32_t)t->kstack - KSTACK_SIZE);
	kfree(kstack);

	notifier_clear(&t->death_notifier);

	DEBUG(DL_DBG, ("process(%s:%d:%d), thread(%s:%d), kstack(%p).\n", p->name,
		       p->id, p->state, t->name, t->id, kstack));

	/* Free this thread to the thread cache */
	slab_cache_free(&_thread_cache, t);
}

void thread_exit()
{
	int rc = -1;
	boolean_t state;

	/* Unmap the user stack */
	if (CURR_THREAD->ustack_size) {
		DEBUG(DL_DBG, ("unmap ustack, proc(%s), mmu(%p).\n",
			       CURR_PROC->name, CURR_PROC->vas->mmu));
		rc = mmu_unmap(CURR_PROC->vas->mmu, (ptr_t)CURR_THREAD->ustack,
			       CURR_THREAD->ustack_size);
		ASSERT(rc == 0);
	}

	/* Notify the waiter */
	notifier_run(&CURR_THREAD->death_notifier);

	state = irq_disable();
	spinlock_acquire_noirq(&CURR_THREAD->lock);
	
	CURR_THREAD->state = THREAD_DEAD;

	sched_reschedule(state);

	PANIC("Should not get here");
}

void init_thread()
{
	/* Initialize the thread slab cache */
	slab_cache_init(&_thread_cache, "thread-cache", sizeof(struct thread), 
			thread_ctor, thread_dtor, 0);
}

