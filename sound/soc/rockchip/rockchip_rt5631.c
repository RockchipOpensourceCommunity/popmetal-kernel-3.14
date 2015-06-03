/*
 * rockchip-rt5631-card.c
 *
 * ROCKCHIP ALSA SoC DAI driver for rt5631 audio on rockchip processors.
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

#define DRV_NAME "rockchip-audio-rt5631"
#if 1
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static int rockchip_rt5631_audio_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int ret;

	DBG("Enter::%s----%d\n", __FUNCTION__, __LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for codec side\n", __FUNCTION__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for cpu side\n", __FUNCTION__);
		return ret;
	}

        switch(params_rate(params)) {
        case 8000:
        case 16000:
        case 24000:
        case 32000:
        case 48000:
                pll_out = 12288000;
                break;
        case 11025:
        case 22050:
        case 44100:
                pll_out = 11289600;
                break;
        case 96000:
        case 192000:
                pll_out = 12288000*2;
                break;
        case 88200:
        case 176400:
                pll_out = 11289600*2;
                break;
        default:
                DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
                return -EINVAL;
                break;
        }

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

	/*Set the system clk for codec*/
	ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
	if (ret < 0)
	{
		DBG("rk29_hw_params_rt5631:failed to set the sysclk for codec side\n");
		return ret;
	}

	//Codec is master, so is not need to set clkdiv for cpu.
	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM)
		return 0;

	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, 64-1);//bclk = 2*32*lrck; 2*32fs
	switch(params_rate(params)) {
        case 176400:
		case 192000:
			snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 1);
        DBG("Enter:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/2,params_rate(params));
			break;
		default:
			snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
        DBG("default:%s, %d, MCLK=%d BCLK=%d LRCK=%d\n",
		__FUNCTION__,__LINE__,pll_out,pll_out/4,params_rate(params));
			break;
	}

	return 0;
}

static struct snd_soc_ops rt5631_audio_dai_ops = {
	.hw_params = rockchip_rt5631_audio_hw_params,
};

static const struct snd_soc_dapm_widget rt5631_dapm_widgets[] = {

	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),

};
static const struct snd_soc_dapm_route audio_map[]={

	/* Mic Jack --> MIC_IN*/
	{"Mic Bias1", NULL, "Mic Jack"},
	{"MIC1", NULL, "Mic Bias1"},
	/* HP_OUT --> Headphone Jack */
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	/* LINE_OUT --> Ext Speaker */
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},

} ;
static int rt5631_audio_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

        /* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, rt5631_dapm_widgets,
				  ARRAY_SIZE(rt5631_dapm_widgets));
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        /* Set up specific audio path audio_mapnects */
        snd_soc_dapm_add_routes(dapm, audio_map, ARRAY_SIZE(audio_map));
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        snd_soc_dapm_nc_pin(dapm, "HP_L");
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	snd_soc_dapm_nc_pin(dapm, "HP_R");
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
        snd_soc_dapm_sync(dapm);
        DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	return 0;
}

static struct snd_soc_dai_link rt5631_audio_dai = {
	.name = "RockchipRT5631",
	.stream_name = "RockchipRT5631",
	.codec_dai_name = "rt5631-hifi",
	.init = rt5631_audio_init,
	.ops = &rt5631_audio_dai_ops,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
};

static const struct snd_soc_dapm_widget rockchip_rt5631_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_soc_dapm_route rockchip_rt5631_dapm_routes[]= {
	/* Mic Jack --> MIC_IN*/
	{"Mic Bias1", NULL, "Mic Jack"},
	{"MIC1", NULL, "Mic Bias1"},
	/* HP_OUT --> Headphone Jack */
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	/* LINE_OUT --> Ext Speaker */
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},
};

static const struct snd_kcontrol_new rockchip_rt5631_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	//SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Mic Jack"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static struct snd_soc_card rockchip_rt5631_audio_card = {
	.name = "RockchipRT5631",
	.owner = THIS_MODULE,
	.dai_link = &rt5631_audio_dai,
	.num_links = 1,
	.dapm_widgets = rt5631_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5631_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	//.controls = rockchip_rt5631_controls,
	//.num_controls = ARRAY_SIZE(rockchip_rt5631_controls),
};

static int rockchip_rt5631_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &rockchip_rt5631_audio_card;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	rt5631_audio_dai.cpu_of_node = of_parse_phandle(np, "rockchip,i2s-controller", 0);
	if (!rt5631_audio_dai.cpu_of_node) {
		dev_err(&pdev->dev, "Property 'i2s-controller' missing !\n");
		goto free_priv_data;
	}

	rt5631_audio_dai.codec_of_node = of_parse_phandle(np, "rockchip,audio-codec", 0);
	if (!rt5631_audio_dai.codec_of_node) {
		dev_err(&pdev->dev, "Property 'audio-codec' missing !\n");
		goto free_priv_data;
	}
	rt5631_audio_dai.platform_of_node = rt5631_audio_dai.cpu_of_node;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "register card failed (%d)\n", ret);
		card->dev = NULL;
		goto free_cpu_of_node;
	}

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret)
		return ret;

	dev_info(&pdev->dev, "rt5631 audio init success.\n");

	return 0;

free_cpu_of_node:
	rt5631_audio_dai.cpu_of_node = NULL;
	rt5631_audio_dai.platform_of_node = NULL;
free_priv_data:
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	return ret;
}

static int rockchip_rt5631_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	snd_soc_card_set_drvdata(card, NULL);
	platform_set_drvdata(pdev, NULL);
	card->dev = NULL;

	return 0;
}

static const struct of_device_id rockchip_rt5631_audio_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-rt5631", },
	{},
};

static struct platform_driver rockchip_rt5631_audio_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_rt5631_audio_of_match,
	},
	.probe = rockchip_rt5631_audio_probe,
	.remove = rockchip_rt5631_audio_remove,
};
module_platform_driver(rockchip_rt5631_audio_driver);

MODULE_AUTHOR("lxt <lxt@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rt5631 Audio ASoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_rt5631_audio_of_match);
