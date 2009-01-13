#ifndef __HVMLOADER_CONFIG_H__
#define __HVMLOADER_CONFIG_H__

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1ul << PAGE_SHIFT)

#define IOAPIC_BASE_ADDRESS 0xfec00000
#define IOAPIC_ID           0x01
#define IOAPIC_VERSION      0x11

#define LAPIC_BASE_ADDRESS  0xfee00000
#define LAPIC_ID(vcpu_id)   ((vcpu_id) * 2)

#define PCI_ISA_DEVFN       0x08    /* dev 1, fn 0 */
#define PCI_ISA_IRQ_MASK    0x0c20U /* ISA IRQs 5,10,11 are PCI connected */

/* MMIO hole: Hardcoded defaults, which can be dynamically expanded. */
#define PCI_MEM_START       0xf0000000
#define PCI_MEM_END         0xfc000000
extern unsigned long pci_mem_start, pci_mem_end;

/* We reserve 16MB for special BIOS mappings, etc. */
#define RESERVED_MEMBASE    0xfc000000
#define RESERVED_MEMSIZE    0x01000000

#define ROMBIOS_SEG            0xF000
#define ROMBIOS_BEGIN          0x000F0000
#define ROMBIOS_SIZE           0x00010000
#define ROMBIOS_MAXOFFSET      0x0000FFFF
#define ROMBIOS_END            (ROMBIOS_BEGIN + ROMBIOS_SIZE)

/* Memory map. */
#define SCRATCH_PHYSICAL_ADDRESS      0x00010000
#define HYPERCALL_PHYSICAL_ADDRESS    0x00080000
#define VGABIOS_PHYSICAL_ADDRESS      0x000C0000
#define OPTIONROM_PHYSICAL_ADDRESS    0x000C8000
#define OPTIONROM_PHYSICAL_END        0x000EA000
#define ACPI_PHYSICAL_ADDRESS         0x000EA000
#define E820_PHYSICAL_ADDRESS         0x000EA100
#define SMBIOS_PHYSICAL_ADDRESS       0x000EB000
#define SMBIOS_MAXIMUM_SIZE           0x00005000
#define ROMBIOS_PHYSICAL_ADDRESS      0x000F0000

/* Offsets from E820_PHYSICAL_ADDRESS. */
#define E820_NR_OFFSET                0x0
#define E820_OFFSET                   0x8

/* Xen Platform Device */
#define PFFLAG_ROM_LOCK 1 /* Sets whether ROM memory area is RW or RO */

struct bios_info {
    uint8_t  com1_present:1;
    uint8_t  com2_present:1;
    uint8_t  hpet_present:1;
    uint32_t pci_min, pci_len;
    uint16_t xen_pfiob;
};

#endif /* __HVMLOADER_CONFIG_H__ */
