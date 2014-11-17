/**
 * dwmac-rk.c - Rockchip RK3288 DWMAC specific glue layer
 *
 * Copyright (C) 2014 Chen-Zhi (Roger Chen)
 *
 * Chen-Zhi (Roger Chen)  <roger.chen@rock-chips.com>
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

#include <linux/stmmac.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

struct rk_priv_data {
	struct platform_device *pdev;
	int phy_iface;
	bool power_ctrl_by_pmu;
	char pmu_regulator[32];
	int pmu_enable_level;

	int power_io;
	int power_io_level;
	int reset_io;
	int reset_io_level;
	int phyirq_io;
	int phyirq_io_level;

	bool clk_enabled;
	bool clock_input;

	struct clk *clk_mac;
	struct clk *clk_mac_pll;
	struct clk *gmac_clkin;
	struct clk *mac_clk_rx;
	struct clk *mac_clk_tx;
	struct clk *clk_mac_ref;
	struct clk *clk_mac_refout;
	struct clk *aclk_mac;
	struct clk *pclk_mac;

	int tx_delay;
	int rx_delay;

	struct regmap *grf;
};


#define RK3288_GRF_SOC_CON1 0x0248
#define RK3288_GRF_SOC_CON3 0x0250
#define RK3288_GRF_GPIO3D_E 0x01ec
#define RK3288_GRF_GPIO4A_E 0x01f0
#define RK3288_GRF_GPIO4B_E 0x01f4

/*RK3288_GRF_SOC_CON1*/
#define GMAC_PHY_INTF_SEL_RGMII ((0x01C0 << 16) | (0x0040))
#define GMAC_PHY_INTF_SEL_RMII  ((0x01C0 << 16) | (0x0100))
#define GMAC_FLOW_CTRL          ((0x0200 << 16) | (0x0200))
#define GMAC_FLOW_CTRL_CLR      ((0x0200 << 16) | (0x0000))
#define GMAC_SPEED_10M          ((0x0400 << 16) | (0x0000))
#define GMAC_SPEED_100M         ((0x0400 << 16) | (0x0400))
#define GMAC_RMII_CLK_25M       ((0x0800 << 16) | (0x0800))
#define GMAC_RMII_CLK_2_5M      ((0x0800 << 16) | (0x0000))
#define GMAC_CLK_125M           ((0x3000 << 16) | (0x0000))
#define GMAC_CLK_25M            ((0x3000 << 16) | (0x3000))
#define GMAC_CLK_2_5M           ((0x3000 << 16) | (0x2000))
#define GMAC_RMII_MODE          ((0x4000 << 16) | (0x4000))
#define GMAC_RMII_MODE_CLR      ((0x4000 << 16) | (0x0000))

/*RK3288_GRF_SOC_CON3*/
#define GMAC_TXCLK_DLY_ENABLE   ((0x4000 << 16) | (0x4000))
#define GMAC_TXCLK_DLY_DISABLE  ((0x4000 << 16) | (0x0000))
#define GMAC_RXCLK_DLY_ENABLE   ((0x8000 << 16) | (0x8000))
#define GMAC_RXCLK_DLY_DISABLE  ((0x8000 << 16) | (0x0000))
#define GMAC_CLK_RX_DL_CFG(val) ((0x3F80 << 16) | (val<<7))
#define GMAC_CLK_TX_DL_CFG(val) ((0x007F << 16) | (val))

static void set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	if (IS_ERR(bsp_priv->grf)) {
		pr_err("%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_PHY_INTF_SEL_RGMII);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     GMAC_RXCLK_DLY_ENABLE);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     GMAC_TXCLK_DLY_ENABLE);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     GMAC_CLK_RX_DL_CFG(rx_delay));
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     GMAC_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, RK3288_GRF_GPIO3D_E, 0xFFFFFFFF);
	regmap_write(bsp_priv->grf, RK3288_GRF_GPIO4B_E,
		     0x3<<2<<16 | 0x3<<2);
	regmap_write(bsp_priv->grf, RK3288_GRF_GPIO4A_E, 0xFFFFFFFF);

	pr_info("%s: tx delay=0x%x; rx delay=0x%x;\n",
		__func__, tx_delay, rx_delay);
}

