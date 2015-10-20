/*
 * Device Tree support for Rockchip SoCs
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irqchip.h>
#include <linux/memblock.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>
#include "core.h"
#include "pm.h"

#define SYS_LOADER_REBOOT_FLAG   0x5242C300  //high 24 bits is tag, low 8 bits is type
#define SYS_KERNRL_REBOOT_FLAG   0xC3524200  //high 24 bits is tag, low 8 bits is type

enum {
    BOOT_NORMAL = 0, /* normal boot */
    BOOT_LOADER,     /* enter loader rockusb mode */
    BOOT_MASKROM,    /* enter maskrom rockusb mode (not support now) */
    BOOT_RECOVER,    /* enter recover */
    BOOT_NORECOVER,  /* do not enter recover */
    BOOT_SECONDOS,   /* boot second OS (not support now)*/
    BOOT_WIPEDATA,   /* enter recover and wipe data. */
    BOOT_WIPEALL,    /* enter recover and wipe all data. */
    BOOT_CHECKIMG,   /* check firmware img with backup part(in loader mode)*/
    BOOT_FASTBOOT,   /* enter fast boot mode */
    BOOT_SECUREBOOT_DISABLE,
    BOOT_CHARGING,   /* enter charge mode */
    BOOT_MAX         /* MAX VALID BOOT TYPE.*/
};

static void __init rockchip_dt_init(void)
{
	l2x0_of_init(0, ~0UL);
	rockchip_suspend_init();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	platform_device_register_simple("cpufreq-cpu0", 0, NULL, 0);
}

extern struct ion_platform_data ion_pdata;
extern void __init ion_reserve(struct ion_platform_data *data);
extern int __init rockchip_ion_find_heap(unsigned long node,
				const char *uname, int depth, void *data);
void __init rockchip_ion_reserve(void)
{
#ifdef CONFIG_ION_ROCKCHIP
	printk("%s\n", __func__);
	of_scan_flat_dt(rockchip_ion_find_heap, (void*)&ion_pdata);
	ion_reserve(&ion_pdata);
#endif
}
static void __init rockchip_memory_init(void)
{
	memblock_reserve(0xfe000000, 0x1000000);
	/* reserve memory for ION */
	rockchip_ion_reserve();
}

void rockchip_restart_get_boot_mode(const char *cmd, u32 *flag, u32 *mode)
{
	*flag = SYS_LOADER_REBOOT_FLAG + BOOT_NORMAL;
	*mode = BOOT_MODE_REBOOT;

	if (cmd) {
		if (!strcmp(cmd, "loader") || !strcmp(cmd, "bootloader"))
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_LOADER;
		else if(!strcmp(cmd, "recovery"))
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_RECOVER;
		else if (!strcmp(cmd, "fastboot"))
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_FASTBOOT;
		else if (!strcmp(cmd, "charge")) {
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_CHARGING;
			*mode = BOOT_MODE_CHARGE;
		}
	} else {
		if (is_panic)
			*mode = BOOT_MODE_PANIC;
	}
}

static void rockchip_restart(char mode, const char *cmd)
{
	u32 boot_flag, boot_mode;

	rockchip_restart_get_boot_mode(cmd, &boot_flag, &boot_mode);

	writel_relaxed(boot_flag, RK_PMU_VIRT + RK3288_PMU_SYS_REG0);	// for loader
	writel_relaxed(boot_mode, RK_PMU_VIRT + RK3288_PMU_SYS_REG1);	// for linux
	dsb();

	/* pll enter slow mode */
	writel_relaxed(0xf3030000, RK_CRU_VIRT + RK3288_CRU_MODE_CON);
	dsb();
	writel_relaxed(0xeca8, RK_CRU_VIRT + RK3288_CRU_GLB_SRST_SND_VALUE);
	dsb();
}

static const char * const rockchip_board_dt_compat[] = {
	"rockchip,rk2928",
	"rockchip,rk3066a",
	"rockchip,rk3066b",
	"rockchip,rk3188",
	"rockchip,rk3288",
	NULL,
};

DT_MACHINE_START(ROCKCHIP_DT, "Rockchip (Device Tree)")
	.init_machine	= rockchip_dt_init,
	.dt_compat	= rockchip_board_dt_compat,
	.reserve        = rockchip_memory_init,
	.restart	= rockchip_restart,
MACHINE_END
