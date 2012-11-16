/*
 * rk29_rt5621.c  --  SoC audio for rockchip
 *
 * Driver for rockchip rt5621 audio
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/rk29_iomap.h>
#include "../codecs/rt5621.h"
#include "rk29_pcm.h"
#include "rk29_i2s.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static int rk29_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0; 
	unsigned int lrclk = 0;
	int ret;
	  
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);    

	/*by Vincent Hsiung for EQ Vol Change*/
	#define HW_PARAMS_FLAG_EQVOL_ON 0x21
	#define HW_PARAMS_FLAG_EQVOL_OFF 0x22
	if ((params->flags == HW_PARAMS_FLAG_EQVOL_ON)||(params->flags == HW_PARAMS_FLAG_EQVOL_OFF))
	{
		ret = codec_dai->driver->ops->hw_params(substream, params, codec_dai); //by Vincent
		DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	} else {
		/* set codec DAI configuration */
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
		ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS); 
#endif
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
		ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM ); 
#endif
		if (ret < 0)
			return ret; 
		/* set cpu DAI configuration */
#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE) 
		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
#endif
#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);	
#endif
		if (ret < 0)
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
	default:
		DBG("Enter:%s, %d, Error rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
		return -EINVAL;
		break;
	}
	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));

#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
#if 0	//use pll from blck
	/*Set the pll of rt5621,the Pll source from BITCLK on CPU is master mode*/
	//bitclk is 64fs           
	ret=snd_soc_dai_set_pll(codec_dai,RT5621_PLL_FR_BCLK,params_rate(params)*64,pll_out);
	if (ret < 0) { 
		DBG("rk29_hw_params_rt5621:failed to set the pll for codec side\n"); 
		return ret;
	}
#endif	    
	/*Set the system clk for codec*/
	ret=snd_soc_dai_set_sysclk(codec_dai, 0,pll_out,SND_SOC_CLOCK_IN);
	if (ret < 0) {
		DBG("rk29_hw_params_rt5621:failed to set the sysclk for codec side\n"); 
		return ret;
	}	    
#endif
  

  #if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 

	if((24576000%params_rate(params))==0)	//for 8k,16k,32k,48k
	{
		snd_soc_dai_set_pll(codec_dai,RT5621_PLL_FR_MCLK,pll_out, 24576000);
		snd_soc_dai_set_sysclk(codec_dai,0, 24576000, SND_SOC_CLOCK_IN);			
	}
	else if((22579200%params_rate(params))==0)	//for 11k,22k,44k
	{
		snd_soc_dai_set_pll(codec_dai,RT5621_PLL_FR_MCLK,pll_out, 22579200);
		snd_soc_dai_set_sysclk(codec_dai,0, 22579200, SND_SOC_CLOCK_IN);			
	}
      
#endif


#if defined (CONFIG_SND_RK29_CODEC_SOC_SLAVE)
	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, (pll_out/4)/params_rate(params)-1);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, 3);
#endif

	DBG("Enter:%s, %d, LRCK=%d\n",__FUNCTION__,__LINE__,(pll_out/4)/params_rate(params));
        
	return 0;
}

static const struct snd_soc_dapm_widget rt5621_dapm_widgets[] = {
	
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

/*
 * Logic for a rt5621 as connected on a rockchip board.
 */
static int rk29_rt5621_init(struct snd_soc_pcm_runtime *rtd)
{
        return 0;
}

static struct snd_soc_ops rk29_ops = {
	  .hw_params = rk29_hw_params,
};

static struct snd_soc_dai_link rk29_dai = {
	.name = "RT5621",
	.stream_name = "RT5621 PCM",
	.codec_name = "RT5621.0-001a",
	.platform_name = "rockchip-audio",
	.cpu_dai_name = "rk29_i2s.0",
	.codec_dai_name = "RT5621 HiFi",
	.init = rk29_rt5621_init,
	.ops = &rk29_ops,
};

static struct snd_soc_card snd_soc_card_rk29 = {
	.name = "RK29_RT5621",
	.dai_link = &rk29_dai,
	.num_links = 1,
};

static struct platform_device *rk29_snd_device;

static int __init audio_card_init(void)
{
	int ret =0;	
	
	//rk29_speaker = rk29_speaker_init(RK29_PIN6_PB6, GPIO_HIGH, 2, (200*1000*1000));

	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	rk29_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rk29_snd_device) {
		  DBG("platform device allocation failed\n");
		  ret = -ENOMEM;
		  return ret;
	}
	platform_set_drvdata(rk29_snd_device, &snd_soc_card_rk29);
	ret = platform_device_add(rk29_snd_device);
	if (ret) {
	    DBG("platform device add failed\n");
	    platform_device_put(rk29_snd_device);
		return ret;
	}
	return ret;
}

static void __exit audio_card_exit(void)
{
	platform_device_unregister(rk29_snd_device);
}

module_init(audio_card_init);
module_exit(audio_card_exit);
/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
