#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/hypervisor-ifs/dom0_ops.h>


asmlinkage int sys_iopl(unsigned int new_io_pl)
{
    unsigned int old_io_pl = current->thread.io_pl;
    dom0_op_t op;

    if ( !(start_info.flags & SIF_PRIVILEGED) )
        return -EPERM;

    if ( new_io_pl > 3 )
        return -EINVAL;

    /* Need "raw I/O" privileges for direct port access. */
    if ( (new_io_pl > old_io_pl) && !capable(CAP_SYS_RAWIO) )
        return -EPERM;

    /* Maintain OS privileges even if user attempts to relinquish them. */
    if ( (new_io_pl == 0) && (start_info.flags & SIF_PRIVILEGED) )
        new_io_pl = 1;

    /* Change our version of the privilege levels. */
    current->thread.io_pl = new_io_pl;

    /* Force the change at ring 0. */
    op.cmd           = DOM0_IOPL;
    op.u.iopl.domain = DOMID_SELF;
    op.u.iopl.iopl   = new_io_pl;
    HYPERVISOR_dom0_op(&op);

    return 0;
}


asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
    printk(KERN_INFO "ioperm not fully supported - %s\n",
           turn_on ? "set iopl to 3" : "ignore resource release");
    return turn_on ? sys_iopl(3) : 0;
}


