/*
 * gcw0.c  --  SoC audio for GCW Zero
 *
 * PISCES JZ4770 board support: (was used as a base)
 * Copyright (C) Ingenic Semiconductor Inc.
 *
 * GCW Zero specific board support:
 * Copyright (C) 2012, Maarten ter Huurne <maarten@treewalker.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>


struct gcw0 {
	struct gpio_desc *spk_gpio;
	struct gpio_desc *av_gpio;
};

/* Headphone jack: plug insert detection */

static struct snd_soc_jack gcw0_avout_jack;
static struct snd_soc_card gcw0_card;

static struct snd_soc_jack_pin gcw0_avout_jack_pins[] = {
	{
		.pin		= "Headphone",
		.mask		= SND_JACK_HEADPHONE,
		.invert		= 1,
	},
	{
		.pin		= "Speakers",
		.mask		= SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio gcw0_avout_jack_gpios[] = {
	{
		.name		= "detect",
		.report		= SND_JACK_HEADPHONE,
		.debounce_time	= 200,
	},
};

/* Headphone and speaker switches */

static int gcw0_avout_event(struct snd_soc_dapm_widget *widget,
			    struct snd_kcontrol *ctrl, int event)
{
	struct gcw0 *gcw0 = snd_soc_card_get_drvdata(widget->dapm->card);

	/* Delay the amp power up to reduce pops. */
	if (SND_SOC_DAPM_EVENT_ON(event))
		msleep(50);

	gpiod_set_value_cansleep(gcw0->av_gpio, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int gcw0_speaker_event(struct snd_soc_dapm_widget *widget,
			      struct snd_kcontrol *ctrl, int event)
{
	struct gcw0 *gcw0 = snd_soc_card_get_drvdata(widget->dapm->card);

	gpiod_set_value_cansleep(gcw0->spk_gpio, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static const struct snd_kcontrol_new gcw0_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
};

/* GCW0 machine dapm widgets */
static const struct snd_soc_dapm_widget gcw0_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", gcw0_avout_event),
	SND_SOC_DAPM_SPK("Speakers", gcw0_speaker_event),
	SND_SOC_DAPM_LINE("FM Radio", NULL),
	SND_SOC_DAPM_MIC("Built-in Mic", NULL),
};

/* GCW0 machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route gcw0_routes[] = {
	/* Destination Widget(sink)  <=== Path Name <=== Source Widget */

	{ "Headphone", NULL, "LHPOUT" },
	{ "Headphone", NULL, "RHPOUT" },

	{ "Speakers", NULL, "LOUT" },
	{ "Speakers", NULL, "ROUT" },

	{ "LLINEIN", NULL, "FM Radio" },
	{ "RLINEIN", NULL, "FM Radio" },

	{ "MIC1P", NULL, "Mic Bias" },
	{ "MIC1N", NULL, "Mic Bias" }, /* no such connection, but not harm */
	{ "Built-in Mic", NULL, "Mic Bias" },
};

/*
 * GCW0 for a jz_icdc as connected on jz4770 Device
 */
static int gcw0_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	/* set up codec pins not used */
	snd_soc_dapm_nc_pin(dapm, "MIC2P");
	snd_soc_dapm_nc_pin(dapm, "MIC2N");

	/* set up headphone plug detection */
	ret = snd_soc_card_jack_new(&gcw0_card, "Headphone Jack",
			       SND_JACK_VIDEOOUT | SND_JACK_HEADPHONE,
			       &gcw0_avout_jack, gcw0_avout_jack_pins,
			       ARRAY_SIZE(gcw0_avout_jack_pins));
	if (ret < 0) {
		dev_err(codec->dev,
			"Failed to create headphone jack: %d\n", ret);
	} else {
		ret = snd_soc_jack_add_gpiods(gcw0_card.dev,
				&gcw0_avout_jack,
				ARRAY_SIZE(gcw0_avout_jack_gpios),
				gcw0_avout_jack_gpios);
		if (ret < 0)
			dev_err(codec->dev,
				"Failed to add headphone jack GPIO: %d\n", ret);
	}

	/* set endpoints to default mode */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
					   SND_SOC_DAIFMT_NB_NF |
					   SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cpu dai format: %d\n", ret);
		return ret;
	}

	return 0;
}

/* digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link gcw0_dai = {
	.name			= "JZ_ICDC",
	.stream_name		= "JZ_ICDC",
	.cpu_dai_name		= "jz4770-i2s",
	.platform_name		= "jz4770-pcm-audio",
	.codec_dai_name		= "jz4770-hifi",
	.codec_name		= "jz4770-icdc",
	.init			= gcw0_codec_init,
};

static struct snd_soc_card gcw0_card = {
	.name			= "GCW0",
	.owner			= THIS_MODULE,
	.dai_link		= &gcw0_dai,
	.num_links		= 1,

	.controls		= gcw0_controls,
	.num_controls		= ARRAY_SIZE(gcw0_controls),

	.dapm_widgets		= gcw0_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(gcw0_widgets),
	.dapm_routes		= gcw0_routes,
	.num_dapm_routes	= ARRAY_SIZE(gcw0_routes),
};

static const struct of_device_id ingenic_asoc_gcw0_dt_ids[] = {
	{ .compatible = "ingenic,gcw0-audio", },
	{ }
};

static int gcw0_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &gcw0_card;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec, *i2s;
	struct gcw0 *gcw0;
	int ret;

	gcw0 = devm_kzalloc(&pdev->dev, sizeof(*gcw0), GFP_KERNEL);
	if (!gcw0)
		return -ENOMEM;

	i2s = of_parse_phandle(np, "ingenic,i2s-controller", 0);
	codec = of_parse_phandle(np, "ingenic,codec", 0);

	if (!i2s || !codec) {
		dev_warn(&pdev->dev,
			 "Phandle not found for i2s/codecs, using defaults\n");
	} else {
		dev_dbg(&pdev->dev, "Setting dai_link parameters\n");
		gcw0_dai.cpu_of_node = i2s;
		gcw0_dai.cpu_dai_name = NULL;
		gcw0_dai.platform_of_node = i2s;
		gcw0_dai.platform_name = NULL;
		gcw0_dai.codec_of_node = codec;
		gcw0_dai.codec_name = NULL;
	}

	gcw0->av_gpio = devm_gpiod_get(&pdev->dev, "avout", GPIOD_OUT_HIGH);
	if (IS_ERR(gcw0->av_gpio)) {
		ret = PTR_ERR(gcw0->av_gpio);
		dev_err(&pdev->dev, "Failed to request avout GPIO: %d\n", ret);
		return ret;
	}

	gcw0->spk_gpio = devm_gpiod_get(&pdev->dev, "speaker", GPIOD_OUT_HIGH);
	if (IS_ERR(gcw0->spk_gpio)) {
		ret = PTR_ERR(gcw0->spk_gpio);
		dev_err(&pdev->dev,
				"Failed to request speaker GPIO: %d\n", ret);
		return ret;
	}

	card->dev = &pdev->dev;

	snd_soc_card_set_drvdata(card, gcw0);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int gcw0_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_jack_free_gpios(&gcw0_avout_jack,
				ARRAY_SIZE(gcw0_avout_jack_gpios),
				gcw0_avout_jack_gpios);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver gcw0_driver = {
	.driver		= {
		.name	= "gcw0-audio",
		.of_match_table = of_match_ptr(ingenic_asoc_gcw0_dt_ids),
	},
	.probe		= gcw0_probe,
	.remove		= gcw0_remove,
};

module_platform_driver(gcw0_driver);

MODULE_AUTHOR("Maarten ter Huurne <maarten@treewalker.org>");
MODULE_DESCRIPTION("ALSA SoC GCW0 Audio support");
MODULE_LICENSE("GPL v2");
