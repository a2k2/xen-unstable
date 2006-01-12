/*
 *      based on linux-2.6.11/arch/i386/kernel/apic.c
 *
 *  Local APIC handling, local APIC timers
 *
 *  (c) 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes
 *  Maciej W. Rozycki   :   Bits for genuine 82489DX APICs;
 *                  thanks to Eric Gilmore
 *                  and Rolf G. Tews
 *                  for testing these extensively.
 *	Maciej W. Rozycki	:	Various updates and fixes.
 *	Mikael Pettersson	:	Power Management for UP-APIC.
 *    Pavel Machek and
 *    Mikael Pettersson    :    PM converted to driver model.
 */

#include <xen/config.h>
#include <xen/perfc.h>
#include <xen/errno.h>
#include <xen/init.h>
#include <xen/mm.h>
#include <xen/sched.h>
#include <xen/irq.h>
#include <xen/delay.h>
#include <xen/smp.h>
#include <xen/softirq.h>
#include <asm/mc146818rtc.h>
#include <asm/msr.h>
#include <asm/atomic.h>
#include <asm/mpspec.h>
#include <asm/flushtlb.h>
#include <asm/hardirq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <mach_apic.h>
#include <io_ports.h>

/*
 * Debug level
 */
int apic_verbosity;

/* Using APIC to generate smp_local_timer_interrupt? */
int using_apic_timer = 0;

static int enabled_via_apicbase;

int get_physical_broadcast(void)
{
    unsigned int lvr, version;
    lvr = apic_read(APIC_LVR);
    version = GET_APIC_VERSION(lvr);
    if (!APIC_INTEGRATED(version) || version >= 0x14)
        return 0xff;
    else
        return 0xf;
}

int get_maxlvt(void)
{
    unsigned int v, ver, maxlvt;

    v = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(v);
    /* 82489DXs do not report # of LVT entries. */
    maxlvt = APIC_INTEGRATED(ver) ? GET_APIC_MAXLVT(v) : 2;
    return maxlvt;
}

void clear_local_APIC(void)
{
    int maxlvt;
    unsigned long v;

    maxlvt = get_maxlvt();

    /*
     * Masking an LVT entry on a P6 can trigger a local APIC error
     * if the vector is zero. Mask LVTERR first to prevent this.
     */
    if (maxlvt >= 3) {
        v = ERROR_APIC_VECTOR; /* any non-zero vector will do */
        apic_write_around(APIC_LVTERR, v | APIC_LVT_MASKED);
    }
    /*
     * Careful: we have to set masks only first to deassert
     * any level-triggered sources.
     */
    v = apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT, v | APIC_LVT_MASKED);
    v = apic_read(APIC_LVT0);
    apic_write_around(APIC_LVT0, v | APIC_LVT_MASKED);
    v = apic_read(APIC_LVT1);
    apic_write_around(APIC_LVT1, v | APIC_LVT_MASKED);
    if (maxlvt >= 4) {
        v = apic_read(APIC_LVTPC);
        apic_write_around(APIC_LVTPC, v | APIC_LVT_MASKED);
    }

/* lets not touch this if we didn't frob it */
#ifdef CONFIG_X86_MCE_P4THERMAL
    if (maxlvt >= 5) {
        v = apic_read(APIC_LVTTHMR);
        apic_write_around(APIC_LVTTHMR, v | APIC_LVT_MASKED);
    }
#endif
    /*
     * Clean APIC state for other OSs:
     */
    apic_write_around(APIC_LVTT, APIC_LVT_MASKED);
    apic_write_around(APIC_LVT0, APIC_LVT_MASKED);
    apic_write_around(APIC_LVT1, APIC_LVT_MASKED);
    if (maxlvt >= 3)
        apic_write_around(APIC_LVTERR, APIC_LVT_MASKED);
    if (maxlvt >= 4)
        apic_write_around(APIC_LVTPC, APIC_LVT_MASKED);