static void set_to_rmii(struct rk_priv_data *bsp_priv)
{
	if (IS_ERR(bsp_priv->grf)) {
		pr_err("%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_PHY_INTF_SEL_RMII);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_RMII_MODE);
}

static void set_rgmii_speed(struct rk_priv_data *bsp_priv, int speed)
{
	if (IS_ERR(bsp_priv->grf)) {
		pr_err("%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	if (speed == 10)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_2_5M);
	else if (speed == 100)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_25M);
	else if (speed == 1000)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_125M);
	else
		pr_err("unknown speed value for RGMII! speed=%d", speed);
}

static void set_rmii_speed(struct rk_priv_data *bsp_priv, int speed)
{
	if (IS_ERR(bsp_priv->grf)) {
		pr_err("%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	if (speed == 10) {
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_RMII_CLK_2_5M);
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_SPEED_10M);
	} else if (speed == 100) {
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_RMII_CLK_25M);
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_SPEED_100M);
	} else {
		pr_err("unknown speed value for RMII! speed=%d", speed);
	}
}

#define MAC_CLK_RX	"mac_clk_rx"
#define MAC_CLK_TX	"mac_clk_tx"
#define CLK_MAC_REF	"clk_mac_ref"
#define CLK_MAC_REF_OUT	"clk_mac_refout"
#define CLK_MAC_PLL	"clk_mac_pll"
#define ACLK_MAC	"aclk_mac"
#define PCLK_MAC	"pclk_mac"
#define MAC_CLKIN	"ext_gmac"
#define CLK_MAC		"stmmaceth"

int gmac_clk_init(struct rk_priv_data *bsp_priv)
{
	struct device *dev = &(bsp_priv->pdev->dev);
	bsp_priv->clk_enabled = false;

	bsp_priv->mac_clk_rx = clk_get(dev, MAC_CLK_RX);
	if (IS_ERR(bsp_priv->mac_clk_rx))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, MAC_CLK_RX);

	bsp_priv->mac_clk_tx = clk_get(dev, MAC_CLK_TX);
	if (IS_ERR(bsp_priv->mac_clk_tx))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, MAC_CLK_TX);

	bsp_priv->clk_mac_ref = clk_get(dev, CLK_MAC_REF);
	if (IS_ERR(bsp_priv->clk_mac_ref))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, CLK_MAC_REF);

	bsp_priv->clk_mac_refout = clk_get(dev, CLK_MAC_REF_OUT);
	if (IS_ERR(bsp_priv->clk_mac_refout))
		pr_warn("%s: warning:cannot get clock %s\n",
			__func__, CLK_MAC_REF_OUT);

	bsp_priv->aclk_mac = clk_get(dev, ACLK_MAC);
	if (IS_ERR(bsp_priv->aclk_mac))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, ACLK_MAC);

	bsp_priv->pclk_mac = clk_get(dev, PCLK_MAC);
	if (IS_ERR(bsp_priv->pclk_mac))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, PCLK_MAC);

	bsp_priv->clk_mac_pll = clk_get(dev, CLK_MAC_PLL);
	if (IS_ERR(bsp_priv->clk_mac_pll))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, CLK_MAC_PLL);

	bsp_priv->gmac_clkin = clk_get(dev, MAC_CLKIN);
	if (IS_ERR(bsp_priv->gmac_clkin))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, MAC_CLKIN);

	bsp_priv->clk_mac = clk_get(dev, CLK_MAC);
	if (IS_ERR(bsp_priv->clk_mac))
		pr_warn("%s: warning: cannot get clock %s\n",
			__func__, CLK_MAC);

	if (bsp_priv->clock_input) {
		pr_info("%s: clock input from PHY\n", __func__);
	} else {
		if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII)
			clk_set_rate(bsp_priv->clk_mac_pll, 50000000);

		clk_set_parent(bsp_priv->clk_mac, bsp_priv->clk_mac_pll);
	}

	return 0;
}

