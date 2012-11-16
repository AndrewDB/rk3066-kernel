/* arch/arm/mach-rk30/io.c
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/page.h>
#include <asm/mach/map.h>

#define RK30_DEVICE(name) { \
		.virtual = (unsigned long) RK30_##name##_BASE, \
		.pfn = __phys_to_pfn(RK30_##name##_PHYS), \
		.length = RK30_##name##_SIZE, \
		.type = MT_DEVICE, \
	 }

static struct map_desc rk30_io_desc[] __initdata = {
	RK30_DEVICE(CORE),
	RK30_DEVICE(CPU_AXI_BUS),
#if CONFIG_RK_DEBUG_UART == 0
	RK30_DEVICE(UART0),
#elif CONFIG_RK_DEBUG_UART == 1
	RK30_DEVICE(UART1),
#elif CONFIG_RK_DEBUG_UART == 2
	RK30_DEVICE(UART2),
#elif CONFIG_RK_DEBUG_UART == 3
	RK30_DEVICE(UART3),
#endif
	RK30_DEVICE(GRF),
	RK30_DEVICE(CRU),
	RK30_DEVICE(PMU),
	RK30_DEVICE(GPIO0),
	RK30_DEVICE(GPIO1),
	RK30_DEVICE(GPIO2),
	RK30_DEVICE(GPIO3),
	RK30_DEVICE(GPIO4),
	RK30_DEVICE(GPIO6),
	RK30_DEVICE(TIMER0),
	RK30_DEVICE(TIMER1),
	RK30_DEVICE(TIMER2),
	RK30_DEVICE(PWM01),
	RK30_DEVICE(PWM23),
	RK30_DEVICE(DDR_PCTL),
	RK30_DEVICE(DDR_PUBL),
	RK30_DEVICE(I2C1),

#ifdef CONFIG_RK_SRAM_DMA
	{
		.virtual = (unsigned long) RK30_IMEM_NONCACHED,
		.pfn = __phys_to_pfn(RK30_IMEM_PHYS),
		.length = RK30_IMEM_SIZE,
		.type = MT_MEMORY_NONCACHED,
	},
#endif
};

void __init rk30_map_common_io(void)
{
	iotable_init(rk30_io_desc, ARRAY_SIZE(rk30_io_desc));
}