#ifdef CONFIG_X86_MCE_P4THERMAL
    if (maxlvt >= 5)
        apic_write_around(APIC_LVTTHMR, APIC_LVT_MASKED);
#endif
    v = GET_APIC_VERSION(apic_read(APIC_LVR));
    if (APIC_INTEGRATED(v)) {	/* !82489DX */
        if (maxlvt > 3)        /* Due to Pentium errata 3AP and 11AP. */
            apic_write(APIC_ESR, 0);
        apic_read(APIC_ESR);
    }
}

void __init connect_bsp_APIC(void)
{
    if (pic_mode) {
        /*
         * Do not trust the local APIC being empty at bootup.
         */
        clear_local_APIC();
        /*
         * PIC mode, enable APIC mode in the IMCR, i.e.
         * connect BSP's local APIC to INT and NMI lines.
         */
        apic_printk(APIC_VERBOSE, "leaving PIC mode, "
                    "enabling APIC mode.\n");
        outb(0x70, 0x22);
        outb(0x01, 0x23);
    }
    enable_apic_mode();
}

void disconnect_bsp_APIC(void)
{
    if (pic_mode) {
        /*
         * Put the board back into PIC mode (has an effect
         * only on certain older boards).  Note that APIC
         * interrupts, including IPIs, won't work beyond
         * this point!  The only exception are INIT IPIs.
         */
        apic_printk(APIC_VERBOSE, "disabling APIC mode, "
                    "entering PIC mode.\n");
        outb(0x70, 0x22);
        outb(0x00, 0x23);
    }
}

void disable_local_APIC(void)
{
    unsigned long value;

    clear_local_APIC();

    /*
     * Disable APIC (implies clearing of registers
     * for 82489DX!).
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_SPIV_APIC_ENABLED;
    apic_write_around(APIC_SPIV, value);

    if (enabled_via_apicbase) {
        unsigned int l, h;
        rdmsr(MSR_IA32_APICBASE, l, h);
        l &= ~MSR_IA32_APICBASE_ENABLE;
        wrmsr(MSR_IA32_APICBASE, l, h);
    }
}

/*
 * This is to verify that we're looking at a real local APIC.
 * Check these against your board if the CPUs aren't getting
 * started for no apparent reason.
 */
int __init verify_local_APIC(void)
{
    unsigned int reg0, reg1;

    /*
     * The version register is read-only in a real APIC.
     */
    reg0 = apic_read(APIC_LVR);
    apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg0);
    apic_write(APIC_LVR, reg0 ^ APIC_LVR_MASK);
    reg1 = apic_read(APIC_LVR);
    apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg1);

    /*
     * The two version reads above should print the same
     * numbers.  If the second one is different, then we
     * poke at a non-APIC.
     */
    if (reg1 != reg0)
        return 0;

    /*
     * Check if the version looks reasonably.
     */
    reg1 = GET_APIC_VERSION(reg0);
    if (reg1 == 0x00 || reg1 == 0xff)
        return 0;
    reg1 = get_maxlvt();
    if (reg1 < 0x02 || reg1 == 0xff)
        return 0;

    /*
     * The ID register is read/write in a real APIC.
     */
    reg0 = apic_read(APIC_ID);
    apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg0);

    /*
     * The next two are just to see if we have sane values.
     * They're only really relevant if we're in Virtual Wire
     * compatibility mode, but most boxes are anymore.
     */
    reg0 = apic_read(APIC_LVT0);
    apic_printk(APIC_DEBUG, "Getting LVT0: %x\n", reg0);
    reg1 = apic_read(APIC_LVT1);
    apic_printk(APIC_DEBUG, "Getting LVT1: %x\n", reg1);

    return 1;
}

