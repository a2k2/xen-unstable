/******************************************************************************
 * irq.h
 * 
 * Interrupt distribution and delivery logic.
 * 
 * Copyright (c) 2006, K A Fraser, XenSource Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __XEN_HVM_IRQ_H__
#define __XEN_HVM_IRQ_H__

#include <xen/types.h>
#include <xen/spinlock.h>
#include <asm/irq.h>
#include <public/hvm/save.h>

struct dev_intx_gsi_link {
    struct list_head list;
    uint8_t device;
    uint8_t intx;
    uint8_t gsi;
    uint8_t link;
};

#define _HVM_IRQ_DPCI_MACH_PCI_SHIFT            0
#define _HVM_IRQ_DPCI_MACH_MSI_SHIFT            1
#define _HVM_IRQ_DPCI_GUEST_PCI_SHIFT           4
#define _HVM_IRQ_DPCI_GUEST_MSI_SHIFT           5
#define _HVM_IRQ_DPCI_TRANSLATE_SHIFT          15
#define HVM_IRQ_DPCI_MACH_PCI        (1 << _HVM_IRQ_DPCI_MACH_PCI_SHIFT)
#define HVM_IRQ_DPCI_MACH_MSI        (1 << _HVM_IRQ_DPCI_MACH_MSI_SHIFT)
#define HVM_IRQ_DPCI_GUEST_PCI       (1 << _HVM_IRQ_DPCI_GUEST_PCI_SHIFT)
#define HVM_IRQ_DPCI_GUEST_MSI       (1 << _HVM_IRQ_DPCI_GUEST_MSI_SHIFT)
#define HVM_IRQ_DPCI_TRANSLATE       (1 << _HVM_IRQ_DPCI_TRANSLATE_SHIFT)

struct hvm_gmsi_info {
    uint32_t gvec;
    uint32_t gflags;
};

struct hvm_mirq_dpci_mapping {
    uint32_t flags;
    int pending;
    struct list_head digl_list;
    struct domain *dom;
    struct hvm_gmsi_info gmsi;
};

struct hvm_girq_dpci_mapping {
    struct list_head list;
    uint8_t device;
    uint8_t intx;
    uint8_t machine_gsi;
};

#define NR_ISAIRQS  16
#define NR_LINK     4
#if defined(__i386__) || defined(__x86_64__)
# define NR_HVM_IRQS VIOAPIC_NUM_PINS
#elif defined(__ia64__)
# define NR_HVM_IRQS VIOSAPIC_NUM_PINS
#endif

/* Protected by domain's event_lock */
struct hvm_irq_dpci {
    /* Machine IRQ to guest device/intx mapping. */
    unsigned long *mapping;
    struct hvm_mirq_dpci_mapping *mirq;
    unsigned long *dirq_mask;
    /* Guest IRQ to guest device/intx mapping. */
    struct list_head girq[NR_HVM_IRQS];
    uint8_t msi_gvec_pirq[0x100];
    /* Record of mapped ISA IRQs */
    DECLARE_BITMAP(isairq_map, NR_ISAIRQS);
    /* Record of mapped Links */
    uint8_t link_cnt[NR_LINK];
    struct timer hvm_timer[NR_VECTORS];
};

/* Modify state of a PCI INTx wire. */
void hvm_pci_intx_assert(
    struct domain *d, unsigned int device, unsigned int intx);
void hvm_pci_intx_deassert(
    struct domain *d, unsigned int device, unsigned int intx);

/* Modify state of an ISA device's IRQ wire. */
void hvm_isa_irq_assert(
    struct domain *d, unsigned int isa_irq);
void hvm_isa_irq_deassert(
    struct domain *d, unsigned int isa_irq);

void hvm_set_pci_link_route(struct domain *d, u8 link, u8 isa_irq);

void hvm_maybe_deassert_evtchn_irq(void);
void hvm_assert_evtchn_irq(struct vcpu *v);
void hvm_set_callback_via(struct domain *d, uint64_t via);

void hvm_dirq_assist(struct vcpu *v);

#endif /* __XEN_HVM_IRQ_H__ */
