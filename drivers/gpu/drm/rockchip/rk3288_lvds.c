/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      hjc <hjc@rock-chips.com>
 *      Mark Yao <mark.yao@rock-chips.com>
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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <video/display_timing.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"
#include "rk3288_lvds.h"

#define DISPLAY_OUTPUT_RGB		0
#define DISPLAY_OUTPUT_LVDS		1
#define DISPLAY_OUTPUT_DUAL_LVDS	2

#define connector_to_lvds(c) \
		container_of(c, struct rk3288_lvds, connector)

#define encoder_to_lvds(c) \
		container_of(c, struct rk3288_lvds, encoder)

/*
 * @grf_offset: offset inside the grf regmap for setting the rockchip lvds
 */
struct rk3288_lvds_soc_data {
	int grf_gpio1d_iomux;
	int grf_soc_con6;
	int grf_soc_con7;
};

struct rk3288_lvds {
	void *base;
	struct device *dev;
	void __iomem *regs;
	struct regmap *grf;
	struct clk *pclk;
	struct rk3288_lvds_soc_data *soc_data;

	int output;
	int format;
	
	struct drm_device *drm_dev;
	struct drm_panel *panel;
	struct drm_connector connector;
	struct drm_encoder encoder;
	int dpms;
};

static inline void lvds_writel(struct rk3288_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
	writel_relaxed(val, lvds->regs + offset + 0x100);
	dsb();
}

static inline int lvds_name_to_format(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "jeida", 6) == 0)
		return LVDS_FORMAT_JEIDA;
	else if (strncmp(s, "vesa", 6) == 0)
		return LVDS_FORMAT_VESA;

	return -EINVAL;
}

static inline int lvds_name_to_output(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "rgb", 3) == 0)
		return DISPLAY_OUTPUT_RGB;
	else if (strncmp(s, "lvds", 4) == 0)
		return DISPLAY_OUTPUT_LVDS;
	else if (strncmp(s, "duallvds", 8) == 0)
		return DISPLAY_OUTPUT_DUAL_LVDS;

	return -EINVAL;
}

static struct rk3288_lvds_soc_data soc_data[2] = {
	{.grf_gpio1d_iomux = 0x000c,
	 .grf_soc_con6 = 0x025c,
	 .grf_soc_con7 = 0x0260},
};

static const struct of_device_id rk3288_lvds_dt_ids[] = {
	{.compatible = "rockchip,rk3288-lvds",
	 .data = (void *)&soc_data[0] },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_lvds_dt_ids);

static void rk3288_lvds_poweron(struct drm_encoder *encoder)
{
	struct rk3288_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	if (lvds->panel)
		lvds->panel->funcs->enable(lvds->panel);

	/* enable clk */
	ret = clk_enable(lvds->pclk);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to enable lvds pclk %d\n", ret);
		return;
	}
	
	/* enable pll */
	writel(0x00, lvds->regs + LVDS_CFG_REG_C);
	/* enable tx*/
	writel(0x92, lvds->regs + LVDS_CFG_REG_21);
}

static void rk3288_lvds_poweroff(struct drm_encoder *encoder)
{
	struct rk3288_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con7, 0xffff8000);
	if (ret != 0)
		dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);

	/*disable tx*/
	writel(0x00, lvds->regs + LVDS_CFG_REG_21);
	/*disable pll*/
	writel(0xff, lvds->regs + LVDS_CFG_REG_C);

	if (lvds->panel)
		lvds->panel->funcs->disable(lvds->panel);
	
	clk_disable(lvds->pclk);
}

static enum drm_connector_status
rockchip_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_connector_destroy(struct drm_connector *connector)
{
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs rockchip_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = rockchip_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_connector_destroy,
};

static int rockchip_connector_get_modes(struct drm_connector *connector)
{
	struct rk3288_lvds *lvds = connector_to_lvds(connector);
	struct drm_panel *panel = lvds->panel;

	return panel->funcs->get_modes(panel);
}

static struct drm_encoder *
	rockchip_connector_best_encoder(struct drm_connector *connector)
{
	struct rk3288_lvds *lvds = connector_to_lvds(connector);

	return &lvds->encoder;
}

static enum drm_mode_status rockchip_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_connector_helper_funcs rockchip_connector_helper_funcs = {
	.get_modes = rockchip_connector_get_modes,
	.mode_valid = rockchip_connector_mode_valid,
	.best_encoder = rockchip_connector_best_encoder,
};

static void rockchip_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct rk3288_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->dpms == mode)
		return;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		rk3288_lvds_poweron(encoder);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		rk3288_lvds_poweroff(encoder);
		break;
	default:
		break;
	}

	lvds->dpms = mode;
}