void __init sync_Arb_IDs(void)
{
    /* Unsupported on P4 - see Intel Dev. Manual Vol. 3, Ch. 8.6.1 */
    unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));
    if (ver >= 0x14)    /* P4 or higher */
        return;
    /*
     * Wait for idle.
     */
    apic_wait_icr_idle();

    apic_printk(APIC_DEBUG, "Synchronizing Arb IDs.\n");
    apic_write_around(APIC_ICR, APIC_DEST_ALLINC | APIC_INT_LEVELTRIG
                      | APIC_DM_INIT);
}

extern void __error_in_apic_c (void);

/*
 * An initial setup of the virtual wire mode.
 */
void __init init_bsp_APIC(void)
{
    unsigned long value, ver;

    /*
     * Don't do the setup now if we have a SMP BIOS as the
     * through-I/O-APIC virtual wire mode might be active.
     */
    if (smp_found_config || !cpu_has_apic)
        return;

    value = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(value);
    
    /*
     * Do not trust the local APIC being empty at bootup.
     */
    clear_local_APIC();
    
    /*
     * Enable APIC.
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_VECTOR_MASK;
    value |= APIC_SPIV_APIC_ENABLED;
    
    /* This bit is reserved on P4/Xeon and should be cleared */
    if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) && (boot_cpu_data.x86 == 15))
        value &= ~APIC_SPIV_FOCUS_DISABLED;
    else
        value |= APIC_SPIV_FOCUS_DISABLED;
    value |= SPURIOUS_APIC_VECTOR;
    apic_write_around(APIC_SPIV, value);

    /*
     * Set up the virtual wire mode.
     */
    apic_write_around(APIC_LVT0, APIC_DM_EXTINT);
    value = APIC_DM_NMI;
    if (!APIC_INTEGRATED(ver))              /* 82489DX */
        value |= APIC_LVT_LEVEL_TRIGGER;
    apic_write_around(APIC_LVT1, value);
}