static int gmac_clk_enable(struct rk_priv_data *bsp_priv, bool enable)
{
	int phy_iface = phy_iface = bsp_priv->phy_iface;

	if (enable) {
		if (!bsp_priv->clk_enabled) {
			if (phy_iface == PHY_INTERFACE_MODE_RMII) {
				if (!IS_ERR(bsp_priv->mac_clk_rx))
					clk_prepare_enable(
						bsp_priv->mac_clk_rx);

				if (!IS_ERR(bsp_priv->clk_mac_ref))
					clk_prepare_enable(
						bsp_priv->clk_mac_ref);

				if (!IS_ERR(bsp_priv->clk_mac_refout))
					clk_prepare_enable(
						bsp_priv->clk_mac_refout);
			}

			if (!IS_ERR(bsp_priv->aclk_mac))
				clk_prepare_enable(bsp_priv->aclk_mac);

			if (!IS_ERR(bsp_priv->pclk_mac))
				clk_prepare_enable(bsp_priv->pclk_mac);

			if (!IS_ERR(bsp_priv->mac_clk_tx))
				clk_prepare_enable(bsp_priv->mac_clk_tx);

			/**
			 * if (!IS_ERR(bsp_priv->clk_mac))
			 *	clk_prepare_enable(bsp_priv->clk_mac);
			 */
			mdelay(5);
			bsp_priv->clk_enabled = true;
		}
	} else {
		if (bsp_priv->clk_enabled) {
			if (phy_iface == PHY_INTERFACE_MODE_RMII) {
				if (!IS_ERR(bsp_priv->mac_clk_rx))
					clk_disable_unprepare(
						bsp_priv->mac_clk_rx);

				if (!IS_ERR(bsp_priv->clk_mac_ref))
					clk_disable_unprepare(
						bsp_priv->clk_mac_ref);

				if (!IS_ERR(bsp_priv->clk_mac_refout))
					clk_disable_unprepare(
						bsp_priv->clk_mac_refout);
			}

			if (!IS_ERR(bsp_priv->aclk_mac))
				clk_disable_unprepare(bsp_priv->aclk_mac);

			if (!IS_ERR(bsp_priv->pclk_mac))
				clk_disable_unprepare(bsp_priv->pclk_mac);

			if (!IS_ERR(bsp_priv->mac_clk_tx))
				clk_disable_unprepare(bsp_priv->mac_clk_tx);
			/**
			 * if (!IS_ERR(bsp_priv->clk_mac))
			 *	clk_disable_unprepare(bsp_priv->clk_mac);
			 */
			bsp_priv->clk_enabled = false;
		}
	}

	return 0;
}

static int power_on_by_pmu(struct rk_priv_data *bsp_priv, bool enable)
{
	struct regulator *ldo;
	char *ldostr = bsp_priv->pmu_regulator;
	int ret;

	if (ldostr == NULL) {
		pr_err("%s: no ldo found\n", __func__);
		return -1;
	}

	ldo = regulator_get(NULL, ldostr);
	if (ldo == NULL) {
		pr_err("\n%s get ldo %s failed\n", __func__, ldostr);
	} else {
		if (enable) {
			if (!regulator_is_enabled(ldo)) {
				regulator_set_voltage(ldo, 3300000, 3300000);
				ret = regulator_enable(ldo);
				if (ret != 0)
					pr_err("%s: faild to enable %s\n",
					       __func__, ldostr);
				else
					pr_info("turn on ldo done.\n");
			} else {
				pr_warn("%s is enabled before enable", ldostr);
			}
		} else {
			if (regulator_is_enabled(ldo)) {
				ret = regulator_disable(ldo);
				if (ret != 0)
					pr_err("%s: faild to disable %s\n",
					       __func__, ldostr);
				else
					pr_info("turn off ldo done.\n");
			} else {
				pr_warn("%s is disabled before disable",
					ldostr);
			}
		}
		regulator_put(ldo);
	}

	return 0;
}

static int power_on_by_gpio(struct rk_priv_data *bsp_priv, bool enable)
{
	if (enable) {
		/*power on*/
		if (gpio_is_valid(bsp_priv->power_io))
			gpio_direction_output(bsp_priv->power_io,
					      bsp_priv->power_io_level);
	} else {
		/*power off*/
		if (gpio_is_valid(bsp_priv->power_io))
			gpio_direction_output(bsp_priv->power_io,
					      !bsp_priv->power_io_level);
	}

	return 0;
}