static bool
rockchip_drm_encoder_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void rockchip_drm_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rk3288_lvds *lvds = encoder_to_lvds(encoder);
	u32 h_bp = mode->htotal - mode->hsync_start;
	u8 pin_hsync = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 1 : 0;
	u8 pin_dclk = (mode->flags & DRM_MODE_FLAG_PCSYNC) ? 1 : 0;
	u32 val;
	int ret;

	val = lvds->format;
	if (lvds->output == DISPLAY_OUTPUT_DUAL_LVDS)
		val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
	else if (lvds->output == DISPLAY_OUTPUT_LVDS)
		val |= LVDS_CH0_EN;
	else if (lvds->output == DISPLAY_OUTPUT_RGB)
		val |= LVDS_TTL_EN | LVDS_CH0_EN | LVDS_CH1_EN;

	if (h_bp & 0x01)
		val |= LVDS_START_PHASE_RST_1;

	val |= (pin_dclk << 8) | (pin_hsync << 9);
	val |= (0xffff << 16);
	ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con7, val);
	if (ret != 0) {
		dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
		return;
	}

	if (lvds->output == DISPLAY_OUTPUT_RGB) {
		val = 0x007f007f;
		ret = regmap_write(lvds->grf, lvds->soc_data->grf_gpio1d_iomux,
				   val);
		if (ret != 0) {
			dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
			return;
		}

		lvds_writel(lvds, LVDS_CH0_REG_0, 0x7f);
		lvds_writel(lvds, LVDS_CH0_REG_1, 0x40);
		lvds_writel(lvds, LVDS_CH0_REG_2, 0x00);
		lvds_writel(lvds, LVDS_CH0_REG_4, 0x3f);
		lvds_writel(lvds, LVDS_CH0_REG_5, 0x3f);
		lvds_writel(lvds, LVDS_CH0_REG_3, 0x46);
		lvds_writel(lvds, LVDS_CH0_REG_D, 0x0a);
		lvds_writel(lvds, LVDS_CH0_REG_20, 0x44);
		lvds_writel(lvds, 0x100, 0x7f);
		lvds_writel(lvds, 0x104, 0x40);
		lvds_writel(lvds, 0x108, 0x00);
		lvds_writel(lvds, 0x10c, 0x46);
		lvds_writel(lvds, 0x110, 0x3f);
		lvds_writel(lvds, 0x114, 0x3f);
		lvds_writel(lvds, 0x134, 0x0a);
	} else {
		lvds_writel(lvds, LVDS_CH0_REG_0, 0xbf);
		lvds_writel(lvds, LVDS_CH0_REG_1, 0x3f);
		lvds_writel(lvds, LVDS_CH0_REG_2, 0xfe);
		lvds_writel(lvds, LVDS_CH0_REG_3, 0x46);
		lvds_writel(lvds, LVDS_CH0_REG_4, 0x00);
		lvds_writel(lvds, LVDS_CH0_REG_D, 0x0a);
		lvds_writel(lvds, LVDS_CH0_REG_20, 0x44);
	}
}

static void rockchip_drm_encoder_prepare(struct drm_encoder *encoder)
{
	struct rk3288_lvds *lvds = encoder_to_lvds(encoder);
	u32 val;
	int ret;

	ret = rockchip_drm_crtc_enable_out_mode(encoder->crtc,
						lvds->connector.connector_type,
						ROCKCHIP_OUT_MODE_P888);
	if (ret < 0) {
		dev_err(lvds->dev, "Could not set crtc mode config: %d.\n", ret);
		return;
	}

	ret = rockchip_drm_encoder_get_mux_id(lvds->dev->of_node, encoder);
	if (ret < 0)
		return;

	if (ret)
		val = LVDS_SEL_VOP_LIT | (LVDS_SEL_VOP_LIT << 16);
	else
		val = LVDS_SEL_VOP_LIT << 16;
	ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con6, val);
	if (ret != 0) {
		dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
		return;
	}
}

static void rockchip_drm_encoder_commit(struct drm_encoder *encoder)
{
	rockchip_drm_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void rockchip_drm_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_plane *plane;
	struct drm_device *dev = encoder->dev;

	rockchip_drm_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* all planes connected to this encoder should be also disabled. */
	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		if (plane->crtc && (plane->crtc == encoder->crtc))
			plane->funcs->disable_plane(plane);
	}
}

static struct drm_encoder_helper_funcs rockchip_encoder_helper_funcs = {
	.dpms = rockchip_drm_encoder_dpms,
	.mode_fixup = rockchip_drm_encoder_mode_fixup,
	.mode_set = rockchip_drm_encoder_mode_set,
	.prepare = rockchip_drm_encoder_prepare,
	.commit = rockchip_drm_encoder_commit,
	.disable = rockchip_drm_encoder_disable,
};