void __init setup_local_APIC (void)
{
    unsigned long oldvalue, value, ver, maxlvt;

    /* Pound the ESR really hard over the head with a big hammer - mbligh */
    if (esr_disable) {
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
    }

    value = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(value);

    if ((SPURIOUS_APIC_VECTOR & 0x0f) != 0x0f)
        __error_in_apic_c();

    /*
     * Double-check whether this APIC is really registered.
     */
    if (!apic_id_registered())
        BUG();

    /*
     * Intel recommends to set DFR, LDR and TPR before enabling
     * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
     * document number 292116).  So here it goes...
     */
    init_apic_ldr();

    /*
     * Set Task Priority to 'accept all'. We never change this
     * later on.
     */
    value = apic_read(APIC_TASKPRI);
    value &= ~APIC_TPRI_MASK;
    apic_write_around(APIC_TASKPRI, value);

    /*
     * Now that we are all set up, enable the APIC
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_VECTOR_MASK;
    /*
     * Enable APIC
     */
    value |= APIC_SPIV_APIC_ENABLED;

    /*
     * Some unknown Intel IO/APIC (or APIC) errata is biting us with
     * certain networking cards. If high frequency interrupts are
     * happening on a particular IOAPIC pin, plus the IOAPIC routing
     * entry is masked/unmasked at a high rate as well then sooner or
     * later IOAPIC line gets 'stuck', no more interrupts are received
     * from the device. If focus CPU is disabled then the hang goes
     * away, oh well :-(
     *
     * [ This bug can be reproduced easily with a level-triggered
     *   PCI Ne2000 networking cards and PII/PIII processors, dual
     *   BX chipset. ]
     */
    /*
     * Actually disabling the focus CPU check just makes the hang less
     * frequent as it makes the interrupt distributon model be more
     * like LRU than MRU (the short-term load is more even across CPUs).
     * See also the comment in end_level_ioapic_irq().  --macro
     */
#if 1
    /* Enable focus processor (bit==0) */
    value &= ~APIC_SPIV_FOCUS_DISABLED;
#else
    /* Disable focus processor (bit==1) */
    value |= APIC_SPIV_FOCUS_DISABLED;
#endif
    /*
     * Set spurious IRQ vector
     */
    value |= SPURIOUS_APIC_VECTOR;
    apic_write_around(APIC_SPIV, value);

    /*
     * Set up LVT0, LVT1:
     *
     * set up through-local-APIC on the BP's LINT0. This is not
     * strictly necessery in pure symmetric-IO mode, but sometimes
     * we delegate interrupts to the 8259A.
     */
    /*
     * TODO: set up through-local-APIC from through-I/O-APIC? --macro
     */
    value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
    if (!smp_processor_id() && (pic_mode || !value)) {
        value = APIC_DM_EXTINT;
        apic_printk(APIC_VERBOSE, "enabled ExtINT on CPU#%d\n",
                    smp_processor_id());
    } else {
        value = APIC_DM_EXTINT | APIC_LVT_MASKED;
        apic_printk(APIC_VERBOSE, "masked ExtINT on CPU#%d\n",
                    smp_processor_id());
    }
    apic_write_around(APIC_LVT0, value);

    /*
     * only the BP should see the LINT1 NMI signal, obviously.
     */
    if (!smp_processor_id())
        value = APIC_DM_NMI;
    else
        value = APIC_DM_NMI | APIC_LVT_MASKED;
    if (!APIC_INTEGRATED(ver))      /* 82489DX */
        value |= APIC_LVT_LEVEL_TRIGGER;
    apic_write_around(APIC_LVT1, value);

    if (APIC_INTEGRATED(ver) && !esr_disable) {        /* !82489DX */
        maxlvt = get_maxlvt();
        if (maxlvt > 3)     /* Due to the Pentium erratum 3AP. */
            apic_write(APIC_ESR, 0);
        oldvalue = apic_read(APIC_ESR);

        value = ERROR_APIC_VECTOR;      // enables sending errors
        apic_write_around(APIC_LVTERR, value);
        /*
         * spec says clear errors after enabling vector.
         */
        if (maxlvt > 3)
            apic_write(APIC_ESR, 0);
        value = apic_read(APIC_ESR);
        if (value != oldvalue)
            apic_printk(APIC_VERBOSE, "ESR value before enabling "
                        "vector: 0x%08lx  after: 0x%08lx\n",
                        oldvalue, value);
    } else {
        if (esr_disable)    
            /* 
             * Something untraceble is creating bad interrupts on 
             * secondary quads ... for the moment, just leave the
             * ESR disabled - we can't do anything useful with the
             * errors anyway - mbligh
             */
            printk("Leaving ESR disabled.\n");
        else
            printk("No ESR for 82489DX.\n");
    }

    if (nmi_watchdog == NMI_LOCAL_APIC)
        setup_apic_nmi_watchdog();
}

/*
 * Detect and enable local APICs on non-SMP boards.
 * Original code written by Keir Fraser.
 */

/*
 * Knob to control our willingness to enable the local APIC.
 */
int enable_local_apic __initdata = 0; /* -1=force-disable, +1=force-enable */

static void __init lapic_disable(char *str)
{
    enable_local_apic = -1;
    clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
}
custom_param("nolapic", lapic_disable);

static void __init lapic_enable(char *str)
{
    enable_local_apic = 1;
}
custom_param("lapic", lapic_enable);

static void __init apic_set_verbosity(char *str)
{
    if (strcmp("debug", str) == 0)
        apic_verbosity = APIC_DEBUG;
    else if (strcmp("verbose", str) == 0)
        apic_verbosity = APIC_VERBOSE;
    else
        printk(KERN_WARNING "APIC Verbosity level %s not recognised"
               " use apic_verbosity=verbose or apic_verbosity=debug", str);
}
custom_param("apic_verbosity", apic_set_verbosity);

