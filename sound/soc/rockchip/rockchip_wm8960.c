/*
 * rockchip-wm8960-card.c
 *
 * ROCKCHIP ALSA SoC DAI driver for wm8960 audio on rockchip processors.
 * Copyright (c) 2014, ROCKCHIP CORPORATION. All rights reserved.
 * Authors: Yakir Yang <ykk@rock-chips.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/pcm_params.h>

#include "rockchip_i2s.h"
#include "../codecs/wm8960.h"

#define DRV_NAME "rockchip-audio-wm8960"

static int rockchip_wm8960_audio_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = substream->private_data;
	struct snd_soc_dai *codec_dai = runtime->codec_dai;
	struct snd_soc_dai *cpu_dai = runtime->cpu_dai;
	unsigned int dai_fmt = runtime->dai_link->dai_fmt;
	int mclk, ret;
	int dac_div, adc_div, div;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		printk("failed to set cpu_dai fmt.\n");
		return ret;
	}
	printk("success to set cpu_dai fmt.\n");

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		printk("failed to set cpu_dai sysclk.\n");
		return ret;
	}
	printk("success to set cpu_dai sysclk %d.\n", mclk);

	/* ensure codec_dai fmt set to Slave Mode */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("can't set codec dai fmt (%d)\n", ret);
		return ret;
	}
	printk("success to set codec_dai fmt.\n");

	/* codec using mclk as resource PLL*/
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_SYSCLKDIV,
			       WM8960_SYSCLK_DIV_1 | WM8960_SYSCLK_MCLK);

	/* calcu the ADC & DAC sample rate parameter */
	div = mclk / params_rate(params) * 10 / 256;
	if (div == 10) {
		adc_div = dac_div = 0;
	} else if (div == 15 ) {
		adc_div = dac_div = 2;
	} else if (div == 55) {
		adc_div = dac_div = 5;
	} else {
		adc_div = dac_div = div / 10;
	}
	adc_div = adc_div << 6;
	dac_div = dac_div << 3;
	
	/* setting ACL sample rate */
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_ACL_SR, params_rate(params));

	/* setting De-emphasis rate */
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_DEEMPH, params_rate(params));

	/* setting DAC sample rate */
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_DACDIV, dac_div);

	/* setting ADC sample rate */
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_ADCDIV, adc_div);

	/* setting Class D switch clock, between 700KHz to 800KHz */
	snd_soc_dai_set_clkdiv(codec_dai, WM8960_DCLKDIV, WM8960_DCLK_DIV_16);

	/* caclu the LRCK sample rate div */
	div = mclk / params_rate(params) / 256;
	if (div == 6) {
		div = 6;
		/* setting SCLK to MCLK/6, divider from MCLK */
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 4);
	} else {
		div = 4;
		/* setting SCLK to MCLK/6, divider from MCLK */
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
	}

	/* setting LRCK to SCLK/xxx, divider from SCLK */
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,
			       (mclk / div) / params_rate(params) - 1);

	return 0;
}

static struct snd_soc_ops wm8960_audio_dai_ops = {
	.hw_params = rockchip_wm8960_audio_hw_params,
};

static int wm8960_audio_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_enable_pin(dapm, "Headset Mic");
	snd_soc_dapm_enable_pin(dapm, "Headphone");
	snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	snd_soc_dapm_enable_pin(dapm, "Int Mic");

	snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link wm8960_audio_dai = {
	.name = "RockchipWM8960",
	.stream_name = "RockchipWM8960",
	.codec_dai_name = "wm8960-hifi",
	.init = wm8960_audio_init,
	.ops = &wm8960_audio_dai_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
};

static const struct snd_soc_dapm_widget rockchip_wm8960_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route rockchip_wm8960_dapm_routes[]= {
	{"Ext Spk",   NULL, "SPK_LP"},
	{"Ext Spk",   NULL, "SPK_LN"},
	{"Ext Spk",   NULL, "SPK_RN"},
	{"Ext Spk",   NULL, "SPK_RN"},
	{"Headphone", NULL, "HP_L"},
	{"Headphone", NULL, "HP_R"},
	{"Int Mic",   NULL, "LINPUT1"},
	{"Headset Mic", NULL, "RINPUT1"},
};

static const struct snd_kcontrol_new rockchip_wm8960_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static struct snd_soc_card rockchip_wm8960_audio_card = {
	.name = "RockchipWM8960",
	.owner = THIS_MODULE,
	.dai_link = &wm8960_audio_dai,
	.num_links = 1,
	.dapm_widgets = rockchip_wm8960_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rockchip_wm8960_dapm_widgets),
	.dapm_routes = rockchip_wm8960_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rockchip_wm8960_dapm_routes),
	.controls = rockchip_wm8960_controls,
	.num_controls = ARRAY_SIZE(rockchip_wm8960_controls),
};

static int rockchip_wm8960_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &rockchip_wm8960_audio_card;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	wm8960_audio_dai.cpu_of_node = of_parse_phandle(np, "rockchip,i2s-controller", 0);
	if (!wm8960_audio_dai.cpu_of_node) {
		dev_err(&pdev->dev, "Property 'i2s-controller' missing !\n");
		goto free_priv_data;
	}

	wm8960_audio_dai.codec_of_node = of_parse_phandle(np, "rockchip,audio-codec", 0);
	if (!wm8960_audio_dai.codec_of_node) {
		dev_err(&pdev->dev, "Property 'audio-codec' missing !\n");
		goto free_priv_data;
	}
	wm8960_audio_dai.platform_of_node = wm8960_audio_dai.cpu_of_node;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "register card failed (%d)\n", ret);
		card->dev = NULL;
		goto free_cpu_of_node;
	}

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret)
		return ret;

	dev_info(&pdev->dev, "wm8960 audio init success.\n");

	return 0;

free_cpu_of_node:
	wm8960_audio_dai.cpu_of_node = NULL;
	wm8960_audio_dai.platform_of_node = NULL;
free_priv_data:
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	return ret;
}

static int rockchip_wm8960_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	return 0;
}

static const struct of_device_id rockchip_wm8960_audio_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-wm8960", },
	{},
};

static struct platform_driver rockchip_wm8960_audio_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_wm8960_audio_of_match,
	},
	.probe = rockchip_wm8960_audio_probe,
	.remove = rockchip_wm8960_audio_remove,
};
module_platform_driver(rockchip_wm8960_audio_driver);

MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip wm8960 Audio ASoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_wm8960_audio_of_match);