static int phy_power_on(struct rk_priv_data *bsp_priv, bool enable)
{
	int ret = -1;

	pr_info("Ethernet PHY power %s\n", enable == 1 ? "on" : "off");

	if (bsp_priv->power_ctrl_by_pmu)
		ret = power_on_by_pmu(bsp_priv, enable);
	else
		ret =  power_on_by_gpio(bsp_priv, enable);

	if (enable) {
		/*reset*/
		if (gpio_is_valid(bsp_priv->reset_io)) {
			gpio_direction_output(bsp_priv->reset_io,
					      bsp_priv->reset_io_level);
			mdelay(5);
			gpio_direction_output(bsp_priv->reset_io,
					      !bsp_priv->reset_io_level);
		}
		mdelay(30);

	} else {
		/*pull down reset*/
		if (gpio_is_valid(bsp_priv->reset_io)) {
			gpio_direction_output(bsp_priv->reset_io,
					      bsp_priv->reset_io_level);
		}
	}

	return ret;
}

#define GPIO_PHY_POWER	"gmac_phy_power"
#define GPIO_PHY_RESET	"gmac_phy_reset"
#define GPIO_PHY_IRQ	"gmac_phy_irq"

static void *rk_gmac_setup(struct platform_device *pdev)
{
	struct rk_priv_data *bsp_priv;
	struct device *dev = &pdev->dev;
	enum of_gpio_flags flags;
	int ret;
	const char *strings = NULL;
	int value;
	int irq;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return ERR_PTR(-ENOMEM);

	bsp_priv->phy_iface = of_get_phy_mode(dev->of_node);

	ret = of_property_read_string(dev->of_node, "pmu_regulator", &strings);
	if (ret) {
		pr_err("%s: Can not read property: pmu_regulator.\n", __func__);
		bsp_priv->power_ctrl_by_pmu = false;
	} else {
		pr_info("%s: ethernet phy power controled by pmu(%s).\n",
			__func__, strings);
		bsp_priv->power_ctrl_by_pmu = true;
		strcpy(bsp_priv->pmu_regulator, strings);
	}

	ret = of_property_read_u32(dev->of_node, "pmu_enable_level", &value);
	if (ret) {
		pr_err("%s: Can not read property: pmu_enable_level.\n",
		       __func__);
		bsp_priv->power_ctrl_by_pmu = false;
	} else {
		pr_info("%s: PHY power controled by pmu(level = %s).\n",
			__func__, (value == 1) ? "HIGH" : "LOW");
		bsp_priv->power_ctrl_by_pmu = true;
		bsp_priv->pmu_enable_level = value;
	}

	ret = of_property_read_string(dev->of_node, "clock_in_out", &strings);
	if (ret) {
		pr_err("%s: Can not read property: clock_in_out.\n", __func__);
		bsp_priv->clock_input = true;
	} else {
		pr_info("%s: clock input or output? (%s).\n",
			__func__, strings);
		if (!strcmp(strings, "input"))
			bsp_priv->clock_input = true;
		else
			bsp_priv->clock_input = false;
	}

	ret = of_property_read_u32(dev->of_node, "tx_delay", &value);
	if (ret) {
		bsp_priv->tx_delay = 0x30;
		pr_err("%s: Can not read property: tx_delay.", __func__);
		pr_err("%s: set tx_delay to 0x%x\n",
		       __func__, bsp_priv->tx_delay);
	} else {
		pr_info("%s: TX delay(0x%x).\n", __func__, value);
		bsp_priv->tx_delay = value;
	}

	ret = of_property_read_u32(dev->of_node, "rx_delay", &value);
	if (ret) {
		bsp_priv->rx_delay = 0x10;
		pr_err("%s: Can not read property: rx_delay.", __func__);
		pr_err("%s: set rx_delay to 0x%x\n",
		       __func__, bsp_priv->rx_delay);
	} else {
		pr_info("%s: RX delay(0x%x).\n", __func__, value);
		bsp_priv->rx_delay = value;
	}

	bsp_priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,grf");
	if (IS_ERR(bsp_priv->grf))
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");

	bsp_priv->phyirq_io =
		of_get_named_gpio_flags(dev->of_node,
					"phyirq-gpio", 0, &flags);
	bsp_priv->phyirq_io_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;

	bsp_priv->reset_io =
		of_get_named_gpio_flags(dev->of_node,
					"reset-gpio", 0, &flags);
	bsp_priv->reset_io_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;

	bsp_priv->power_io =
		of_get_named_gpio_flags(dev->of_node, "power-gpio", 0, &flags);
	bsp_priv->power_io_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;

	/*power*/
	if (!gpio_is_valid(bsp_priv->power_io)) {
		pr_err("%s: Failed to get GPIO %s.\n",
		       __func__, "power-gpio");
	} else {
		ret = gpio_request(bsp_priv->power_io, GPIO_PHY_POWER);
		if (ret)
			pr_err("%s: ERROR: Failed to request GPIO %s.\n",
			       __func__, GPIO_PHY_POWER);
	}

	if (!gpio_is_valid(bsp_priv->reset_io)) {
		pr_err("%s: ERROR: Get reset-gpio failed.\n", __func__);
	} else {
		ret = gpio_request(bsp_priv->reset_io, GPIO_PHY_RESET);
		if (ret)
			pr_err("%s: ERROR: Failed to request GPIO %s.\n",
			       __func__, GPIO_PHY_RESET);
	}

	if (bsp_priv->phyirq_io > 0) {
		struct plat_stmmacenet_data *plat_dat;

		pr_info("PHY irq in use\n");
		ret = gpio_request(bsp_priv->phyirq_io, GPIO_PHY_IRQ);
		if (ret < 0) {
			pr_warn("%s: Failed to request GPIO %s\n",
				__func__, GPIO_PHY_IRQ);
			goto goon;
		}

		ret = gpio_direction_input(bsp_priv->phyirq_io);
		if (ret < 0) {
			pr_err("%s, Failed to set input for GPIO %s\n",
			       __func__, GPIO_PHY_IRQ);
			gpio_free(bsp_priv->phyirq_io);
			goto goon;
		}

		irq = gpio_to_irq(bsp_priv->phyirq_io);
		if (irq < 0) {
			ret = irq;
			pr_err("Failed to set irq for %s\n",
			       GPIO_PHY_IRQ);
			gpio_free(bsp_priv->phyirq_io);
			goto goon;
		}

		plat_dat = dev_get_platdata(&pdev->dev);
		if (plat_dat)
			plat_dat->mdio_bus_data->probed_phy_irq = irq;
		else
			pr_err("%s: plat_data is NULL\n", __func__);
	}

goon:
	/*rmii or rgmii*/
	if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RGMII) {
		pr_info("%s: init for RGMII\n", __func__);
		set_to_rgmii(bsp_priv, bsp_priv->tx_delay, bsp_priv->rx_delay);
	} else if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII) {
		pr_info("%s: init for RMII\n", __func__);
		set_to_rmii(bsp_priv);
	} else {
		pr_err("%s: ERROR: NO interface defined!\n", __func__);
	}

	bsp_priv->pdev = pdev;

	gmac_clk_init(bsp_priv);

	return bsp_priv;
}

static int rk_gmac_init(struct platform_device *pdev, void *priv)
{
	struct rk_priv_data *bsp_priv = priv;
	int ret;

	ret = phy_power_on(bsp_priv, true);
	if (ret)
		return ret;

	ret = gmac_clk_enable(bsp_priv, true);
	if (ret)
		return ret;


	return 0;
}

static void rk_gmac_exit(struct platform_device *pdev, void *priv)
{
	struct rk_priv_data *gmac = priv;

	phy_power_on(gmac, false);
	gmac_clk_enable(gmac, false);
}

static void rk_fix_speed(void *priv, unsigned int speed)
{
	struct rk_priv_data *bsp_priv = priv;

	if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RGMII)
		set_rgmii_speed(bsp_priv, speed);
	else if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII)
		set_rmii_speed(bsp_priv, speed);
	else
		pr_err("unsupported interface %d", bsp_priv->phy_iface);
}

const struct stmmac_of_data rk_gmac_data = {
	.has_gmac = 1,
	.tx_coe = 1,
	.fix_mac_speed = rk_fix_speed,
	.setup = rk_gmac_setup,
	.init = rk_gmac_init,
	.exit = rk_gmac_exit,
};