static int __init detect_init_APIC (void)
{
    u32 h, l, features;

    /* Disabled by kernel option? */
    if (enable_local_apic < 0)
        return -1;

    /* Workaround for us being called before identify_cpu(). */
    /*get_cpu_vendor(&boot_cpu_data); Not for Xen */

    switch (boot_cpu_data.x86_vendor) {
    case X86_VENDOR_AMD:
        if ((boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model > 1) ||
            (boot_cpu_data.x86 == 15))        
            break;
        goto no_apic;
    case X86_VENDOR_INTEL:
        if (boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15 ||
            (boot_cpu_data.x86 == 5 && cpu_has_apic))
            break;
        goto no_apic;
    default:
        goto no_apic;
    }

    if (!cpu_has_apic) {
        /*
         * Over-ride BIOS and try to enable the local
         * APIC only if "lapic" specified.
         */
        if (enable_local_apic <= 0) {
            printk("Local APIC disabled by BIOS -- "
                   "you can enable it with \"lapic\"\n");
            return -1;
        }
        /*
         * Some BIOSes disable the local APIC in the
         * APIC_BASE MSR. This can only be done in
         * software for Intel P6 or later and AMD K7
         * (Model > 1) or later.
         */
        rdmsr(MSR_IA32_APICBASE, l, h);
        if (!(l & MSR_IA32_APICBASE_ENABLE)) {
            printk("Local APIC disabled by BIOS -- reenabling.\n");
            l &= ~MSR_IA32_APICBASE_BASE;
            l |= MSR_IA32_APICBASE_ENABLE | APIC_DEFAULT_PHYS_BASE;
            wrmsr(MSR_IA32_APICBASE, l, h);
            enabled_via_apicbase = 1;
        }
    }
    /*
     * The APIC feature bit should now be enabled
     * in `cpuid'
     */
    features = cpuid_edx(1);
    if (!(features & (1 << X86_FEATURE_APIC))) {
        printk("Could not enable APIC!\n");
        return -1;
    }

    set_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
    mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

    /* The BIOS may have set up the APIC at some other address */
    rdmsr(MSR_IA32_APICBASE, l, h);
    if (l & MSR_IA32_APICBASE_ENABLE)
        mp_lapic_addr = l & MSR_IA32_APICBASE_BASE;

    if (nmi_watchdog != NMI_NONE)
        nmi_watchdog = NMI_LOCAL_APIC;

    printk("Found and enabled local APIC!\n");

    return 0;

no_apic:
    printk("No local APIC present or hardware disabled\n");
    return -1;
}

void __init init_apic_mappings(void)
{
    unsigned long apic_phys;

    /*
     * If no local APIC can be found then set up a fake all
     * zeroes page to simulate the local APIC and another
     * one for the IO-APIC.
     */
    if (!smp_found_config && detect_init_APIC())
        apic_phys = __pa(alloc_xenheap_page());
    else
        apic_phys = mp_lapic_addr;

    set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
    apic_printk(APIC_VERBOSE, "mapped APIC to %08lx (%08lx)\n", APIC_BASE,
                apic_phys);

    /*
     * Fetch the APIC ID of the BSP in case we have a
     * default configuration (or the MP table is broken).
     */
    if (boot_cpu_physical_apicid == -1U)
        boot_cpu_physical_apicid = GET_APIC_ID(apic_read(APIC_ID));

#ifdef CONFIG_X86_IO_APIC
    {
        unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
        int i;

        for (i = 0; i < nr_ioapics; i++) {
            if (smp_found_config) {
                ioapic_phys = mp_ioapics[i].mpc_apicaddr;
                if (!ioapic_phys) {
                    printk(KERN_ERR
                           "WARNING: bogus zero IO-APIC "
                           "address found in MPTABLE, "
                           "disabling IO/APIC support!\n");
                    smp_found_config = 0;
                    skip_ioapic_setup = 1;
                    goto fake_ioapic_page;
                }
            } else {
fake_ioapic_page:
                ioapic_phys = __pa(alloc_xenheap_page());
            }
            set_fixmap_nocache(idx, ioapic_phys);
            apic_printk(APIC_VERBOSE, "mapped IOAPIC to %08lx (%08lx)\n",
                        __fix_to_virt(idx), ioapic_phys);
            idx++;
        }
    }
#endif
}