static void rockchip_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs rockchip_encoder_funcs = {
	.destroy = rockchip_drm_encoder_destroy,
};

static int rk3288_lvds_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct resource *res;
	struct platform_device *pdev = to_platform_device(dev);
	struct rk3288_lvds *lvds = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_device *drm_dev = data;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->regs)) {
		dev_err(dev, "ioremap reg failed\n");
		return PTR_ERR(lvds->regs);
	}

	lvds->pclk = devm_clk_get(&pdev->dev, "pclk_lvds");
	if (IS_ERR(lvds->pclk)) {
		dev_err(dev, "get clk failed\n");
		return PTR_ERR(lvds->pclk);
	}

	ret = clk_prepare(lvds->pclk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare pclk\n");
		return ret;
	}

	lvds->drm_dev = drm_dev;

	encoder = &lvds->encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS);
	if (ret) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		goto err_unprepare_pclk;
	}

	drm_encoder_helper_add(encoder, &rockchip_encoder_helper_funcs);

	connector = &lvds->connector;
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_connector_init(drm_dev, connector,
				 &rockchip_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector,
				 &rockchip_connector_helper_funcs);

	ret = drm_sysfs_connector_add(connector);
	if (ret) {
		DRM_ERROR("failed to add drm_sysfs\n");
		goto err_free_connector;
	}

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector_sysfs;
	}

	ret = drm_panel_attach(lvds->panel, connector);
	if (ret) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector_sysfs;
	}

	return 0;

err_free_connector_sysfs:
	drm_sysfs_connector_remove(connector);
err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_unprepare_pclk:
	clk_unprepare(lvds->pclk);
	return ret;
}

static void rk3288_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rk3288_lvds *lvds = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &lvds->encoder;

	if (lvds->panel)
		drm_panel_detach(lvds->panel);

	rockchip_drm_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
	encoder->funcs->destroy(encoder);
	drm_sysfs_connector_remove(&lvds->connector);
	drm_connector_cleanup(&lvds->connector);
	drm_encoder_cleanup(encoder);
}
static const struct component_ops rk3288_lvds_component_ops = {
	.bind = rk3288_lvds_bind,
	.unbind = rk3288_lvds_unbind,
};

static int rk3288_lvds_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct drm_panel *panel;
	struct device_node *panel_node;
	struct rk3288_lvds *lvds;
	const char *name;
	int i;

	if (!dev->of_node) {
		dev_err(dev, "can't find lvds devices\n");
		return -ENODEV;
	}

	panel_node = of_parse_phandle(dev->of_node, "rockchip,panel", 0);
	if (!panel_node) {
		DRM_ERROR("failed to find diaplay panel\n");
		return -ENODEV;
	}

	panel = of_drm_find_panel(panel_node);
	if (!panel) {
		DRM_ERROR("failed to find diaplay panel\n");
		of_node_put(panel_node);
		return -EPROBE_DEFER;
	}

	of_node_put(panel_node);

	lvds = devm_kzalloc(&pdev->dev, sizeof(struct rk3288_lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	if (of_property_read_string(dev->of_node, "rockchip,output", &name))
		/* default set it as output rgb */
		lvds->output = DISPLAY_OUTPUT_RGB;
	else
		lvds->output = lvds_name_to_output(name);

	if (of_property_read_string(dev->of_node, "rockchip,data-mapping", &name))
		/* default set it as format jeida */
		lvds->format = LVDS_FORMAT_JEIDA;
	else
		lvds->format = lvds_name_to_format(name);

	if (of_property_read_u32(dev->of_node, "rockchip,data-width", &i)) {
		lvds->format |= LVDS_24BIT;
	} else {
		if (i == 24) {
			lvds->format |= LVDS_24BIT;
		} else if (i == 18) {
			lvds->format |= LVDS_18BIT;
		} else {
			dev_err(&pdev->dev,
				"rockchip-lvds unsupport data-width[%d]\n", i);
			return -EINVAL;
		}
	}

	lvds->dev = dev;
	lvds->panel = panel;
	match = of_match_node(rk3288_lvds_dt_ids, dev->of_node);
	lvds->soc_data = (struct rk3288_lvds_soc_data *)match->data;

	platform_set_drvdata(pdev, lvds);

	return component_add(dev, &rk3288_lvds_component_ops);
}

static int rk3288_lvds_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rk3288_lvds_component_ops);

	return 0;
}

struct platform_driver rk3288_lvds_driver = {
	.probe = rk3288_lvds_probe,
	.remove = rk3288_lvds_remove,
	.driver = {
		   .name = "rk3288-lvds",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3288_lvds_dt_ids),
	},
};

module_platform_driver(rk3288_lvds_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP LVDS Driver");
MODULE_LICENSE("GPL v2");
