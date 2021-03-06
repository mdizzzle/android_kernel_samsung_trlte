/*
 *  arch/arm/include/asm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/proc-fns.h>
#include <asm-generic/mm_hooks.h>

void __check_vmalloc_seq(struct mm_struct *mm);

#ifdef CONFIG_CPU_HAS_ASID

#ifdef CONFIG_TIMA_RKP_DEBUG
extern unsigned long tima_debug_infra_cnt;
#endif

void check_and_switch_context(struct mm_struct *mm, struct task_struct *tsk);
#define init_new_context(tsk,mm)	({ atomic64_set(&mm->context.id, 0); 0; })

DECLARE_PER_CPU(atomic64_t, active_asids);

#else	/* !CONFIG_CPU_HAS_ASID */

#ifdef CONFIG_MMU

static inline void check_and_switch_context(struct mm_struct *mm,
					    struct task_struct *tsk)
{
	if (unlikely(mm->context.vmalloc_seq != init_mm.context.vmalloc_seq))
		__check_vmalloc_seq(mm);

	if (irqs_disabled())
		/*
		 * cpu_switch_mm() needs to flush the VIVT caches. To avoid
		 * high interrupt latencies, defer the call and continue
		 * running with the old mm. Since we only support UP systems
		 * on non-ASID CPUs, the old mm will remain valid until the
		 * finish_arch_post_lock_switch() call.
		 */
		set_ti_thread_flag(task_thread_info(tsk), TIF_SWITCH_MM);
	else
		cpu_switch_mm(mm->pgd, mm);
}

#define finish_arch_post_lock_switch \
	finish_arch_post_lock_switch
static inline void finish_arch_post_lock_switch(void)
{
	if (test_and_clear_thread_flag(TIF_SWITCH_MM)) {
		struct mm_struct *mm = current->mm;
		cpu_switch_mm(mm->pgd, mm);
	}
}

#endif	/* CONFIG_MMU */

#define init_new_context(tsk,mm)	0

#endif	/* CONFIG_CPU_HAS_ASID */

#define destroy_context(mm)		do { } while(0)
#define activate_mm(prev,next)		switch_mm(prev, next, NULL)

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
#ifdef	CONFIG_TIMA_RKP
extern unsigned long tima_switch_count;
extern spinlock_t tima_switch_count_lock;
#endif
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	
#ifdef	CONFIG_TIMA_RKP
	unsigned long flags;
#endif
#ifdef CONFIG_TIMA_RKP_DEBUG
	int i;
	unsigned long pmd;
	unsigned long va;
	int ret;
#endif 
#ifdef CONFIG_MMU
	unsigned int cpu = smp_processor_id();

#ifdef CONFIG_SMP
	/* check for possible thread migration */
	if (!cpumask_empty(mm_cpumask(next)) &&
	    !cpumask_test_cpu(cpu, mm_cpumask(next)))
		__flush_icache_all();
#endif
	if (!cpumask_test_and_set_cpu(cpu, mm_cpumask(next)) || prev != next) {
		check_and_switch_context(next, tsk);
#ifdef	CONFIG_TIMA_RKP
		spin_lock_irqsave(&tima_switch_count_lock, flags);
		tima_switch_count++;
		spin_unlock_irqrestore(&tima_switch_count_lock, flags);
#endif
	#ifdef CONFIG_TIMA_RKP_DEBUG
		/* 
		 * if debug infrastructure is enabled,
		 * check is L1 and L2 page tables of a 
		 * process are protected (readonly) at 
		 * each context switch
		 */
		#ifdef CONFIG_TIMA_RKP_L1_TABLES
		for (i=0; i<4; i++) {
			if (tima_debug_page_protection(((unsigned long)next->pgd + i*0x1000), 1, 1) == 0) {
				tima_debug_signal_failure(0x3f80f221, 1);
				//tima_send_cmd((unsigned long)next->pgd, 0x3f80e221);
				//printk(KERN_ERR"TIMA: New L1 PGT not protected\n");
			}
		}
		#endif
		#ifdef CONFIG_TIMA_RKP_L2_TABLES
		for (i=0; i<0x1000; i++) {
			pmd = *(unsigned long *)((unsigned long)next->pgd + i*4);
			if ((pmd & 0x3) != 0x1)
				continue;
			if((0x07e00000 <= pmd) && (pmd <= 0x07f00000)) /* skip sect to pgt region */
			       continue;	
			va = (unsigned long)phys_to_virt(pmd & (~0x3ff)) ;
			if ((ret = tima_debug_page_protection(va, 0x101, 1)) == 0) {
				tima_debug_signal_failure(0x3f80f221, 101);
				//printk(KERN_ERR"TIMA: New L2 PGT not RO va=%lx pa=%lx tima_debug_infra_cnt=%lx ret=%d\n", va, pmd, tima_debug_infra_cnt, ret);
			} else if (ret == 1) {
				tima_debug_infra_cnt++;
			}
		}
		#endif /* CONFIG_TIMA_RKP_L2_TABLES */
	#endif /* CONFIG_TIMA_RKP_DEBUG */
		if (cache_is_vivt())
			cpumask_clear_cpu(cpu, mm_cpumask(prev));
	}
#endif
}

#define deactivate_mm(tsk,mm)	do { } while (0)

#endif