/*****************************************************************************
 * APIC calibration
 * 
 * The APIC is programmed in bus cycles.
 * Timeout values should specified in real time units.
 * The "cheapest" time source is the cyclecounter.
 * 
 * Thus, we need a mappings from: bus cycles <- cycle counter <- system time
 * 
 * The calibration is currently a bit shoddy since it requires the external
 * timer chip to generate periodic timer interupts. 
 *****************************************************************************/

/* used for system time scaling */
static unsigned long bus_freq;    /* KAF: pointer-size avoids compile warns. */
static u32           bus_cycle;   /* length of one bus cycle in pico-seconds */
static u32           bus_scale;   /* scaling factor convert ns to bus cycles */

/*
 * The timer chip is already set up at HZ interrupts per second here,
 * but we do not accept timer interrupts yet. We only allow the BP
 * to calibrate.
 */
static unsigned int __init get_8254_timer_count(void)
{
    /*extern spinlock_t i8253_lock;*/
    /*unsigned long flags;*/

    unsigned int count;

    /*spin_lock_irqsave(&i8253_lock, flags);*/

    outb_p(0x00, PIT_MODE);
    count = inb_p(PIT_CH0);
    count |= inb_p(PIT_CH0) << 8;

    /*spin_unlock_irqrestore(&i8253_lock, flags);*/

    return count;
}

/* next tick in 8254 can be caught by catching timer wraparound */
static void __init wait_8254_wraparound(void)
{
    unsigned int curr_count, prev_count;
    
    curr_count = get_8254_timer_count();
    do {
        prev_count = curr_count;
        curr_count = get_8254_timer_count();

        /* workaround for broken Mercury/Neptune */
        if (prev_count >= curr_count + 0x100)
            curr_count = get_8254_timer_count();
        
    } while (prev_count >= curr_count);
}

/*
 * Default initialization for 8254 timers. If we use other timers like HPET,
 * we override this later
 */
void (*wait_timer_tick)(void) __initdata = wait_8254_wraparound;

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice on the boot CPU, once with a bogus timeout
 * value, second time for real. The other (noncalibrating) CPUs
 * call this function only once, with the real, calibrated value.
 *
 * We do reads before writes even if unnecessary, to get around the
 * P5 APIC double write bug.
 */

#define APIC_DIVISOR 1

void __setup_APIC_LVTT(unsigned int clocks)
{
    unsigned int lvtt_value, tmp_value, ver;

    ver = GET_APIC_VERSION(apic_read(APIC_LVR));
    /* NB. Xen uses local APIC timer in one-shot mode. */
    lvtt_value = /*APIC_LVT_TIMER_PERIODIC |*/ LOCAL_TIMER_VECTOR;
    if (!APIC_INTEGRATED(ver))
        lvtt_value |= SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV);
    apic_write_around(APIC_LVTT, lvtt_value);

    tmp_value = apic_read(APIC_TDCR);
    apic_write_around(APIC_TDCR, (tmp_value | APIC_TDR_DIV_1));

    apic_write_around(APIC_TMICT, clocks/APIC_DIVISOR);
}

static void __init setup_APIC_timer(unsigned int clocks)
{
    unsigned long flags;
    local_irq_save(flags);
    __setup_APIC_LVTT(clocks);
    local_irq_restore(flags);
}

/*
 * In this function we calibrate APIC bus clocks to the external
 * timer. Unfortunately we cannot use jiffies and the timer irq
 * to calibrate, since some later bootup code depends on getting
 * the first irq? Ugh.
 *
 * We want to do the calibration only once since we
 * want to have local timer irqs syncron. CPUs connected
 * by the same APIC bus have the very same bus frequency.
 * And we want to have irqs off anyways, no accidental
 * APIC irq that way.
 */

