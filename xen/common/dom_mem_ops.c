/******************************************************************************
 * dom_mem_ops.c
 *
 * Code to handle memory related requests from domains eg. balloon driver.
 *
 * Copyright (c) 2003, B Dragovic & K A Fraser.
 */

#include <xeno/config.h>
#include <xeno/types.h>
#include <xeno/lib.h>
#include <xeno/mm.h>
#include <hypervisor-ifs/dom_mem_ops.h>
#include <xeno/perfc.h>
#include <xeno/sched.h>
#include <xeno/event.h>
#include <asm/domain_page.h>

#if 0
#define DPRINTK(_f, _a...) printk( _f , ## _a )
#else
#define DPRINTK(_f, _a...) ((void)0)
#endif

static long alloc_dom_mem(struct task_struct *p, reservation_increase_t op)
{
    struct list_head *temp;
    struct pfn_info  *pf;     /* pfn_info of current page */
    unsigned long     mpfn;   /* machine frame number of current page */
    void             *va;     /* Xen-usable mapping of current page */
    unsigned long     i;
    unsigned long     flags;

    /*
     * POLICY DECISION: Each domain has a page limit.
     * NB. The first part of test is because op.size could be so big that
     * tot_pages + op.size overflows a u_long.
     */
    if( (op.size > p->max_pages) ||
        ((p->tot_pages + op.size) > p->max_pages) )
        return -ENOMEM;

    spin_lock_irqsave(&free_list_lock, flags);

    if ( free_pfns < (op.size + (SLACK_DOMAIN_MEM_KILOBYTES >> 
                                  (PAGE_SHIFT-10))) ) 
    {
        spin_unlock_irqrestore(&free_list_lock, flags);
        return -ENOMEM;
    }

    spin_lock(&p->page_lock);
    
    temp = free_list.next;
    for ( i = 0; i < op.size; i++ )
    {
        /* Get a free page and add it to the domain's page list. */
        pf = list_entry(temp, struct pfn_info, list);
        pf->flags |= p->domain;
        pf->type_count = pf->tot_count = 0;
        temp = temp->next;
        list_del(&pf->list);
        list_add_tail(&pf->list, &p->pg_head);
        free_pfns--;

        p->tot_pages++;

        /* Inform the domain of the new page's machine address. */ 
        mpfn = (unsigned long)(pf - frame_table);
        copy_to_user(op.pages, &mpfn, sizeof(mpfn));
        op.pages++; 

        /* Zero out the page to prevent information leakage. */
        va = map_domain_mem(mpfn << PAGE_SHIFT);
        memset(va, 0, PAGE_SIZE);
        unmap_domain_mem(va);
    }

    spin_unlock(&p->page_lock);
    spin_unlock_irqrestore(&free_list_lock, flags);
    
    return op.size;
}
    
static long free_dom_mem(struct task_struct *p, reservation_decrease_t op)
{
    struct list_head *temp;
    struct pfn_info  *pf;     /* pfn_info of current page */
    unsigned long     mpfn;   /* machine frame number of current page */
    unsigned long     i;
    unsigned long     flags;
    long              rc = 0;
    int               need_flush = 0;

    spin_lock_irqsave(&free_list_lock, flags);
    spin_lock(&p->page_lock);

    temp = free_list.next;
    for ( i = 0; i < op.size; i++ )
    {
        copy_from_user(&mpfn, op.pages, sizeof(mpfn));
        op.pages++;
        if ( mpfn >= max_page )
        {
            DPRINTK("Domain %d page number out of range (%08lx>=%08lx)\n", 
                    p->domain, mpfn, max_page);
            rc = -EINVAL;
            goto out;
        }

        pf = &frame_table[mpfn];
        if ( (pf->type_count != 0) || 
             (pf->tot_count != 0) ||
             (pf->flags & PG_domain_mask != p->domain) )
        {
            DPRINTK("Bad page free for domain %d (%ld, %ld, %08lx)\n",
                    p->domain, pf->type_count, pf->tot_count, pf->flags);
            rc = -EINVAL;
            goto out;
        }

        need_flush |= pf->flags & PG_need_flush;

        pf->flags = 0;

        list_del(&pf->list);
        list_add(&pf->list, &free_list);
        free_pfns++;

        p->tot_pages--;
    }

 out:
    spin_unlock(&p->page_lock);
    spin_unlock_irqrestore(&free_list_lock, flags);
    
    if ( need_flush )
    {
        __flush_tlb();
        perfc_incrc(need_flush_tlb_flush);
    }

    return rc ? rc : op.size;
}
    
long do_dom_mem_op(dom_mem_op_t *mem_op)
{
    dom_mem_op_t dmop;
    unsigned long ret;

    if ( copy_from_user(&dmop, mem_op, sizeof(dom_mem_op_t)) )
        return -EFAULT;

    switch ( dmop.op )
    {
    case MEMOP_RESERVATION_INCREASE:
        ret = alloc_dom_mem(current, dmop.u.increase);
        break;

    case MEMOP_RESERVATION_DECREASE:
        ret = free_dom_mem(current, dmop.u.decrease);
        break;

    default:
        ret = -ENOSYS;
        break;
    }

    return ret;    
}
