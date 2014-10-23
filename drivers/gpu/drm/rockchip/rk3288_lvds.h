/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      hjc <hjc@rock-chips.com>
 *      mark yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _RK3288_LVDS_
#define _RK3288_LVDS_

#define LVDS_CH0_REG_0			0x00
#define LVDS_CH0_REG_1			0x04
#define LVDS_CH0_REG_2			0x08
#define LVDS_CH0_REG_3			0x0c
#define LVDS_CH0_REG_4			0x10
#define LVDS_CH0_REG_5			0x14
#define LVDS_CH0_REG_9			0x24
#define LVDS_CFG_REG_C			0x30
#define LVDS_CH0_REG_D			0x34
#define LVDS_CH0_REG_F			0x3c
#define LVDS_CH0_REG_20			0x80
#define LVDS_CFG_REG_21			0x84

#define LVDS_SEL_VOP_LIT		(1 << 3)

#define LVDS_FMT_MASK			(0x07 << 16)
#define LVDS_MSB			(0x01 << 3)
#define LVDS_DUAL			(0x01 << 4)
#define LVDS_FMT_1			(0x01 << 5)
#define LVDS_TTL_EN			(0x01 << 6)
#define LVDS_START_PHASE_RST_1		(0x01 << 7)
#define LVDS_DCLK_INV			(0x01 << 8)
#define LVDS_CH0_EN			(0x01 << 11)
#define LVDS_CH1_EN			(0x01 << 12)
#define LVDS_PWRDN			(0x01 << 15)

#define LVDS_24BIT		(0 << 1)
#define LVDS_18BIT		(1 << 1)
#define LVDS_FORMAT_VESA	(0 << 0)
#define LVDS_FORMAT_JEIDA	(1 << 0)
#endif /* _RK3288_LVDS_ */