int __init calibrate_APIC_clock(void)
{
    unsigned long long t1 = 0, t2 = 0;
    long tt1, tt2;
    long result;
    int i;
    const int LOOPS = HZ/10;

    apic_printk(APIC_VERBOSE, "calibrating APIC timer ...\n");

    /*
     * Put whatever arbitrary (but long enough) timeout
     * value into the APIC clock, we just want to get the
     * counter running for calibration.
     */
    __setup_APIC_LVTT(1000000000);

    /*
     * The timer chip counts down to zero. Let's wait
     * for a wraparound to start exact measurement:
     * (the current tick might have been already half done)
     */
    wait_timer_tick();

    /*
     * We wrapped around just now. Let's start:
     */
    if (cpu_has_tsc)
        rdtscll(t1);
    tt1 = apic_read(APIC_TMCCT);

    /*
     * Let's wait LOOPS wraprounds:
     */
    for (i = 0; i < LOOPS; i++)
        wait_timer_tick();

    tt2 = apic_read(APIC_TMCCT);
    if (cpu_has_tsc)
        rdtscll(t2);

    /*
     * The APIC bus clock counter is 32 bits only, it
     * might have overflown, but note that we use signed
     * longs, thus no extra care needed.
     *
     * underflown to be exact, as the timer counts down ;)
     */

    result = (tt1-tt2)*APIC_DIVISOR/LOOPS;

    if (cpu_has_tsc)
        apic_printk(APIC_VERBOSE, "..... CPU clock speed is "
                    "%ld.%04ld MHz.\n",
                    ((long)(t2-t1)/LOOPS)/(1000000/HZ),
                    ((long)(t2-t1)/LOOPS)%(1000000/HZ));

    apic_printk(APIC_VERBOSE, "..... host bus clock speed is "
		"%ld.%04ld MHz.\n",
		result/(1000000/HZ),
		result%(1000000/HZ));

    /* set up multipliers for accurate timer code */
    bus_freq   = result*HZ;
    bus_cycle  = (u32) (1000000000000LL/bus_freq); /* in pico seconds */
    bus_scale  = (1000*262144)/bus_cycle;

    apic_printk(APIC_VERBOSE, "..... bus_scale = 0x%08X\n", bus_scale);
    /* reset APIC to zero timeout value */
    __setup_APIC_LVTT(0);

    return result;
}

unsigned int get_apic_bus_scale(void)
{
    return bus_scale;
}

static unsigned int calibration_result;

void __init setup_boot_APIC_clock(void)
{
    apic_printk(APIC_VERBOSE, "Using local APIC timer interrupts.\n");
    using_apic_timer = 1;

    local_irq_disable();
    
    calibration_result = calibrate_APIC_clock();
    /*
     * Now set up the timer for real.
     */
    setup_APIC_timer(calibration_result);
    
    local_irq_enable();
}

void __init setup_secondary_APIC_clock(void)
{
    setup_APIC_timer(calibration_result);
}

void __init disable_APIC_timer(void)
{
    if (using_apic_timer) {
        unsigned long v;
        
        v = apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT, v | APIC_LVT_MASKED);
    }
}

void enable_APIC_timer(void)
{
    if (using_apic_timer) {
        unsigned long v;
        
        v = apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT, v & ~APIC_LVT_MASKED);
    }
}

#undef APIC_DIVISOR

/*
 * reprogram the APIC timer. Timeoutvalue is in ns from start of boot
 * returns 1 on success
 * returns 0 if the timeout value is too small or in the past.
 */
