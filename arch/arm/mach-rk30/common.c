#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/pgtable-hwdef.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/sram.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/fiq.h>
#include <mach/pmu.h>
#include <mach/loader.h>
#include <mach/ddr.h>

static void __init rk30_cpu_axi_init(void)
{
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x0088);	// cpu0
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x0108);	// dmac1
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x0188);	// cpu1r
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x0388);	// cpu1w
#ifdef CONFIG_RK29_VMAC
	writel_relaxed(0xa, RK30_CPU_AXI_BUS_BASE + 0x4008);	// peri
#else
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x4008);	// peri
#endif
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x5008);	// gpu
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x6008);	// vpu
	writel_relaxed(0xa, RK30_CPU_AXI_BUS_BASE + 0x7008);	// lcdc0
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x7088);	// cif0
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x7108);	// ipp
	writel_relaxed(0xa, RK30_CPU_AXI_BUS_BASE + 0x7188);	// lcdc1
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x7208);	// cif1
	writel_relaxed(0x0, RK30_CPU_AXI_BUS_BASE + 0x7288);	// rga
	writel_relaxed(0x3f, RK30_CPU_AXI_BUS_BASE + 0x0014);	// memory scheduler read latency
	dsb();
}

#define L2_LY_SP_OFF (0)
#define L2_LY_SP_MSK (0x7)

#define L2_LY_RD_OFF (4)
#define L2_LY_RD_MSK (0x7)

#define L2_LY_WR_OFF (8)
#define L2_LY_WR_MSK (0x7)
#define L2_LY_SET(ly,off) (((ly)-1)<<(off))

static void __init rk30_l2_cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl, aux_ctrl_mask;

	writel_relaxed(L2_LY_SET(1,L2_LY_SP_OFF)
				|L2_LY_SET(1,L2_LY_RD_OFF)
				|L2_LY_SET(1,L2_LY_WR_OFF), RK30_L2C_BASE + L2X0_TAG_LATENCY_CTRL);
	writel_relaxed(L2_LY_SET(4,L2_LY_SP_OFF)
				|L2_LY_SET(6,L2_LY_RD_OFF)
				|L2_LY_SET(1,L2_LY_WR_OFF), RK30_L2C_BASE + L2X0_DATA_LATENCY_CTRL);

	/* L2X0 Prefetch Control */
	writel_relaxed(0x70000003, RK30_L2C_BASE + L2X0_PREFETCH_CTRL);

	/* L2X0 Power Control */
	writel_relaxed(L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN, RK30_L2C_BASE + L2X0_POWER_CTRL);

	/*
         * 16-way associativity, parity disabled
         * Way size - 32KB
         */
	aux_ctrl = ((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) | 	// round-robin
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	aux_ctrl_mask = ~((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) | // 16-way
			(0x1 << 25) | 	// round-robin
			(0x1 << 0) |		// Full Line of Zero Enable
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x7 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) | // 32KB way-size
			(0x1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT) );

	l2x0_init(RK30_L2C_BASE, aux_ctrl, aux_ctrl_mask);
#endif
}

static int boot_mode;
static void __init rk30_boot_mode_init(void)
{
	u32 boot_flag = readl_relaxed(RK30_PMU_BASE + PMU_SYS_REG0);
	boot_mode = readl_relaxed(RK30_PMU_BASE + PMU_SYS_REG1);

	if (boot_flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER)) {
		boot_mode = BOOT_MODE_RECOVERY;
	} else if (strstr(boot_command_line, "(parameter)")) {
		boot_mode = BOOT_MODE_RECOVERY;
	}
	if (boot_mode || boot_flag)
		printk("Boot mode: %d flag: 0x%08x\n", boot_mode, boot_flag);
}

int board_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(board_boot_mode);

void __init rk30_init_irq(void)
{
	gic_init(0, IRQ_LOCALTIMER, RK30_GICD_BASE, RK30_GICC_BASE);
#ifdef CONFIG_FIQ
	rk30_fiq_init();
#endif
	rk30_gpio_init();
}
int __init clk_disable_unused(void);

void __init rk30_map_io(void)
{
	rk30_map_common_io();
	rk29_setup_early_printk();
	rk30_cpu_axi_init();
	rk29_sram_init();
	board_clock_init();
	rk30_l2_cache_init();
	ddr_init(DDR_TYPE, DDR_FREQ);
	clk_disable_unused();
	rk30_iomux_init();
	rk30_boot_mode_init();
}

extern u32 ddr_get_cap(void);
static __init u32 rk30_get_ddr_size(void)
{
	u32 size;
	u32 v[3], a[3];
	u32 pgtbl = PAGE_OFFSET + TEXT_OFFSET - 0x4000;
	u32 flag = PMD_TYPE_SECT | PMD_SECT_XN | PMD_SECT_AP_WRITE | PMD_SECT_AP_READ;

	a[0] = pgtbl + (((u32)RK30_CPU_AXI_BUS_BASE >> 20) << 2);
	a[1] = pgtbl + (((u32)RK30_DDR_PUBL_BASE >> 20) << 2);
	a[2] = pgtbl + (((u32)RK30_GRF_BASE >> 20) << 2);
	v[0] = readl_relaxed(a[0]);
	v[1] = readl_relaxed(a[1]);
	v[2] = readl_relaxed(a[2]);
	writel_relaxed(flag | ((RK30_CPU_AXI_BUS_PHYS >> 20) << 20), a[0]);
	writel_relaxed(flag | ((RK30_DDR_PUBL_PHYS >> 20) << 20), a[1]);
	writel_relaxed(flag | ((RK30_GRF_PHYS >> 20) << 20), a[2]);

	size = ddr_get_cap();

	writel_relaxed(v[0], a[0]);
	writel_relaxed(v[1], a[1]);
	writel_relaxed(v[2], a[2]);

	return size;
}

void __init rk30_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = rk30_get_ddr_size();
}

