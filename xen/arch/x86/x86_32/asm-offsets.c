/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <xen/sched.h>

#define DEFINE(_sym, _val) \
    __asm__ __volatile__ ( "\n->" #_sym " %0 " #_val : : "i" _val )
#define BLANK() \
    __asm__ __volatile__ ( "\n->" : : )
#define OFFSET(_sym, _str, _mem) \
    DEFINE(_sym, offsetof(_str, _mem));

void __dummy__(void)
{
    OFFSET(XREGS_eax, struct xen_regs, eax);
    OFFSET(XREGS_ebx, struct xen_regs, ebx);
    OFFSET(XREGS_ecx, struct xen_regs, ecx);
    OFFSET(XREGS_edx, struct xen_regs, edx);
    OFFSET(XREGS_esi, struct xen_regs, esi);
    OFFSET(XREGS_edi, struct xen_regs, edi);
    OFFSET(XREGS_esp, struct xen_regs, esp);
    OFFSET(XREGS_ebp, struct xen_regs, ebp);
    OFFSET(XREGS_eip, struct xen_regs, eip);
    OFFSET(XREGS_cs, struct xen_regs, cs);
    OFFSET(XREGS_ds, struct xen_regs, ds);
    OFFSET(XREGS_es, struct xen_regs, es);
    OFFSET(XREGS_fs, struct xen_regs, fs);
    OFFSET(XREGS_gs, struct xen_regs, gs);
    OFFSET(XREGS_ss, struct xen_regs, ss);
    OFFSET(XREGS_eflags, struct xen_regs, eflags);
    OFFSET(XREGS_orig_eax, struct xen_regs, orig_eax);
    BLANK();

    OFFSET(EDOMAIN_processor, struct exec_domain, processor);
    OFFSET(EDOMAIN_vcpu_info, struct exec_domain, vcpu_info);
    OFFSET(EDOMAIN_event_sel, struct exec_domain, thread.event_selector);
    OFFSET(EDOMAIN_event_addr, struct exec_domain, thread.event_address);
    OFFSET(EDOMAIN_failsafe_sel, struct exec_domain, thread.failsafe_selector);
    OFFSET(EDOMAIN_failsafe_addr, struct exec_domain, thread.failsafe_address);
    OFFSET(EDOMAIN_trap_bounce, struct exec_domain, thread.trap_bounce);
    BLANK();

    OFFSET(VCPUINFO_upcall_pending, vcpu_info_t, evtchn_upcall_pending);
    OFFSET(VCPUINFO_upcall_mask, vcpu_info_t, evtchn_upcall_mask);
    BLANK();

    OFFSET(TRAPBOUNCE_error_code, struct trap_bounce, error_code);
    OFFSET(TRAPBOUNCE_cr2, struct trap_bounce, cr2);
    OFFSET(TRAPBOUNCE_flags, struct trap_bounce, flags);
    OFFSET(TRAPBOUNCE_cs, struct trap_bounce, cs);
    OFFSET(TRAPBOUNCE_eip, struct trap_bounce, eip);
    BLANK();
}