int reprogram_timer(s_time_t timeout)
{
    s_time_t    now;
    s_time_t    expire;
    u64         apic_tmict;

    /*
     * We use this value because we don't trust zero (we think it may just
     * cause an immediate interrupt). At least this is guaranteed to hold it
     * off for ages (esp. since the clock ticks on bus clock, not cpu clock!).
     */
    if ( timeout == 0 )
    {
        apic_tmict = 0xffffffff;
        goto reprogram;
    }

    now = NOW();
    expire = timeout - now; /* value from now */

    if ( expire <= 0 )
    {
        Dprintk("APICT[%02d] Timeout in the past 0x%08X%08X > 0x%08X%08X\n", 
                smp_processor_id(), (u32)(now>>32), 
                (u32)now, (u32)(timeout>>32),(u32)timeout);
        return 0;
    }

    /*
     * If we don't have local APIC then we just poll the timer list off the
     * PIT interrupt. Cheesy but good enough to work on eg. VMware :-)
     */
    if ( !cpu_has_apic )
        return 1;

    /* conversion to bus units */
    apic_tmict = (((u64)bus_scale) * expire)>>18;

    if ( apic_tmict >= 0xffffffff )
    {
        Dprintk("APICT[%02d] Timeout value too large\n", smp_processor_id());
        apic_tmict = 0xffffffff;
    }

    if ( apic_tmict == 0 )
    {
        Dprintk("APICT[%02d] timeout value too small\n", smp_processor_id());
        return 0;
    }

 reprogram:
    /* Program the timer. */
    apic_write(APIC_TMICT, (unsigned long)apic_tmict);

    return 1;
}

void smp_apic_timer_interrupt(struct cpu_user_regs * regs)
{
    ack_APIC_irq();
    perfc_incrc(apic_timer);
    raise_softirq(TIMER_SOFTIRQ);
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
asmlinkage void smp_spurious_interrupt(struct cpu_user_regs *regs)
{
    unsigned long v;

    /*
     * Check if this really is a spurious interrupt and ACK it
     * if it is a vectored one.  Just in case...
     * Spurious interrupts should not be ACKed.
     */
    v = apic_read(APIC_ISR + ((SPURIOUS_APIC_VECTOR & ~0x1f) >> 1));
    if (v & (1 << (SPURIOUS_APIC_VECTOR & 0x1f)))
        ack_APIC_irq();

    /* see sw-dev-man vol 3, chapter 7.4.13.5 */
    printk(KERN_INFO "spurious APIC interrupt on CPU#%d, should never happen.\n",
           smp_processor_id());
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */

asmlinkage void smp_error_interrupt(struct cpu_user_regs *regs)
{
    unsigned long v, v1;

    /* First tickle the hardware, only then report what went on. -- REW */
    v = apic_read(APIC_ESR);
    apic_write(APIC_ESR, 0);
    v1 = apic_read(APIC_ESR);
    ack_APIC_irq();
    atomic_inc(&irq_err_count);

    /* Here is what the APIC error bits mean:
       0: Send CS error
       1: Receive CS error
       2: Send accept error
       3: Receive accept error
       4: Reserved
       5: Send illegal vector
       6: Received illegal vector
       7: Illegal register address
    */
    printk (KERN_DEBUG "APIC error on CPU%d: %02lx(%02lx)\n",
            smp_processor_id(), v , v1);
}

/*
 * This initializes the IO-APIC and APIC hardware if this is
 * a UP kernel.
 */
int __init APIC_init_uniprocessor (void)
{
    if (enable_local_apic < 0)
        clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);

    if (!smp_found_config && !cpu_has_apic)
        return -1;

    /*
     * Complain if the BIOS pretends there is one.
     */
    if (!cpu_has_apic && APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
        printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
               boot_cpu_physical_apicid);
        return -1;
    }

    verify_local_APIC();

    connect_bsp_APIC();

    phys_cpu_present_map = physid_mask_of_physid(boot_cpu_physical_apicid);

    setup_local_APIC();

    if (nmi_watchdog == NMI_LOCAL_APIC)
        check_nmi_watchdog();
#ifdef CONFIG_X86_IO_APIC
    if (smp_found_config)
        if (!skip_ioapic_setup && nr_ioapics)
            setup_IO_APIC();
#endif
    setup_boot_APIC_clock();

    return 0;
}
