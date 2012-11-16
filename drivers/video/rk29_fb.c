/*
 * drivers/video/rk29_fb.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/wakelock.h>

#include <asm/io.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include "rk29_fb.h"

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif
#ifdef CONFIG_HDMI
#include <linux/completion.h>

#include <linux/hdmi.h>
#endif

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/pmu.h>
#include <mach/rk29-ipp.h>
#include <mach/ddr.h>

#include "./display/screen/screen.h"

#ifdef CONFIG_DISPLAY_LCD_SUPPORT
extern	void lcd_register_display_sysfs(void *devdata);
#endif

#define ANDROID_USE_THREE_BUFS  0       //android use three buffers to accelerate UI display in rgb plane
#define CURSOR_BUF_SIZE         256     //RK2818 cursor need 256B buf
int rk29_cursor_buf[CURSOR_BUF_SIZE];
char cursor_img[] = {
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x20,0x00,0x00,
0x00,0x30,0x00,0x00,
0x00,0x28,0x00,0x00,
0x00,0x24,0x00,0x00,
0x00,0x22,0x00,0x00,
0x00,0x21,0x00,0x00,
0x00,0x20,0x80,0x00,
0x00,0x20,0x40,0x00,
0x00,0x20,0x20,0x00,
0x00,0x20,0x10,0x00,
0x00,0x20,0x08,0x00,
0x00,0x20,0x7C,0x00,
0x00,0x22,0x40,0x00,
0x00,0x26,0x40,0x00,
0x00,0x29,0x20,0x00,
0x00,0x31,0x20,0x00,
0x00,0x20,0x90,0x00,
0x00,0x00,0x90,0x00,
0x00,0x00,0x48,0x00,
0x00,0x00,0x48,0x00,
0x00,0x00,0x30,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00
};

#if 0
	#define fbprintk(msg...)	printk(msg);
#else
	#define fbprintk(msg...)
#endif


#if 0
	#define fbprintk2(msg...)	printk(msg);
#else
	#define fbprintk2(msg...)
#endif

#define LcdReadBit(inf, addr, msk)      ((inf->regbak.addr=inf->preg->addr)&(msk))
#define LcdWrReg(inf, addr, val)        inf->preg->addr=inf->regbak.addr=(val)
#define LcdRdReg(inf, addr)             (inf->preg->addr)
#define LcdSetBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) |= (msk))
#define LcdClrBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) &= ~(msk))
#define LcdSetRegisterBit(inf, addr, msk)    inf->preg->addr=((inf->preg->addr) |= (msk))
#define LcdMskReg(inf, addr, msk, val)  (inf->regbak.addr)&=~(msk);   inf->preg->addr=(inf->regbak.addr|=(val))


#define IsMcuLandscape()                ((SCREEN_MCU==inf->cur_screen->type) && (0==inf->mcu_scandir))
#define IsMcuUseFmk()                   ( (2==inf->cur_screen->mcu_usefmk) || (1==inf->cur_screen->mcu_usefmk))

#define CalScaleW0(x, y)	             (((u32)x*0x1000)/y)

struct rk29fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

static struct rk29fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
};

struct win_set {
	volatile u32 y_offset;
	volatile u32 c_offset;
};

struct win0_par {
    u32 refcount;
    u32	pseudo_pal[16];
    u32 y_offset;
    u32 c_offset;
    u32 xpos;         //size in panel
    u32 ypos;
    u32 xsize;        //start point in panel
    u32 ysize;
    u32 format;

    wait_queue_head_t wait;
    struct win_set mirror;
    struct win_set displ;
    struct win_set done;

    u8 par_seted;
    u8 addr_seted;
};

/*
struct win1_par {
	u32 refcount;
	u32	pseudo_pal[16];
	int lstblank;
    u32 xpos;
    u32 ypos;
    u32 xsize;
    u32 ysize;
    u32 format;
    u32 addr_offset;
};
*/
struct rk29fb_scaleinfo {
	int xrate;	// screen scale rate in x direction
	int yrate;		// screen scale rate in y direction
};

static inline u32 calculate_res_scaled(u32 size, int rate)
{
	return size*rate/100;
}

struct rk29fb_inf {
    struct fb_info *fb1;
    struct fb_info *fb0;

    void __iomem *reg_vir_base;  // virtual basic address of lcdc register
	u32 reg_phy_base;       // physical basic address of lcdc register
	u32 len;               // physical map length of lcdc register
    u32 video_mode;

    struct clk      *clk;
    struct clk      *dclk;            //lcdc dclk
    struct clk      *aclk;   //lcdc share memory frequency
    struct clk      *aclk_parent;     //lcdc aclk divider frequency source
    struct clk      *aclk_ddr_lcdc;   //DDR LCDC AXI clock disable.
    struct clk      *aclk_disp_matrix;  //DISPLAY matrix AXI clock disable.
    struct clk      *hclk_cpu_display;  //CPU DISPLAY AHB bus clock disable.
    struct clk      *pd_display;        // display power domain
    unsigned long	dclk_rate;

    /* lcdc reg base address and backup reg */
    LCDC_REG *preg;
    LCDC_REG regbak;

	int in_suspend;
    int fb0_color_deepth;
    /* variable used in mcu panel */
	int mcu_needflush;
	int mcu_isrcnt;
	u16 mcu_scandir;
	struct timer_list mcutimer;
	int mcu_status;
	u8 mcu_fmksync;
	int mcu_usetimer;
	int mcu_stopflush;

    int setFlag;
    /* external memery */
	char __iomem *screen_base2;
    __u32 smem_len2;
    unsigned long  smem_start2;

    char __iomem *cursor_base;   /* cursor Virtual address*/
    __u32 cursor_size;           /* Amount of ioremapped VRAM or 0 */
    unsigned long  cursor_start;

    struct rk29fb_screen panel1_info;         // 1st panel, it's lcd normally
    struct rk29fb_screen panel2_info;         // 2nd panel
    struct rk29fb_screen *cur_screen;
#if 0 //def CONFIG_CPU_FREQ
    struct notifier_block freq_transition;
#endif
#if  defined(CONFIG_MACH_RK29_ITV_HOTDOG) || defined(CONFIG_MACH_RK29_ITV)
	struct rk29fb_scaleinfo	scaleinfo;
#endif
#ifdef CONFIG_FB_DSP_LUT
	u32	*dsp_lut;
#endif
};

typedef enum _TRSP_MODE
{
    TRSP_CLOSE = 0,
    TRSP_FMREG,
    TRSP_FMREGEX,
    TRSP_FMRAM,
    TRSP_FMRAMEX,
    TRSP_MASK,
    TRSP_INVAL
} TRSP_MODE;
#ifdef	CONFIG_FB_MIRRORING

int (*video_data_to_mirroring)(struct fb_info *info,u32 yuv_phy[2]) = NULL;
EXPORT_SYMBOL(video_data_to_mirroring);
	
#endif



struct platform_device *g_pdev = NULL;
//static int win1fb_set_par(struct fb_info *info);

#if 0
#define CHK_SUSPEND(inf)	\
	if(inf->in_suspend)	{	\
		fbprintk(">>>>>> fb is in suspend! return! \n");	\
		return -EPERM;	\
	}
#else
#define CHK_SUSPEND(inf)
#endif
static DECLARE_WAIT_QUEUE_HEAD(fb0_wait_queue);     
static volatile int idle_condition = 1;              //1:idel, 0:busy

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int wq_condition = 0;
static int wq_condition2 = 0;
static int fb1_open_init = 0;
#if ANDROID_USE_THREE_BUFS
static int new_frame_seted = 1;
#endif
static struct wake_lock idlelock; /* only for fb */
#ifdef CONFIG_FB_ROTATE_VIDEO	
//add by zyc
static bool has_set_rotate; 
static u32 last_yuv_phy[2] = {0,0};
#endif
int fb0_first_buff_bits = 32;
int fb0_second_buff_bits = 32;
int fb_compose_layer_count = 0;
static BLOCKING_NOTIFIER_HEAD(rk29fb_notifier_list);
int rk29fb_register_notifier(struct notifier_block *nb)
{
	int ret = 0;
	if (g_pdev) {
		struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
		if (inf) {
			if (inf->cur_screen && inf->cur_screen->type == SCREEN_HDMI)
				nb->notifier_call(nb, RK29FB_EVENT_HDMI_ON, inf->cur_screen);
			if (inf->fb1 && inf->fb1->par) {
				struct win0_par *par = inf->fb1->par;
				if (par->refcount)
					nb->notifier_call(nb, RK29FB_EVENT_FB1_ON, inf->cur_screen);
			}
		}
	}
	ret = blocking_notifier_chain_register(&rk29fb_notifier_list, nb);

	return ret;
}

int rk29fb_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&rk29fb_notifier_list, nb);
}

static int rk29fb_notify(struct rk29fb_inf *inf, unsigned long event)
{
	return blocking_notifier_call_chain(&rk29fb_notifier_list, event, inf->cur_screen);
}
#ifdef CONFIG_FB_DSP_LUT
#define DSP_LUT_ADDR    0x800
#define DSP_LUT_SIZE 	0x400
static int rk29_fb_lut_config(bool enable)
{
	struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
	printk(">>>>%s enable =%d\n",__func__,enable);
	if(enable && inf->dsp_lut){
		LcdClrBit(inf, SYS_CONFIG, m_DSIP_LUT_CTL);
		LcdWrReg(inf, REG_CFG_DONE, 0x01);
		msleep(30);
		if(inf->dsp_lut != NULL){
			memcpy((inf->reg_vir_base + (DSP_LUT_ADDR)),inf->dsp_lut,DSP_LUT_SIZE );
			LcdSetBit(inf, SYS_CONFIG, m_DSIP_LUT_CTL);
			LcdWrReg(inf, REG_CFG_DONE, 0x01);
		}
	}else{
		LcdClrBit(inf, SYS_CONFIG, m_DSIP_LUT_CTL);
		LcdWrReg(inf, REG_CFG_DONE, 0x01);
	}
	return true;
}

static ssize_t store_lut(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
	char *start = buf;
	int i=256,j,temp;
	int space_max = 10;

	if(!inf->dsp_lut) {
		inf->dsp_lut = (u32*)kmalloc(DSP_LUT_SIZE, GFP_KERNEL);
	}
	if(inf->dsp_lut) {		
		for(i=0;i<256;i++)
		{
			space_max = 10;  //max space number 10;
			inf->dsp_lut[i] = simple_strtoul(start,NULL,10);
			do
			{
				start++;
				space_max--;
			}while ((*start != ' ')&&space_max);
			
			if(!space_max)
				break;
			else
				start++;
		}
	
		rk29_fb_lut_config(1);
	}
	return count;
}

static ssize_t show_lut(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	return 0;
}

static struct device_attribute rk29fb_attrs[] = {
	__ATTR(dsp_lut,/*S_IRUGO | S_IWUSR*/0777 , show_lut, store_lut),
};
#endif
//$_rbox_$_modify_end
int mcu_do_refresh(struct rk29fb_inf *inf)
{
    if(inf->mcu_stopflush)  return 0;

    if(SCREEN_MCU!=inf->cur_screen->type)   return 0;

    // use frame mark
    if(IsMcuUseFmk())
    {
        inf->mcu_needflush = 1;
        return 0;
    }

    // not use frame mark
    if(LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_SELECT))
    {
        if(!LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST))
        {
            inf->mcu_needflush = 1;
        }
        else
        {
            if(inf->cur_screen->refresh)    inf->cur_screen->refresh(REFRESH_PRE);
            inf->mcu_needflush = 0;
            inf->mcu_isrcnt = 0;
            LcdSetRegisterBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST);
        }
    }
    return 0;
}


void mcutimer_callback(unsigned long arg)
{
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
    static int waitcnt = 0;

    mod_timer(&inf->mcutimer, jiffies + HZ/10);

    switch(inf->mcu_status)
    {
    case MS_IDLE:
        inf->mcu_status = MS_MCU;
        break;
    case MS_MCU:
        if(inf->mcu_usetimer)   mcu_do_refresh(inf);
        break;
    case MS_EWAITSTART:
        inf->mcu_status = MS_EWAITEND;
        waitcnt = 0;
        break;
    case MS_EWAITEND:
        if(0==waitcnt) {
            mcu_do_refresh(inf);
        }
        if(waitcnt++>14) {
            inf->mcu_status = MS_EEND;
        }
        break;
    case MS_EEND:
        inf->mcu_status = MS_MCU;
        break;
    default:
        inf->mcu_status = MS_MCU;
        break;
    }
}

int mcu_refresh(struct rk29fb_inf *inf)
{
    static int mcutimer_inited = 0;

    if(SCREEN_MCU!=inf->cur_screen->type)   return 0;

    if(!mcutimer_inited)
    {
        mcutimer_inited = 1;
        init_timer(&inf->mcutimer);
        inf->mcutimer.function = mcutimer_callback;
        inf->mcutimer.expires = jiffies + HZ/5;
        inf->mcu_status = MS_IDLE;
        add_timer(&inf->mcutimer);
    }

    if(MS_MCU==inf->mcu_status)     mcu_do_refresh(inf);

    return 0;
}

int mcu_ioctl(unsigned int cmd, unsigned long arg)
{
    struct rk29fb_inf *inf = NULL;
    if(!g_pdev)     return -1;

    inf = dev_get_drvdata(&g_pdev->dev);

    switch(cmd)
    {
    case MCU_WRCMD:
        LcdClrBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT);
        LcdWrReg(inf, MCU_BYPASS_WPORT, arg);
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT);
        break;

    case MCU_WRDATA:
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT);
        LcdWrReg(inf, MCU_BYPASS_WPORT, arg);
        break;

    case MCU_SETBYPASS:
        LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_BYPASSMODE_SELECT, v_MCU_BYPASSMODE_SELECT(arg));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;

    default:
        break;
    }

    return 0;
}

static irqreturn_t mcu_irqfmk(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct rk29fb_screen *screen;

    if(!inf)    return IRQ_HANDLED;

    screen = inf->cur_screen;

    if(0==screen->mcu_usefmk) {
        return IRQ_HANDLED;
    }

    if(inf->mcu_fmksync == 1)
        return IRQ_HANDLED;

    inf->mcu_fmksync = 1;
    if(inf->mcu_needflush)
    {
        inf->mcu_needflush = 0;
        inf->mcu_isrcnt = 0;
        if(inf->cur_screen->refresh)
           inf->cur_screen->refresh(REFRESH_PRE);
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST);
    }
    inf->mcu_fmksync = 0;

	return IRQ_HANDLED;
}

int init_lcdc(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    u32 msk=0, clr=0;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    inf->clk = clk_get(NULL, "hclk_lcdc");
    inf->aclk_ddr_lcdc = clk_get(NULL, "aclk_ddr_lcdc");
    inf->aclk_disp_matrix = clk_get(NULL, "aclk_disp_matrix");
    inf->hclk_cpu_display = clk_get(NULL, "hclk_cpu_display");
    inf->pd_display = clk_get(NULL, "pd_display");
    if ((IS_ERR(inf->clk)) ||
        (IS_ERR(inf->aclk_ddr_lcdc)) ||
        (IS_ERR(inf->aclk_disp_matrix)) ||
        (IS_ERR(inf->hclk_cpu_display)) ||
        (IS_ERR(inf->pd_display)))
    {
        printk(KERN_ERR "failed to get lcdc_hclk source\n");
        return PTR_ERR(inf->clk);
    }
    clk_enable(inf->aclk_disp_matrix);
    clk_enable(inf->hclk_cpu_display);
    clk_enable(inf->clk);
    clk_enable(inf->pd_display);
    //pmu_set_power_domain(PD_DISPLAY, 1);
    clk_enable(inf->aclk_ddr_lcdc);

	// set AHB access rule and disable all windows
    LcdWrReg(inf, SYS_CONFIG, 0x60000000);
    LcdWrReg(inf, SWAP_CTRL, 0);
    LcdWrReg(inf, FIFO_WATER_MARK, 0x00000862);//68
    LcdWrReg(inf, AXI_MS_ID, 0x54321);

	// and mcu holdmode; and set win1 top.
    LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST | m_MCU_BYPASSMODE_SELECT ,
            v_MCU_HOLDMODE_SELECT(0)| v_MCU_HOLDMODE_FRAME_ST(0) |v_MCU_BYPASSMODE_SELECT(0));

    // disable blank out, black out, tristate out, yuv2rgb bypass
    LcdMskReg(inf, BLEND_CTRL,m_W2_BLEND_EN | m_W1_BLEND_EN | m_W0_BLEND_EN | m_HWC_BLEND_EN |
             m_HWC_BLEND_FACTOR | m_W1_BLEND_FACTOR | m_W0_BLEND_FACTOR,
             v_W2_BLEND_EN(0) |v_W1_BLEND_EN(0) | v_W0_BLEND_EN(0) | v_HWC_BLEND_EN(0) |
             v_HWC_BLEND_FACTOR(0) | v_W2_BLEND_FACTOR(0) | v_W1_BLEND_FACTOR(0) | v_W0_BLEND_FACTOR(0)
             );

    LcdMskReg(inf, WIN0_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));

    LcdWrReg(inf, DSP_CTRL1, 0);

    // initialize all interrupt
    clr = v_HOR_STARTCLEAR(1) | v_FRM_STARTCLEAR(1) | v_SCANNING_CLEAR(1);

    msk = v_HOR_STARTMASK(1) | v_FRM_STARTMASK(0) | v_SCANNING_MASK(1);

    LcdWrReg(inf, INT_STATUS, clr | msk);

	// let above to take effect
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;
}

void load_screen(struct fb_info *info, bool initscreen)
{
    int ret = -EINVAL;
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    u16 face;
    u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
    u16 right_margin = screen->right_margin, lower_margin = screen->lower_margin;
    u16 x_res = screen->x_res, y_res = screen->y_res;
    u32 aclk_rate = 150000000;

    if(!g_pdev){
        printk(">>>>>> %s : %s no g_pdev\n", __FILE__, __FUNCTION__);
        return;
    }

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    // set the rgb or mcu
    LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_OUTPUT_SELECT, v_MCU_OUTPUT_SELECT((SCREEN_MCU==screen->type)?(1):(0)));

	// set out format and mcu timing
    mcu_total  = (screen->mcu_wrperiod*150*1000)/1000000;
    if(mcu_total>31)    mcu_total = 31;
    if(mcu_total<3)     mcu_total = 3;
    mcu_rwstart = (mcu_total+1)/4 - 1;
    mcu_rwend = ((mcu_total+1)*3)/4 - 1;
    mcu_csstart = (mcu_rwstart>2) ? (mcu_rwstart-3) : (0);
    mcu_csend = (mcu_rwend>15) ? (mcu_rwend-1) : (mcu_rwend);

    fbprintk(">> mcu_total=%d, mcu_rwstart=%d, mcu_csstart=%d, mcu_rwend=%d, mcu_csend=%d \n",
        mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend);

    LcdMskReg(inf, MCU_TIMING_CTRL,
             m_MCU_CS_ST | m_MCU_CS_END| m_MCU_RW_ST | m_MCU_RW_END |
             m_MCU_WRITE_PERIOD | m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST,
            v_MCU_CS_ST(mcu_csstart) | v_MCU_CS_END(mcu_csend) | v_MCU_RW_ST(mcu_rwstart) |
            v_MCU_RW_END(mcu_rwend) |  v_MCU_WRITE_PERIOD(mcu_total) |
            v_MCU_HOLDMODE_SELECT((SCREEN_MCU==screen->type)?(1):(0)) | v_MCU_HOLDMODE_FRAME_ST(0)
           );

	// set synchronous pin polarity and data pin swap rule
	switch (screen->face)
	{
        case OUT_P565:
            face = OUT_P565;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_P666:
            face = OUT_P666;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_D888_P565:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_D888_P666:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_P888:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(1));
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_CCIR656:
            face = OUT_CCIR656;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));        
        	break;
        default:
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            face = screen->face;
            break;
    }

     LcdMskReg(inf, DSP_CTRL0,
        m_DISPLAY_FORMAT | m_HSYNC_POLARITY | m_VSYNC_POLARITY | m_DEN_POLARITY |
        m_DCLK_POLARITY | m_COLOR_SPACE_CONVERSION,
        v_DISPLAY_FORMAT(face) | v_HSYNC_POLARITY(screen->pin_hsync) | v_VSYNC_POLARITY(screen->pin_vsync) |
        v_DEN_POLARITY(screen->pin_den) | v_DCLK_POLARITY(screen->pin_dclk) | v_COLOR_SPACE_CONVERSION(0)
        );
	if(face == OUT_CCIR656) {
		LcdMskReg(inf, DSP_CTRL0, m_COLOR_SPACE_CONVERSION, v_COLOR_SPACE_CONVERSION(3));
		// Backgroud color set to YUV Black.
		LcdMskReg(inf, DSP_CTRL1, m_BG_COLOR,  v_BG_COLOR(0x801080) );
	}
	else
     LcdMskReg(inf, DSP_CTRL1, m_BG_COLOR,  v_BG_COLOR(0x000000) );

     LcdMskReg(inf, SWAP_CTRL, m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_DELTA_SWAP | m_DUMMY_SWAP,
            v_OUTPUT_RB_SWAP(screen->swap_rb) | v_OUTPUT_RG_SWAP(screen->swap_rg) | v_DELTA_SWAP(screen->swap_delta) | v_DUMMY_SWAP(screen->swap_dumy));

	// set horizontal & vertical out timing
	if(SCREEN_MCU==inf->cur_screen->type)
    {
	    right_margin = x_res/6;
	}

    fbprintk("screen->hsync_len =%d,  screen->left_margin =%d, x_res =%d,  right_margin = %d \n",
        screen->hsync_len , screen->left_margin , x_res , right_margin );
    LcdMskReg(inf, DSP_HTOTAL_HS_END, m_BIT12LO | m_BIT12HI, v_BIT12LO(screen->hsync_len) |
             v_BIT12HI(screen->hsync_len + screen->left_margin + x_res + right_margin));
    LcdMskReg(inf, DSP_HACT_ST_END, m_BIT12LO | m_BIT12HI, v_BIT12LO(screen->hsync_len + screen->left_margin + x_res) |
             v_BIT12HI(screen->hsync_len + screen->left_margin));
	if (face == OUT_CCIR656 && screen->mode == FB_VMODE_INTERLACED) {
		int v_total, v_sync_end, v_active_start, v_active_end;
		int v_sync_start_f1, v_sync_end_f1, v_active_start_f1, v_active_end_f1;

		v_total = 2*(screen->vsync_len + screen->upper_margin + lower_margin)+  y_res + 1;
		v_sync_end = screen->vsync_len;
		v_active_start = v_sync_end + screen->upper_margin;
		v_active_end = v_active_start + y_res/2;
		LcdMskReg(inf, DSP_VTOTAL_VS_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(v_sync_end) | v_BIT11HI(v_total));
		LcdMskReg(inf, DSP_VACT_ST_END, m_BIT11LO | m_BIT11HI,	v_BIT11LO(v_active_end)| v_BIT11HI(v_active_start));
		v_sync_start_f1 = v_active_end + screen->lower_margin;
		v_sync_end_f1 = v_sync_start_f1 + screen->vsync_len;
		v_active_start_f1 = v_sync_end_f1 + screen->upper_margin + 1;
		v_active_end_f1 = v_active_start_f1 + y_res/2;
		LcdMskReg(inf, DSP_VS_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(v_sync_end_f1) | v_BIT11HI(v_sync_start_f1));
		LcdMskReg(inf, DSP_VACT_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(v_active_end_f1)| v_BIT11HI(v_active_start_f1));
		LcdMskReg(inf, SYS_CONFIG, m_W0_CBR_DEFLICK_EN | m_W0_YRGB_DEFLICK_EN | m_INTERIACE_EN | m_W0_INTERLACE_READ | m_W1_INTERLACE_READ | m_W2_INTERLACE_READ,
			v_W0_CBR_DEFLICK_EN(1) | v_W0_YRGB_DEFLICK_EN(1) | v_INTERIACE_EN(1) | v_W0_INTERLACE_READ(0) | v_W1_INTERLACE_READ(0) | v_W2_INTERLACE_READ(0) );
	}
	else {		
    LcdMskReg(inf, DSP_VTOTAL_VS_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(screen->vsync_len) |
              v_BIT11HI(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
    LcdMskReg(inf, DSP_VACT_ST_END, m_BIT11LO | m_BIT11HI,  v_BIT11LO(screen->vsync_len + screen->upper_margin+y_res)|
              v_BIT11HI(screen->vsync_len + screen->upper_margin));

    LcdMskReg(inf, DSP_VS_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));
    LcdMskReg(inf, DSP_VACT_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));
		LcdMskReg(inf, SYS_CONFIG, m_W0_CBR_DEFLICK_EN | m_W0_YRGB_DEFLICK_EN | m_INTERIACE_EN | m_W0_INTERLACE_READ | m_W1_INTERLACE_READ | m_W2_INTERLACE_READ,
			v_W0_CBR_DEFLICK_EN(0) | v_W0_YRGB_DEFLICK_EN(0) | v_INTERIACE_EN(0) | v_W0_INTERLACE_READ(0) | v_W1_INTERLACE_READ(0) | v_W2_INTERLACE_READ(0) );
	} 

	// let above to take effect
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    inf->dclk = clk_get(NULL, "dclk_lcdc");
    if (IS_ERR(inf->dclk))
    {
        printk(KERN_ERR "failed to get lcd dclock source\n");
        return ;
    }

    inf->aclk = clk_get(NULL, "aclk_lcdc");
    if (IS_ERR(inf->aclk))
    {
        printk(KERN_ERR "failed to get lcd clock clk_share_mem source \n");
        return;
    }
    inf->aclk_parent = clk_get(NULL, "ddr_pll");//general_pll //ddr_pll
    if (IS_ERR(inf->aclk_parent))
    {
        printk(KERN_ERR "failed to get lcd clock parent source\n");
        return ;
    }

    // set lcdc clk
    if(SCREEN_MCU==screen->type)    screen->pixclock = 150000000; //mcu fix to 150 MHz

    if(initscreen == 0)    //not init
    {
        clk_disable(inf->dclk);
      //  clk_disable(inf->aclk);
    }

   // clk_disable(inf->aclk_ddr_lcdc);
   // clk_disable(inf->aclk_disp_matrix);
   // clk_disable(inf->hclk_cpu_display);

   // clk_disable(inf->clk);
    

    fbprintk(">>>>>> set lcdc dclk need %d HZ, clk_parent = %d hz ret =%d\n ", screen->pixclock, screen->lcdc_aclk, ret);

    ret = clk_set_rate(inf->dclk, screen->pixclock);
    if(ret)
    {
        printk(KERN_ERR ">>>>>> set lcdc dclk failed\n");
    }
    inf->fb0->var.pixclock = inf->fb1->var.pixclock = div_u64(1000000000000llu, clk_get_rate(inf->dclk));
    if(initscreen)
    {
        ret = clk_set_parent(inf->aclk, inf->aclk_parent);
//        if(screen->lcdc_aclk){
//           aclk_rate = screen->lcdc_aclk;
//        }
		aclk_rate = clk_get_rate(inf->aclk_parent);
        ret = clk_set_rate(inf->aclk, aclk_rate);
        if(ret){
            printk(KERN_ERR ">>>>>> set lcdc aclk failed\n");
        }
        clk_enable(inf->aclk);
    }
  //  clk_enable(inf->aclk_ddr_lcdc);
  //  clk_enable(inf->aclk_disp_matrix);
  //  clk_enable(inf->hclk_cpu_display);       
  //  clk_enable(inf->clk);
    clk_enable(inf->dclk);

    // init screen panel
    if(screen->init)
    {
    	screen->init();
    }
}
#if 0 //def  CONFIG_CPU_FREQ
/*
* CPU clock speed change handler. We need to adjust the LCD timing
* parameters when the CPU clock is adjusted by the power management
* subsystem.
*/
#define TO_INF(ptr,member) container_of(ptr,struct rk29fb_inf,member)

static int
rk29fb_freq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
    struct rk29fb_inf *inf = TO_INF(nb, freq_transition);
    struct rk29fb_screen *screen = inf->cur_screen;
    u32 dclk_rate = 0;

    switch (val)
    {
    case CPUFREQ_PRECHANGE:
          break;
    case CPUFREQ_POSTCHANGE:
        {
         dclk_rate = screen->pixclock * 1000000;

         fbprintk(">>>>>> set lcdc dclk need %d HZ, clk_parent = %d hz \n ", screen->pixclock, dclk_rate);

         clk_set_rate(inf->dclk_divider, dclk_rate);
         break;
        }
    }
    return 0;
}
#endif

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;
//	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

int rk29_set_cursor_en(struct rk29fb_inf *inf, int enable)
{
    if (enable){
        LcdSetBit(inf, SYS_CONFIG, m_HWC_ENABLE);
    }else{
        LcdClrBit(inf, SYS_CONFIG, m_HWC_ENABLE);
	}
	LcdWrReg(inf, REG_CFG_DONE, 0x01);
	return 0;
}

int rk29_set_cursor_pos(struct rk29fb_inf *inf, int x, int y)
{
	struct rk29fb_screen *screen = inf->cur_screen;

	struct rk29fb_screen *init_screen = &(inf->panel1_info);

    /* set data */
    if (x >= 0x800 || y >= 0x800 )
        return -EINVAL;
	//printk("%s: %08x,%08x \n",__func__, x, y);

    x = (x*calculate_res_scaled(screen->x_res, inf->scaleinfo.xrate))/init_screen->x_res;
    y = y*calculate_res_scaled(screen->y_res, inf->scaleinfo.yrate)/init_screen->y_res;
    x += (screen->left_margin + screen->hsync_len) + calculate_res_scaled(screen->x_res, 100 - inf->scaleinfo.xrate)/2;
    y += (screen->upper_margin + screen->vsync_len) + calculate_res_scaled(screen->y_res, 100 - inf->scaleinfo.yrate)/2;
    LcdWrReg(inf, HWC_DSP_ST, v_BIT11LO(x)|v_BIT11HI(y));
	LcdWrReg(inf, REG_CFG_DONE, 0x01);
    return 0;
}


int rk29_set_cursor_colour_map(struct rk29fb_inf *inf, int bg_col, int fg_col)
{
	LcdMskReg(inf, HWC_COLOR_LUT0, m_HWC_R|m_HWC_G|m_HWC_B,bg_col);
	LcdMskReg(inf, HWC_COLOR_LUT2, m_HWC_R|m_HWC_G|m_HWC_B,fg_col);
	LcdWrReg(inf, REG_CFG_DONE, 0x01);
    return 0;
}

#define RK29_CURSOR_WRITE_BIT(addr,mask,value)		(*((char*)addr)) = (((*((char*)addr))&(~((char)mask)))|((char)value))
int rk29_set_cursor_img_transform(char *data)
{	int x, y;
	char *pmsk = data;
	char  *dst = (char*)rk29_cursor_buf;
	unsigned char  dmsk = 0;
	unsigned int   op,shift,offset;

	/* rk29 cursor is a 2 bpp 32x32 bitmap this routine
	 * clears it to transparent then combines the cursor
	 * shape plane with the colour plane to set the
	 * cursor */
	for (y = 0; y < 32; y++)
	{
		for (x = 0; x < 32; x++)
		{
			if ((x % 8) == 0) {
				dmsk = *pmsk++;
			} else {
				dmsk <<= 1;
			}
			op = (dmsk & 0x80) ? 2 : 3;
			shift = (x*2)%8;
			offset = x/4;
			RK29_CURSOR_WRITE_BIT((dst+offset),(3<<shift),(op<<shift));
		}
		dst += 8;
	}

	return 0;
}



int rk29_set_cursor_img(struct rk29fb_inf *inf, char *data)
{
    if(data)
    {
        rk29_set_cursor_img_transform(data);
    }
    else
    {
        rk29_set_cursor_img_transform(cursor_img);
    }
    LcdWrReg(inf, HWC_MST, __pa(rk29_cursor_buf));
    //LcdSetBit(inf, SYS_CONFIG,m_HWC_RELOAD_EN);
    LcdSetBit(inf, SYS_CONFIG, m_HWC_RELOAD_EN);
    flush_cache_all();
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
    return 0;
}


int rk29_set_cursor_test(struct fb_info *info)
{
	struct rk29fb_inf *inf = dev_get_drvdata(info->device);
	rk29_set_cursor_colour_map(inf, 0x000000ff, 0x00ff0000);
	rk29_set_cursor_pos(inf, 0, 0);
	rk29_set_cursor_img(inf, 0);
	rk29_set_cursor_en(inf, 1);
	return 0;
}
#if 0

int rk29_set_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

    //fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    /* check not being asked to exceed capabilities */

    if (cursor->image.width > 32)
        return -EINVAL;

    if (cursor->image.height > 32)
        return -EINVAL;

    if (cursor->image.depth > 1)
        return -EINVAL;

    if (cursor->enable)
        LcdSetBit(inf, SYS_CONFIG, m_HWC_ENABLE);
    else
        LcdClrBit(inf, SYS_CONFIG, m_HWC_ENABLE);

    /* set data */
    if (cursor->set & FB_CUR_SETPOS)
    {
        unsigned int x = cursor->image.dx;
        unsigned int y = cursor->image.dy;

        if (x >= 0x800 || y >= 0x800 )
            return -EINVAL;
        LcdWrReg(inf, HWC_DSP_ST, v_BIT11LO(x)|v_BIT11HI(y));
    }


    if (cursor->set & FB_CUR_SETCMAP)
    {
        unsigned int bg_col = cursor->image.bg_color;
        unsigned int fg_col = cursor->image.fg_color;

        fbprintk("%s: update cmap (%08x,%08x)\n",
            __func__, bg_col, fg_col);

        LcdMskReg(inf, HWC_COLOR_LUT0, m_HWC_R|m_HWC_G|m_HWC_B,
                  v_HWC_R(info->cmap.red[bg_col]>>8) | v_HWC_G(info->cmap.green[bg_col]>>8) | v_HWC_B(info->cmap.blue[bg_col]>>8));

        LcdMskReg(inf, HWC_COLOR_LUT2, m_HWC_R|m_HWC_G|m_HWC_B,
                         v_HWC_R(info->cmap.red[fg_col]>>8) | v_HWC_G(info->cmap.green[fg_col]>>8) | v_HWC_B(info->cmap.blue[fg_col]>>8));
    }

    if ((cursor->set & FB_CUR_SETSIZE ||
        cursor->set & (FB_CUR_SETIMAGE | FB_CUR_SETSHAPE))
        && info->screen_base && info->fix.smem_start && info->fix.smem_len)
    {
        /* rk29 cursor is a 2 bpp 32x32 bitmap this routine
         * clears it to transparent then combines the cursor
         * shape plane with the colour plane to set the
         * cursor */
        int x, y;
        const unsigned char *pcol = cursor->image.data;
        const unsigned char *pmsk = cursor->mask;
        void __iomem   *dst;
        unsigned long cursor_mem_start;
        unsigned char  dcol = 0;
        unsigned char  dmsk = 0;
        unsigned int   op;

        dst = info->screen_base + info->fix.smem_len - CURSOR_BUF_SIZE;
	    cursor_mem_start = info->fix.smem_start + info->fix.smem_len - CURSOR_BUF_SIZE;

        fbprintk("%s: setting shape (%d,%d)\n",
            __func__, cursor->image.width, cursor->image.height);

        memset(dst, 0, CURSOR_BUF_SIZE);

        for (y = 0; y < cursor->image.height; y++)
        {
            for (x = 0; x < cursor->image.width; x++)
            {
                if ((x % 8) == 0) {
                    dcol = *pcol++;
                    dmsk = *pmsk++;
                } else {
                    dcol >>= 1;
                    dmsk >>= 1;
                }

                if (dmsk & 1) {
                    op = (dcol & 1) ? 1 : 3;
                    op <<= ((x % 4) * 2);
                    *(u8*)(dst+(x/4)) |= op;
                }
            }
            dst += (32*2)/8;
        }
        LcdSetBit(inf, SYS_CONFIG,m_HWC_RELOAD_EN);
        LcdWrReg(inf, HWC_MST, cursor_mem_start);
        // flush end when wq_condition=1 in mcu panel, but not in rgb panel
        if(SCREEN_MCU == inf->cur_screen->type) {
            wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
            wq_condition = 0;
        } else {
            wq_condition = 0;
            wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
        }
        LcdClrBit(inf, SYS_CONFIG, m_HWC_RELOAD_EN);
    }

    return 0;
}
#endif
static int hdmi_get_fbscale(void)
{
#ifdef CONFIG_HDMI
	return hdmi_get_scale();
#else
	return 100;
#endif
}
static void hdmi_set_fbscale(struct fb_info *info)
{
#if defined(CONFIG_HDMI)
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;
	int scale;
	
	scale = hdmi_get_scale();
	if(scale == 100)
		return;
	par->xpos += screen->x_res * (100-scale) / 200;
	par->ypos += screen->y_res * (100-scale) / 200;
	par->xsize = par->xsize *scale /100;
	par->ysize = par->ysize *scale /100;
#endif
}
static int win0_blank(int blank_mode, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(1));
	    LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;
    case FB_BLANK_NORMAL:
         LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
	     break;
    default:
        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
	    LcdWrReg(inf, REG_CFG_DONE, 0x01);
#ifdef CONFIG_DDR_RECONFIG
	msleep(40);
#endif
        break;
    }

	mcu_refresh(inf);
    return 0;
}

static int win0_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct win0_par *par = info->par;
	u32 xact, yact, xvir, yvir, xpos, ypos, ScaleYrgbX,ScaleYrgbY, ScaleCbrX, ScaleCbrY, y_addr,uv_addr;
	hdmi_set_fbscale(info);
	xact = var->xres;			    /* visible resolution		*/
	yact = var->yres;
	xvir = var->xres_virtual;		/* virtual resolution		*/
	yvir = var->yres_virtual;
	//u32 xact_st = var->xoffset;         /* offset from virtual to visible */
	//u32 yact_st = var->yoffset;         /* resolution			*/
    xpos = par->xpos;
    ypos = par->ypos;

    ScaleYrgbX=0x1000;
	ScaleYrgbY=0x1000;
    ScaleCbrX=0x1000;
	ScaleCbrY=0x1000;

    y_addr = 0;       //user alloc buf addr y
    uv_addr = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if((var->rotate%360!=0)&& (inf->video_mode))
    {
      #ifdef CONFIG_FB_ROTATE_VIDEO  
        if(xact > screen->x_res)
        {
            xact = screen->x_res;       /* visible resolution       */
            yact = screen->y_res;
            xvir = screen->x_res;       /* virtual resolution       */
            yvir = screen->y_res;
        }   else   {            
            xact = var->xres;               /* visible resolution       */
            yact = var->yres;
            xvir = var->xres_virtual;       /* virtual resolution       */
            yvir = var->yres_virtual;
			printk("xact=%d yact =%d \n",xact,yact);
        }
      #else //CONFIG_FB_ROTATE_VIDEO
        printk("LCDC not support rotate!\n");
        return -EINVAL;
      #endif
    }
    
	// calculate the display phy address
    y_addr = fix->smem_start + par->y_offset;
    uv_addr = fix->mmio_start + par->c_offset;

    ScaleYrgbX = CalScaleW0(xact, par->xsize);
	if(screen->face == OUT_CCIR656 && screen->mode == FB_VMODE_INTERLACED) {
		par->ysize /= 2;    
	}
    ScaleYrgbY = CalScaleW0(yact, par->ysize);

    switch (par->format)
    {
       case 2:// yuv422
           ScaleCbrX= CalScaleW0((xact/2), par->xsize);
           ScaleCbrY =  CalScaleW0(yact, par->ysize);
           break;
       case 3: // yuv4200
       case 4: // yuv4201
           ScaleCbrX= CalScaleW0(xact/2, par->xsize);
           ScaleCbrY =  CalScaleW0(yact/2, par->ysize);
           break;
       case 5:// yuv444
           ScaleCbrX= CalScaleW0(xact, par->xsize);
           ScaleCbrY =  CalScaleW0(yact, par->ysize);
           break;
       default:
           break;
    }

    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);

    LcdMskReg(inf, SYS_CONFIG,  m_W0_FORMAT , v_W0_FORMAT(par->format));//(inf->video_mode==0)

    LcdMskReg(inf, WIN0_VIR, m_WORDLO | m_WORDHI, v_VIRWIDTH(xvir) | v_VIRHEIGHT((yvir)) );
    LcdMskReg(inf, WIN0_ACT_INFO, m_WORDLO | m_WORDHI, v_WORDLO(xact) | v_WORDHI(yact));
    LcdMskReg(inf, WIN0_DSP_ST, m_BIT11LO | m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN0_DSP_INFO, m_BIT12LO | m_BIT12HI,  v_BIT12LO(par->xsize) | v_BIT12HI(par->ysize));
    LcdMskReg(inf, WIN0_SCL_FACTOR_YRGB, m_WORDLO | m_WORDHI, v_WORDLO(ScaleYrgbX) | v_WORDHI(ScaleYrgbY));
    LcdMskReg(inf, WIN0_SCL_FACTOR_CBR, m_WORDLO | m_WORDHI, v_WORDLO(ScaleCbrX) | v_WORDHI(ScaleCbrY));

    switch(par->format)
    {
    case 0:  //rgb888
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(1) | v_W0_YRGB_16_SWAP(1) | v_W0_YRGB_R_SHIFT_SWAP(1) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0));
		break;
    case 1:  //rgb565
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0));
        break;
    case 4:   //yuv4201
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) |
            v_W0_YRGB_M8_SWAP(1) | v_W0_CBR_8_SWAP(0));
        break;
    default:
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0) );
		break;
    }

    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;

}

static int win0_pan( struct fb_info *info )
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
   // struct fb_var_screeninfo *var0 = &info->var;
    struct fb_fix_screeninfo *fix0 = &info->fix;
    struct win0_par *par = info->par;
    u32 y_addr=0, uv_addr=0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    y_addr = fix0->smem_start +  par->y_offset;//y_offset;
    uv_addr = fix0->mmio_start + par->c_offset ;//c_offset;

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

     // enable win0 after the win0 addr is seted
 	LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE((1==par->addr_seted)?(1):(0)));
	mcu_refresh(inf);

    return 0;
}

static int win1_blank(int blank_mode, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

	switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(1));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;
    case FB_BLANK_NORMAL:
         LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
	     break;
    default:
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;
    }
    

	mcu_refresh(inf);
    return 0;
}


#ifdef CONFIG_CLOSE_WIN1_DYNAMIC 
static void win1_check_work_func(struct work_struct *work)
{
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
    struct fb_info *fb0_inf = inf->fb0;
     struct fb_var_screeninfo *var = &fb0_inf->var;
    int i=0;
    int *p = NULL;
    int blank_data,total_data;
    int format = 0;
    u16 xres_virtual = fb0_inf->var.xres_virtual;      //virtual screen size
    u16 xpos_virtual = fb0_inf->var.xoffset;           //visiable offset in virtual screen
    u16 ypos_virtual = fb0_inf->var.yoffset;

    int offset = 0;//(ypos_virtual*xres_virtual + xpos_virtual)*((inf->fb0_color_deepth || fb0_inf->var.bits_per_pixel==32)? 4:2)/4;  
    switch(var->bits_per_pixel)
    {
        case 16: 
            format = 1;
            offset = (ypos_virtual*xres_virtual + xpos_virtual)*(inf->fb0_color_deepth ? 4:2);
            if(ypos_virtual == 3*var->yres && inf->fb0_color_deepth)
                offset -= var->yres * var->xres *2;
            break;
        default:
            format = 0;
            offset = (ypos_virtual*xres_virtual + xpos_virtual)*4;            
            if(ypos_virtual >= 2*var->yres)
            {
                format = 1;
                if(ypos_virtual == 3*var->yres)
                {            
                    offset -= var->yres * var->xres *2;
                }
            }
            break;
    }
    p = (u32)fb0_inf->screen_base + offset; 
    blank_data = (inf->fb0_color_deepth==32) ? 0xff000000 : 0;
    total_data = fb0_inf->var.xres * fb0_inf->var.yres / (format+1);
    
   // printk("var->bits_per_pixel=%d,ypos_virtual=%d, var->yres=%d,offset=%d,total_data=%d\n",var->bits_per_pixel,ypos_virtual,var->yres,offset,total_data);
    
    for(i=0; i < total_data; i++)
    {
        if(*p++ != blank_data) 
        {
            //printk("win1 have no 0 data in %d, total %d\n",i,total_data);
            return;
        }            
    }

    win1_blank(FB_BLANK_POWERDOWN, fb0_inf);
   // printk("%s close win1!\n",__func__);
}
static DECLARE_DELAYED_WORK(rk29_win1_check_work, win1_check_work_func);
#endif
static int win1_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;
    struct fb_var_screeninfo *var = &info->var;
	u32 addr;
	u16 xres_virtual,xpos,ypos;
	u8 trspval,trspmode;
 #ifdef CONFIG_CLOSE_WIN1_DYNAMIC   
    cancel_delayed_work_sync(&rk29_win1_check_work);
 #endif   
    if(((screen->x_res != var->xres) || (screen->y_res != var->yres))
	    #ifndef	CONFIG_FB_SCALING_OSD_1080P
            && !((screen->x_res>1280) && (var->bits_per_pixel == 32))
		#endif
		)
    {
        hdmi_set_fbscale(info);
    }
		#ifndef	CONFIG_FB_SCALING_OSD_1080P
		else  if(((screen->x_res==1920) ))
    {
    	if(hdmi_get_fbscale() < 100)
			par->ypos -=screen->y_res * (100-hdmi_get_fbscale()) / 200;
	}
		#endif
    //u32 offset=0, addr=0, map_size=0, smem_len=0;
    addr=0;
    xres_virtual = 0;      //virtual screen size

    //u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    //u16 ypos_virtual = var->yoffset;

    xpos = par->xpos;                 //visiable offset in panel
    ypos = par->ypos;

    trspmode = TRSP_CLOSE;
    trspval = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

   #ifdef CONFIG_FB_SCALING_OSD
    if(((screen->x_res != var->xres) || (screen->y_res != var->yres)) 
        #ifndef	CONFIG_FB_SCALING_OSD_1080P
            && (screen->x_res<=1280)
		#endif
		)
    {
        addr = fix->mmio_start + par->y_offset* hdmi_get_fbscale()/100;
        xres_virtual = screen->x_res* hdmi_get_fbscale()/100;      //virtual screen size
    }
    else
   #endif
    {
        addr = fix->smem_start + par->y_offset;
        xres_virtual = var->xres_virtual;      //virtual screen size
    }
    LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE|m_W1_FORMAT, v_W1_ENABLE(fb1_open_init?0:1)|v_W1_FORMAT(par->format));

    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);

    LcdWrReg(inf, WIN1_YRGB_MST, addr);

    LcdMskReg(inf, WIN1_DSP_ST, m_BIT11LO|m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN1_DSP_INFO, m_BIT12LO|m_BIT12HI, v_BIT12LO(par->xsize) | v_BIT12HI(par->ysize));

    LcdMskReg(inf, WIN1_VIR, m_WORDLO , v_WORDLO(xres_virtual));

    LcdMskReg(inf, BLEND_CTRL, m_W1_BLEND_EN |  m_W1_BLEND_FACTOR,
        v_W1_BLEND_EN((TRSP_FMREG==trspmode) || (TRSP_MASK==trspmode)) | v_W1_BLEND_FACTOR(trspval));

     // enable win1 color key and set the color to black(rgb=0)
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR, v_COLORKEY_EN(1) | v_KEYCOLOR(0));

    if(1==par->format) //rgb565
    {
        LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
            v_W1_8_SWAP(0) | v_W1_16_SWAP(0) | v_W1_R_SHIFT_SWAP(0) | v_W1_565_RB_SWAP(0) );
    }
    else
    {
         LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
                v_W1_8_SWAP(1) | v_W1_16_SWAP(1) | v_W1_R_SHIFT_SWAP(1) | v_W1_565_RB_SWAP(0) );

         LcdMskReg(inf, DSP_CTRL0, m_W1_TRANSP_FROM, v_W1_TRANSP_FROM(TRSP_FMRAM==trspmode) );
    }

	LcdWrReg(inf, REG_CFG_DONE, 0x01);
    
#ifdef CONFIG_CLOSE_WIN1_DYNAMIC 
    schedule_delayed_work(&rk29_win1_check_work, msecs_to_jiffies(5000));
#endif

    return 0;
}

static int win1_pan( struct fb_info *info )
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_fix_screeninfo *fix1 = &info->fix;
    struct win0_par *par = info->par;
    u32 addr = 0;

    #ifdef CONFIG_FB_SCALING_OSD
    struct rk29fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    if(((screen->x_res != var->xres) || (screen->y_res != var->yres)) 
        #ifndef	CONFIG_FB_SCALING_OSD_1080P
            && (screen->x_res<=1280)
		#endif
		)
    {
        addr = fix1->mmio_start + par->y_offset* hdmi_get_fbscale()/100;
    }
    else
    #endif
    {
        addr = fix1->smem_start + par->y_offset;
    }

    //fbprintk("info->screen_base = %8x ; fix1->smem_len = %d , addr = %8x\n",(u32)info->screen_base, fix1->smem_len, addr);

    LcdWrReg(inf, WIN1_YRGB_MST, addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
	mcu_refresh(inf);

    return 0;
}

static int fb0_blank(int blank_mode, struct fb_info *info)
{
	struct rk29fb_inf *inf = dev_get_drvdata(info->device);

    if(inf->video_mode == 1)
    {
        win1_blank(blank_mode, info);
    }
    else
    {
        win0_blank(blank_mode, info);
    }
    return 0;
}

static int fb0_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    u16 xpos = (var->nonstd>>8) & 0xfff;
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xlcd = screen->x_res;
    u16 ylcd = screen->y_res;

    //fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if( 0==var->xres_virtual || 0==var->yres_virtual ||
        0==var->xres || 0==var->yres || var->xres<16 ||
        ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
    {
        printk(">>>>>> fb0_check_var fail 1!!! \n");
        printk(">>>>>> 0==%d || 0==%d ", var->xres_virtual,var->yres_virtual);
        printk("0==%d || 0==%d || %d<16 || ", var->xres,var->yres,var->xres<16);
        printk("bits_per_pixel=%d \n", var->bits_per_pixel);
        return -EINVAL;
    }

    if( (var->xoffset+var->xres)>var->xres_virtual ||
        (var->yoffset+var->yres)>var->yres_virtual*2 )
    {
        printk(">>>>>> fb0_check_var fail 2!!! \n");
        printk(">>>>>> (%d+%d)>%d || ", var->xoffset,var->xres,var->xres_virtual);
        printk("(%d+%d)>%d || ", var->yoffset,var->yres,var->yres_virtual);
        printk("(%d+%d)>%d || (%d+%d)>%d \n", xpos,var->xres,xlcd,ypos,var->yres,ylcd);
        return -EINVAL;
    }

    if(inf->fb0_color_deepth)var->bits_per_pixel=inf->fb0_color_deepth;
    switch(var->bits_per_pixel)
    {
    case 16:    // rgb565
        var->xres_virtual = (var->xres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x1);
        break;
    default:    // rgb888
        var->bits_per_pixel = 32;
        break;
    }

    return 0;
}


static int fb0_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;

    u32 offset=0,  smem_len=0;
    u16 xres_virtual = var->xres_virtual;      //virtual screen size
    u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    u16 ypos_virtual = var->yoffset;
	int x_res_scaled, y_res_scaled;
#ifdef CONFIG_FB_SCALING_OSD
    struct rk29_ipp_req ipp_req;
    u32 dstoffset=0;
	memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
#endif

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
	wait_event_interruptible(fb0_wait_queue, idle_condition);
	idle_condition = 0;

    inf->setFlag = 0;
	CHK_SUSPEND(inf);

    if(inf->fb0_color_deepth)var->bits_per_pixel=inf->fb0_color_deepth;
    #if !defined(CONFIG_FB_SCALING_OSD)
    if((inf->video_mode == 1)&&(screen->y_res < var->yres))ypos_virtual += (var->yres-screen->y_res);
    #endif

    switch(var->bits_per_pixel)
    {
    case 16:    // rgb565
        par->format = 1;
         if( ypos_virtual == 0)
            fb0_first_buff_bits = 16;
        else
            fb0_second_buff_bits = 16;

        //fix->line_length = 2 * xres_virtual;
        fix->line_length = (inf->fb0_color_deepth ? 4:2) * xres_virtual;   //32bit and 16bit change

        #ifdef CONFIG_FB_SCALING_OSD
        dstoffset = ((ypos_virtual*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres )*2;
        ipp_req.src0.fmt = IPP_RGB_565;
        ipp_req.dst0.fmt = IPP_RGB_565;
        #endif
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*(inf->fb0_color_deepth ? 4:2);
        if(ypos_virtual == 3*var->yres && inf->fb0_color_deepth)
            offset -= var->yres * var->xres *2;
        break;
    case 32:    // rgb888
    default:
        par->format = 0;
         if( ypos_virtual == 0)
            fb0_first_buff_bits = 32;
        else
            fb0_second_buff_bits = 32;
        fix->line_length = 4 * xres_virtual;
        #ifdef CONFIG_FB_SCALING_OSD
        dstoffset = ((ypos_virtual*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres )*4;       

        ipp_req.src0.fmt = IPP_XRGB_8888;
        ipp_req.dst0.fmt = IPP_XRGB_8888;
        #endif
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*4;
        
        if(ypos_virtual >= 2*var->yres)
        {
            par->format = 1;
            #ifdef CONFIG_FB_SCALING_OSD
            dstoffset = (((ypos_virtual-2*var->yres)*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres )*4;
            ipp_req.src0.fmt = IPP_RGB_565;
            ipp_req.dst0.fmt = IPP_RGB_565;
            #endif
            if(ypos_virtual == 3*var->yres)
            {            
                offset -= var->yres * var->xres *2;
            }
        }
        break;
    }

    smem_len = fix->line_length * var->yres_virtual;
    //map_size = PAGE_ALIGN(smem_len);

    if (smem_len > fix->smem_len)     // buffer need realloc
    {
        printk("%s sorry!!! win1 buf is not enough\n",__FUNCTION__);
        printk("line_length = %d, yres_virtual = %d, win1_buf only = %dB\n",fix->line_length,var->yres_virtual,fix->smem_len);
        printk("you can change buf size MEM_FB_SIZE in board-xxx.c \n");
    }


    par->addr_seted = 1;
#if ANDROID_USE_THREE_BUFS
    if(0==new_frame_seted) {
        wq_condition = 0;
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    }
    new_frame_seted = 0;
#endif

    if(inf->video_mode == 1)
    {
        #ifdef CONFIG_FB_SCALING_OSD
        if(((screen->x_res != var->xres) || (screen->y_res != var->yres)) 
		#ifndef	CONFIG_FB_SCALING_OSD_1080P
            && (screen->x_res<=1280)
		#endif
		)
        {
            par->xpos = 0;
            par->ypos = 0;
            par->xsize = screen->x_res;
            par->ysize = screen->y_res;
            par->y_offset = dstoffset;

            ipp_req.src0.YrgbMst = fix->smem_start + offset;
            ipp_req.src0.w = var->xres;
            ipp_req.src0.h = var->yres;

            ipp_req.dst0.YrgbMst = fix->mmio_start + dstoffset* hdmi_get_fbscale()/100;
            ipp_req.dst0.w = screen->x_res* hdmi_get_fbscale()/100;
            ipp_req.dst0.h = screen->y_res* hdmi_get_fbscale()/100;

            ipp_req.src_vir_w = ipp_req.src0.w;
            ipp_req.dst_vir_w = ipp_req.dst0.w;
            ipp_req.timeout = 100;
            ipp_req.flag = IPP_ROT_0;
            //ipp_do_blit(&ipp_req);
            ipp_blit_sync(&ipp_req);
        }else
        #endif
        {
            par->y_offset = offset;
            par->xpos = (screen->x_res >= var->xres)?((screen->x_res - var->xres)/2):0;              //visiable offset in panel
            par->ypos = (screen->y_res >= var->yres)?(screen->y_res - var->yres):0;
            par->xsize = var->xres;                                //visiable size in panel
            par->ysize = (screen->y_res >= var->yres) ? var->yres : screen->y_res;
        }
        win1_set_par(info);
    }
    else
    {
        x_res_scaled = calculate_res_scaled(screen->x_res, inf->scaleinfo.xrate);
        y_res_scaled = calculate_res_scaled(screen->y_res, inf->scaleinfo.yrate);
        par->y_offset = offset;
        par->xpos = (screen->x_res - x_res_scaled)/2;
        par->ypos = (screen->y_res - y_res_scaled)/2;
        par->xsize = x_res_scaled;
        par->ysize = y_res_scaled;
        win0_set_par(info);
     #ifdef	CONFIG_FB_MIRRORING
	 if(video_data_to_mirroring!=NULL)
		video_data_to_mirroring(info,NULL);
     #endif
	 
    }
    inf->setFlag = 1;
	idle_condition = 1;
	wake_up_interruptible_sync(&fb0_wait_queue);
    return 0;
}

static int fb0_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{

    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var1 = &info->var;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;

    u32 offset = 0;
    u16 ypos_virtual = var->yoffset;
    u16 xpos_virtual = var->xoffset;
   #ifdef CONFIG_FB_SCALING_OSD
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk29_ipp_req ipp_req;
    u32 dstoffset = 0;
	memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
   #endif
	//fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if(inf->fb0_color_deepth)var->bits_per_pixel=inf->fb0_color_deepth;
    #if !defined(CONFIG_FB_SCALING_OSD)
        if((inf->video_mode == 1)&&(screen->y_res < var->yres))ypos_virtual += (var->yres-screen->y_res);
    #endif

    switch(var1->bits_per_pixel)
    {
    case 16:    // rgb565
        var->xoffset = (var->xoffset) & (~0x1);
        #ifdef CONFIG_FB_SCALING_OSD
        dstoffset = ((ypos_virtual*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres) * 2;
        ipp_req.src0.fmt = IPP_RGB_565;
        ipp_req.dst0.fmt = IPP_RGB_565;
        #endif
        offset = (ypos_virtual*var1->xres_virtual + xpos_virtual)*(inf->fb0_color_deepth ? 4:2);
        if(ypos_virtual == 3*var->yres && inf->fb0_color_deepth)
            offset -= var->yres * var->xres *2;
        break;
    case 32:    // rgb888
        #ifdef CONFIG_FB_SCALING_OSD
        dstoffset = ((ypos_virtual*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres )*4;
        ipp_req.src0.fmt = IPP_XRGB_8888;
        ipp_req.dst0.fmt = IPP_XRGB_8888;
        #endif
        offset = (ypos_virtual*var1->xres_virtual + xpos_virtual)*4;
        if(ypos_virtual >= 2*var->yres)
        {
            par->format = 1;
            #ifdef CONFIG_FB_SCALING_OSD
           	dstoffset = (((ypos_virtual-2*var->yres)*screen->y_res/var->yres) *screen->x_res + (xpos_virtual*screen->x_res)/var->xres )*4;
            ipp_req.src0.fmt = IPP_RGB_565;
            ipp_req.dst0.fmt = IPP_RGB_565;
            #endif
            if(ypos_virtual == 3*var->yres)
            {            
                offset -= var->yres * var->xres *2;
            }
        }
        break;
    default:
        return -EINVAL;
    }

    if(inf->video_mode == 1)
    {
        #ifdef CONFIG_FB_SCALING_OSD
        if(((screen->x_res != var->xres) || (screen->y_res != var->yres)) 
        #ifndef	CONFIG_FB_SCALING_OSD_1080P
            && (screen->x_res<=1280)
		#endif
		)
        {
            par->y_offset = dstoffset;

            ipp_req.src0.YrgbMst = fix->smem_start + offset;
            ipp_req.src0.w = var->xres;
            ipp_req.src0.h = var->yres;

            ipp_req.dst0.YrgbMst = fix->mmio_start + dstoffset* hdmi_get_fbscale()/100;
            ipp_req.dst0.w = screen->x_res* hdmi_get_fbscale()/100;
            ipp_req.dst0.h = screen->y_res* hdmi_get_fbscale()/100;

            ipp_req.src_vir_w = ipp_req.src0.w;
            ipp_req.dst_vir_w = ipp_req.dst0.w;
            ipp_req.timeout = 100;
            ipp_req.flag = IPP_ROT_0;
            //ipp_do_blit(&ipp_req);
            ipp_blit_sync(&ipp_req);
            win1_pan(info);
            return 0;
        }else
        #endif
            par->y_offset = offset;
        win1_pan(info);
    }
    else
    {
        par->y_offset = offset;
        win0_pan(info);
    }
        // flush end when wq_condition=1 in mcu panel, but not in rgb panel
#if !ANDROID_USE_THREE_BUFS
    // flush end when wq_condition=1 in mcu panel, but not in rgb panel
    if(SCREEN_MCU == inf->cur_screen->type) {
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
        wq_condition = 0;
    } else {
        wq_condition = 0;
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    }
#endif
    return 0;
}

static int fb0_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(cmd)
    {
    case FB0_IOCTL_STOP_TIMER_FLUSH:    //stop timer flush mcu panel after android is runing
        if(1==arg)
        {
            inf->mcu_usetimer = 0;
        }
        break;
   case FBIOPUT_16OR32:

        inf->fb0_color_deepth = arg;

	    break;
	case FBIOGET_16OR32:
	    return  inf->fb0_color_deepth;
	case FBIOGET_IDLEFBUff_16OR32:
        if(info->var.yoffset == 0)
        {
            return fb0_second_buff_bits;
        }
        else
        {
            return fb0_first_buff_bits;
        }
    case FBIOSET_COMPOSE_LAYER_COUNTS:
        fb_compose_layer_count = arg;
        break;

    case FBIOGET_COMPOSE_LAYER_COUNTS:
        
        return fb_compose_layer_count;
        
	case FBIOPUT_FBPHYADD:
        return info->fix.smem_start;
    case FBIOGET_OVERLAY_STATE:
        return inf->video_mode;
    case FBIOGET_SCREEN_STATE:
        return inf->cur_screen->type;
        
	case FBIOPUT_SET_CURSOR_EN:
		{
			int en;
			if(copy_from_user(&en, (void*)arg, sizeof(int)))
			    return -EFAULT;
			rk29_set_cursor_en(inf, en);
		}
		break;
	case FBIOPUT_SET_CURSOR_POS:
		{
			struct fbcurpos pos;
			if(copy_from_user(&pos, (void*)arg, sizeof(struct fbcurpos)))
			    return -EFAULT;
			rk29_set_cursor_pos(inf, pos.x , pos.y);
		}
		break;
	case FBIOPUT_SET_CURSOR_IMG:
		{
			char cursor_buf[CURSOR_BUF_SIZE];
			if(copy_from_user(cursor_buf, (void*)arg, CURSOR_BUF_SIZE))
				return -EFAULT;
			rk29_set_cursor_img(inf, cursor_buf);
		}
		break;
	case FBIOPUT_SET_CURSOR_CMAP:
		{
			struct fb_image img;
			if(copy_from_user(&img, (void*)arg, sizeof(struct fb_image)))
			    return -EFAULT;
			rk29_set_cursor_colour_map(inf, img.bg_color, img.fg_color);
		}
		break;
	case FBIOPUT_GET_CURSOR_RESOLUTION:
		{
            u32 panel_size[2];
			//struct rk29fb_inf *inf = dev_get_drvdata(info->device);
             if((inf->fb1->var.rotate &0x1ff ) == 270) {
                panel_size[0] = inf->cur_screen->y_res; //inf->cur_screen->y_res; change for hdmi video size
                panel_size[1] = inf->cur_screen->x_res;
            } else {
                panel_size[0] = inf->cur_screen->x_res;
                panel_size[1] = inf->cur_screen->y_res;
            }
            if(copy_to_user((void*)arg, panel_size, 8))  return -EFAULT;
            break;
		}
	case FBIOPUT_GET_CURSOR_EN:
		{
            u32 en = (inf->regbak.SYS_CONFIG & m_HWC_ENABLE);
            if(copy_to_user((void*)arg, &en, 4))  return -EFAULT;
            break;
		}

#if  defined(CONFIG_MACH_RK29_ITV_HOTDOG) || defined(CONFIG_MACH_RK29_ITV)
	case FBIOPUT_SET_SCALE_RATE:
		{
			int rate = 0;
			if(copy_from_user(&rate, (void*)arg, sizeof(int)))
				return -EFAULT;
			inf->scaleinfo.xrate = rate;
			inf->scaleinfo.yrate = rate;
			fb0_set_par(inf->fb0);
		}
		break;
#endif
	default:
        break;
    }
    return 0;
}

static int fb1_blank(int blank_mode, struct fb_info *info)
{
    win0_blank(blank_mode, info);
    return 0;
}

static int fb1_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;

    u32 ScaleYRGBY=0x1000;
    u16 xpos = (var->nonstd>>8) & 0xfff;   //offset in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;   //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;
    u16 xlcd = screen->x_res;        //size of panel
    u16 ylcd = screen->y_res;
    u16 yres = 0;
#if 0
	struct hdmi *hdmi = get_hdmi_struct(0);
#endif

    if((var->rotate & 0x1ff) == 90 ||(var->rotate &0x1ff)== 270) {
      #ifdef CONFIG_FB_ROTATE_VIDEO
        xlcd = screen->y_res;
        ylcd = screen->x_res;
      #else //CONFIG_FB_ROTATE_VIDEO
        printk("LCDC not support rotate!\n");
        return -EINVAL;
      #endif
    }

    xpos = (xpos * screen->x_res) / inf->panel1_info.x_res;
    ypos = (ypos * screen->y_res) / inf->panel1_info.y_res;
    xsize = (xsize * screen->x_res) / inf->panel1_info.x_res;
    ysize = (ysize * screen->y_res) / inf->panel1_info.y_res;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if( 0==var->xres_virtual || 0==var->yres_virtual ||
        0==var->xres || 0==var->yres || var->xres<16 ||
        0==xsize || 0==ysize || xsize<16 ||
        ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
    {
        printk(">>>>>> win0fb_check_var fail 1!!! \n");
		printk("0==%d || 0==%d || 0==%d || 0==%d || %d<16 \n ||0==%d || 0==%d || %d<16 ||((16!=%d)&&(32!=%d)) \n",
				var->xres_virtual, var->yres_virtual, var->xres, var->yres, var->xres, xsize, ysize, xsize,
        		var->bits_per_pixel, var->bits_per_pixel);
        return -EINVAL;
    }

    if( (var->xoffset+var->xres)>var->xres_virtual ||
        (var->yoffset+var->yres)>var->yres_virtual ||
        (xpos+xsize)>xlcd || (ypos+ysize)>ylcd  )
    {
        printk(">>>>>> win0fb_check_var fail 2!!! \n");
		printk("(%d+%d)>%d || (%d+%d)>%d || (%d+%d)>%d || (%d+%d)>%d \n ",
				var->xoffset, var->xres, var->xres_virtual, var->yoffset, var->yres,
				var->yres_virtual, xpos, xsize, xlcd, ypos, ysize, ylcd);       
        return -EINVAL;
    }

    switch(var->nonstd&0x0f)
    {
    case 0: // rgb
        switch(var->bits_per_pixel)
        {
        case 16:    // rgb565
            var->xres_virtual = (var->xres_virtual + 0x1) & (~0x1);
            var->xres = (var->xres + 0x1) & (~0x1);
            var->xoffset = (var->xoffset) & (~0x1);
            break;
        default:    // rgb888
            var->bits_per_pixel = 32;
            break;
        }
        var->nonstd &= ~0xc0;  //not support I2P in this format
        break;
    case 1: // yuv422
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->xoffset = (var->xoffset) & (~0x3);
        break;
    case 2: // yuv4200
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->yres_virtual = (var->yres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->yres = (var->yres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        break;
    case 3: // yuv4201
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->yres_virtual = (var->yres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->yres = (var->yres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    case 4: // none
    case 5: // yuv444
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->xoffset = (var->xoffset) & (~0x3);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    default:
        printk(">>>>>> fb1 var->nonstd=%d is invalid! \n", var->nonstd);
        return -EINVAL;
    }

    if((var->rotate & 0x1ff ) == 90 ||(var->rotate & 0x1ff ) == 270)
     {
         yres = var->xres;
     }
     else
     {
         yres = var->yres;
     }

    ScaleYRGBY = CalScaleW0(yres, ysize);

    if((ScaleYRGBY>0x8000) || (ScaleYRGBY<0x200))
    {
        return -EINVAL;        // multiple of scale down or scale up can't exceed 8
    }
#if 0
	if(inf->video_mode == 1) {
		if(hdmi_resolution_changed(hdmi,var->xres,var->yres, 1) == 1)
		{
			LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(1));
    		LcdWrReg(inf, REG_CFG_DONE, 0x01);
			init_completion(&hdmi->complete);
			hdmi->wait = 1;
			wait_for_completion_interruptible_timeout(&hdmi->complete,
								msecs_to_jiffies(10000));
		}
	}
#endif
    return 0;
}

static int fb1_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct win0_par *par = info->par;

    u8 format = 0;
    u32 cblen=0, crlen=0, map_size=0, smem_len=0;

	//u32 xact = var->xres;			    /* visible resolution		*/
	//u32 yact = var->yres;
	u32 xvir = var->xres_virtual;		/* virtual resolution		*/
	u32 yvir = var->yres_virtual;
	u32 xact_st = var->xoffset;			/* offset from virtual to visible */
	u32 yact_st = var->yoffset;			/* resolution			*/

    u16 xpos = (var->nonstd>>8) & 0xfff;      //visiable pos in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;  //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;

    //u32 ScaleYrgbX=0x1000,ScaleYrgbY=0x1000;
    //u32 ScaleCbrX=0x1000, ScaleCbrY=0x1000;

    u8 data_format = var->nonstd&0x0f;
   // u32 win0_en = var->reserved[2];
   // u32 y_addr = var->reserved[3];       //user alloc buf addr y
   // u32 uv_addr = var->reserved[4];

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
   	CHK_SUSPEND(inf);
    if((var->rotate & 0x1ff ) == 90 || (var->rotate & 0x1ff ) == 270)
    {
        #ifdef CONFIG_FB_ROTATE_VIDEO
        xpos = (var->nonstd>>20) & 0xfff;      //visiable pos in panel
        ypos = (var->nonstd>>8) & 0xfff;
        xsize = (var->grayscale>>20) & 0xfff;  //visiable size in panel
        ysize = (var->grayscale>>8) & 0xfff;
        #else //CONFIG_FB_ROTATE_VIDEO
        printk("LCDC not support rotate!\n");
        return -EINVAL;
      #endif
    } else{
        xpos = (xpos * screen->x_res) / inf->panel1_info.x_res;
        ypos = (ypos * screen->y_res) / inf->panel1_info.y_res;
        xsize = (xsize * screen->x_res) / inf->panel1_info.x_res;
        ysize = (ysize * screen->y_res) / inf->panel1_info.y_res;
    }
	/* calculate y_offset,c_offset,line_length,cblen and crlen  */
    switch (data_format)
    {
    case 0: // rgb
        switch(var->bits_per_pixel)
        {
        case 16:    // rgb565
            format = 1;
            fix->line_length = 2 * xvir;
            par->y_offset = (yact_st*xvir + xact_st)*2;
            break;
        case 32:    // rgb888
            format = 0;
            fix->line_length = 4 * xvir;
            par->y_offset = (yact_st*xvir + xact_st)*4;
            break;
        default:
            return -EINVAL;
        }
        break;
    case 1: // yuv422
        format = 2;
        fix->line_length = xvir;
        cblen = crlen = (xvir*yvir)/2;
        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = yact_st*xvir + xact_st;
        break;
    case 2: // yuv4200
        format = 3;
        fix->line_length = xvir;
        cblen = crlen = (xvir*yvir)/4;

        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = (yact_st/2)*xvir + xact_st;

        break;
    case 3: // yuv4201
        format = 4;
        fix->line_length = xvir;
        par->y_offset = (yact_st/2)*2*xvir + (xact_st)*2;
        par->c_offset = (yact_st/2)*xvir + xact_st;
        cblen = crlen = (xvir*yvir)/4;
        break;
    case 4: // none
    case 5: // yuv444
        format = 5;
        fix->line_length = xvir;
        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = yact_st*2*xvir + xact_st*2;
        cblen = crlen = (xvir*yvir);
        break;
    default:
        return -EINVAL;
    }

    smem_len = fix->line_length * yvir + cblen + crlen;
    map_size = PAGE_ALIGN(smem_len);

   // fix->smem_start = y_addr;
    fix->smem_len = smem_len;
  //  fix->mmio_start = uv_addr;

 //   par->addr_seted = ((-1==(int)y_addr) || (0==(int)y_addr) || (win0_en==0)) ? 0 : 1;
    fbprintk("buffer alloced by user fix->smem_start = %8x, fix->smem_len = %8x, fix->mmio_start = %8x \n", (u32)fix->smem_start, (u32)fix->smem_len, (u32)fix->mmio_start);

    par->format = format;
    par->xpos = xpos;
    par->ypos = ypos;
    par->xsize = xsize;
    par->ysize = ysize;
    win0_set_par(info);

    if ( wq_condition2 == 0 ) {
        wait_event_interruptible_timeout(wq,  wq_condition2, HZ/20);
    }
    wq_condition2 = 0;

#ifdef CONFIG_FB_ROTATE_VIDEO
//need refresh	,zyc add		
	if((has_set_rotate == true) && (last_yuv_phy[0] != 0) && (last_yuv_phy[1] != 0))
	{
		u32 yuv_phy[2];
		struct rk29_ipp_req ipp_req;
		static u32 dstoffset = 0;
		static int fb_index = 0;
		memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
		yuv_phy[0] = last_yuv_phy[0];
		yuv_phy[1] = last_yuv_phy[1];
		yuv_phy[0] += par->y_offset;
		yuv_phy[1] += par->c_offset;
        #if 0
		if((var->rotate == 90) ||(var->rotate == 270))
        #else
        if(var->rotate%360 != 0)
        #endif
			{
                #ifdef CONFIG_FB_ROTATE_VIDEO 
				dstoffset = (dstoffset+1)%2;
				ipp_req.src0.fmt = 3;
				ipp_req.src0.YrgbMst = yuv_phy[0];
				ipp_req.src0.CbrMst = yuv_phy[1];
				ipp_req.src0.w = var->xres;
				ipp_req.src0.h = var->yres;
				
				ipp_req.src_vir_w= (var->xres + 15) & (~15);
				ipp_req.dst_vir_w=screen->x_res;

                ipp_req.dst0.fmt = 3;
				#ifdef CONFIG_FB_MIRROR_X_Y
				if((var->rotate & 0x1ff)!=0 &&(var->rotate&(X_MIRROR|Y_MIRROR))!= 0 )
				{
				ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*4;
				ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*5;
				 }
				else
				{
					ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
					ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);

				}
				if(var->xres > screen->x_res)
				   {
					ipp_req.dst0.w = screen->x_res;
					ipp_req.dst0.h = screen->y_res;
				  }   else	{
					  ipp_req.dst0.w = var->xres;
				 	  ipp_req.dst0.h = var->yres;
				   }
				 ipp_req.dst_vir_w = (ipp_req.dst0.w + 15) & (~15);
				ipp_req.timeout = 100;
				if((var->rotate & 0x1ff) == 90)
					ipp_req.flag = IPP_ROT_90;
                else if ((var->rotate & 0x1ff) == 180)
					ipp_req.flag = IPP_ROT_180;
				else if((var->rotate & 0x1ff) == 270)
					ipp_req.flag = IPP_ROT_270;
				else if((var->rotate & X_MIRROR) == X_MIRROR )
					ipp_req.flag = IPP_ROT_X_FLIP;
				else if((var->rotate & Y_MIRROR) == Y_MIRROR)
					ipp_req.flag = IPP_ROT_Y_FLIP;
				//ipp_do_blit(&ipp_req);
				ipp_blit_sync(&ipp_req);
				yuv_phy[0] = ipp_req.dst0.YrgbMst;
				yuv_phy[1] = ipp_req.dst0.CbrMst;
				if((var->rotate & 0x1ff)!=0 &&(var->rotate&(X_MIRROR|Y_MIRROR))!= 0 )
				{
					memset(&ipp_req,0,sizeof(struct rk29_ipp_req));
					
					if((var->rotate & X_MIRROR) == X_MIRROR)
						ipp_req.flag = IPP_ROT_X_FLIP;
					else if((var->rotate & Y_MIRROR) == Y_MIRROR)
						ipp_req.flag = IPP_ROT_Y_FLIP;
					else 
						printk(">>>>>> %d rotate is not support!\n",var->rotate);
					
				  if(var->xres > screen->x_res)
				   {
					ipp_req.dst0.w = screen->x_res;
					ipp_req.dst0.h = screen->y_res;
				  }   else	{
					  ipp_req.dst0.w = var->xres;
				 	  ipp_req.dst0.h = var->yres;
				   }
					ipp_req.src0.fmt = 3;
					ipp_req.src0.YrgbMst = yuv_phy[0];
					ipp_req.src0.CbrMst = yuv_phy[1];
					ipp_req.src0.w = ipp_req.dst0.w;
					ipp_req.src0.h = ipp_req.dst0.h;
				
					ipp_req.src_vir_w= (ipp_req.dst0.w + 15) & (~15);
					ipp_req.dst_vir_w= (ipp_req.dst0.w + 15) & (~15);

					ipp_req.dst0.fmt = 3;
					ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
					ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);

					ipp_req.timeout = 100;
					ipp_blit_sync(&ipp_req);
					yuv_phy[0] = ipp_req.dst0.YrgbMst;
					yuv_phy[1] = ipp_req.dst0.CbrMst;
        		}
                fbprintk("yaddr=0x%x,uvaddr=0x%x\n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
				#else
				ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
                ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);
                if(var->xres > screen->x_res)
                {
                    ipp_req.dst0.w = screen->x_res;
                    ipp_req.dst0.h = screen->y_res;
                }   else  {
					ipp_req.dst0.w = var->xres;
					ipp_req.dst0.h = var->yres;

                }
                ipp_req.dst_vir_w = (ipp_req.dst0.w + 15) & (~15);
                ipp_req.timeout = 100;
                if(var->rotate == 90)
                    ipp_req.flag = IPP_ROT_90;
                else if(var->rotate == 180)
                    ipp_req.flag = IPP_ROT_180;
                else if(var->rotate == 270)
                    ipp_req.flag = IPP_ROT_270;
                //ipp_do_blit(&ipp_req);
                ipp_blit_sync(&ipp_req);
				
                fbprintk("yaddr=0x%x,uvaddr=0x%x\n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
                yuv_phy[0] = ipp_req.dst0.YrgbMst;
                yuv_phy[1] = ipp_req.dst0.CbrMst; 
				#endif
                fix->smem_start = yuv_phy[0];
                fix->mmio_start = yuv_phy[1];
                #else //CONFIG_FB_ROTATE_VIDEO
                printk("LCDC not support rotate!\n");
                #endif
            }
            else
			{
			
				fix->smem_start = yuv_phy[0];
				fix->mmio_start = yuv_phy[1];
			}
			
		LcdWrReg(inf, WIN0_YRGB_MST, yuv_phy[0]);
		LcdWrReg(inf, WIN0_CBR_MST, yuv_phy[1]);
		// enable win0 after the win0 par is seted
		LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(par->par_seted && par->addr_seted));
		par->addr_seted = 1;
		LcdWrReg(inf, REG_CFG_DONE, 0x01);
		if(par->addr_seted ) {
		unsigned long flags;

		local_irq_save(flags);
		par->mirror.y_offset = yuv_phy[0];
		par->mirror.c_offset = yuv_phy[1];
		local_irq_restore(flags);

		mcu_refresh(inf);
		//printk("0x%.8x 0x%.8x mirror\n", par->mirror.y_offset, par->mirror.c_offset);
		}

	}

    has_set_rotate = false;
#endif
    return 0;
}

static int fb1_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct win0_par *par = info->par;
     // enable win0 after the win0 addr is seted

    win0_pan(info);
    par->par_seted = 1;
    return 0;
}

int fb1_open(struct fb_info *info, int user)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
	struct rk29fb_screen *screen = inf->cur_screen;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    par->par_seted = 0;
    par->addr_seted = 0;
    inf->video_mode = 1;
    wq_condition2 = 1;
#ifdef CONFIG_FB_ROTATE_VIDEO
   //reinitialize  the var when open,zyc
    last_yuv_phy[0] = 0;
    last_yuv_phy[1] = 0;
    has_set_rotate = 0;
#endif
    if(par->refcount) {
        printk(">>>>>> fb1 has opened! \n");
        return -EACCES;
    } else {
        par->refcount++;
        win0_blank(FB_BLANK_NORMAL, info);
		if(screen->x_res>1280)
		fb1_open_init=1;
        fb0_set_par(inf->fb0);
        fb1_open_init=0;
	    rk29fb_notify(inf, RK29FB_EVENT_FB1_ON);
        return 0;
    }
}

int fb1_release(struct fb_info *info, int user)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
	struct fb_var_screeninfo *var0 = &info->var;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
    if(inf->setFlag == 0)
    {
        wait_event_interruptible_timeout(wq, inf->setFlag, HZ*10);
    } 
    if(par->refcount) {
        par->refcount--;
        inf->video_mode = 0;
        par->par_seted = 0;
        par->addr_seted = 0;
        win1_blank(FB_BLANK_NORMAL, info);

        //if(inf->cur_screen->type != SCREEN_HDMI)
            fb0_set_par(inf->fb0);

        // unmap memory
        info->screen_base = 0;
        info->fix.smem_start = 0;
        info->fix.smem_len = 0;
		// clean the var param
		memset(var0, 0, sizeof(struct fb_var_screeninfo));
	    rk29fb_notify(inf, RK29FB_EVENT_FB1_OFF);
        #ifdef CONFIG_CLOSE_WIN1_DYNAMIC   
         cancel_delayed_work_sync(&rk29_win1_check_work);
        #endif  
    }

    return 0;
}

static int fb1_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
    struct fb_fix_screeninfo *fix0 = &info->fix;
    struct fb_var_screeninfo *var = &info->var;
    void __user *argp = (void __user *)arg;
    
#ifdef CONFIG_FB_ROTATE_VIDEO   
     struct rk29fb_screen *screen = inf->cur_screen;
     struct rk29_ipp_req ipp_req;
     static u32 dstoffset = 0;
    memset(&ipp_req, 0, sizeof(struct rk29_ipp_req)); 
#endif

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
    fbprintk("win0fb_ioctl cmd = %8x, arg = %8x \n", (u32)cmd, (u32)arg);

	CHK_SUSPEND(inf);

    switch(cmd)
    {
    case FB1_IOCTL_GET_PANEL_SIZE:    //get panel size
        {
            u32 panel_size[2];
             if((var->rotate & 0x1ff) == 270 ||(var->rotate & 0x1ff) == 90) {
                panel_size[0] = inf->panel1_info.y_res; //inf->cur_screen->y_res; change for hdmi video size
                panel_size[1] = inf->panel1_info.x_res;
            } else {
                panel_size[0] = inf->panel1_info.x_res;
                panel_size[1] = inf->panel1_info.y_res;
            }

            if(copy_to_user(argp, panel_size, 8))  return -EFAULT;
        }
        break;

    case FB1_IOCTL_SET_YUV_ADDR:    //set y&uv address to register direct
        {
            u32 yuv_phy[2];
            if (copy_from_user(yuv_phy, argp, 8))
			    return -EFAULT;
#ifdef CONFIG_FB_ROTATE_VIDEO 
		//add by zyc
		if(has_set_rotate == true)
		break;
		last_yuv_phy[0] = yuv_phy[0];
		last_yuv_phy[1] = yuv_phy[1];
#endif

            fix0->smem_start = yuv_phy[0];
            fix0->mmio_start = yuv_phy[1];
            yuv_phy[0] += par->y_offset;
            yuv_phy[1] += par->c_offset;

#ifdef	CONFIG_FB_MIRRORING
		if(v_W0_ENABLE(par->par_seted && par->addr_seted))
		{
			if(video_data_to_mirroring!=NULL)
				 video_data_to_mirroring(info,yuv_phy);
		}	
#endif


         
            #if 0
    		if((var->rotate == 90) ||(var->rotate == 270))
            #else
            if(var->rotate%360 != 0)
            #endif
            {
                #ifdef CONFIG_FB_ROTATE_VIDEO 
                dstoffset = (dstoffset+1)%2;
                ipp_req.src0.fmt = 3;
                ipp_req.src0.YrgbMst = yuv_phy[0];
                ipp_req.src0.CbrMst = yuv_phy[1];
          	    ipp_req.src0.w = var->xres ;
                ipp_req.src0.h = var->yres ;
  		        ipp_req.src_vir_w= (var->xres + 15) & (~15);
		        ipp_req.dst_vir_w=screen->x_res;

                ipp_req.dst0.fmt = 3;
				#ifdef CONFIG_FB_MIRROR_X_Y
				if((var->rotate & 0x1ff)!=0 &&(var->rotate&(X_MIRROR|Y_MIRROR))!= 0 )
				{
				ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*4;
				ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*5;
				 }
				else
				{
					ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
					ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);

				}
				if(var->xres > screen->x_res)
				   {
					ipp_req.dst0.w = screen->x_res;
					ipp_req.dst0.h = screen->y_res;
				  }   else	{
					  ipp_req.dst0.w = var->xres;
				 	  ipp_req.dst0.h = var->yres;
				   }
				 ipp_req.dst_vir_w = (ipp_req.dst0.w + 15) & (~15);
				ipp_req.timeout = 100;
				if((var->rotate & 0x1ff) == 90)
					ipp_req.flag = IPP_ROT_90;
                else if ((var->rotate & 0x1ff) == 180)
					ipp_req.flag = IPP_ROT_180;
				else if((var->rotate & 0x1ff) == 270)
					ipp_req.flag = IPP_ROT_270;
				else if((var->rotate & X_MIRROR) == X_MIRROR )
					ipp_req.flag = IPP_ROT_X_FLIP;
				else if((var->rotate & Y_MIRROR) == Y_MIRROR)
					ipp_req.flag = IPP_ROT_Y_FLIP;
				//ipp_do_blit(&ipp_req);
				ipp_blit_sync(&ipp_req);
				yuv_phy[0] = ipp_req.dst0.YrgbMst;
				yuv_phy[1] = ipp_req.dst0.CbrMst;
				if((var->rotate & 0x1ff)!=0 &&(var->rotate&(X_MIRROR|Y_MIRROR))!= 0 )
				{
					memset(&ipp_req,0,sizeof(struct rk29_ipp_req));
					
					if((var->rotate & X_MIRROR) == X_MIRROR)
						ipp_req.flag = IPP_ROT_X_FLIP;
					else if((var->rotate & Y_MIRROR) == Y_MIRROR)
						ipp_req.flag = IPP_ROT_Y_FLIP;
					else 
						printk(">>>>>> %d rotate is not support!\n",var->rotate);
					
				  if(var->xres > screen->x_res)
				   {
					ipp_req.dst0.w = screen->x_res;
					ipp_req.dst0.h = screen->y_res;
				  }   else	{
					  ipp_req.dst0.w = var->xres;
				 	  ipp_req.dst0.h = var->yres;
				   }
					ipp_req.src0.fmt = 3;
					ipp_req.src0.YrgbMst = yuv_phy[0];
					ipp_req.src0.CbrMst = yuv_phy[1];
					ipp_req.src0.w = ipp_req.dst0.w;
					ipp_req.src0.h = ipp_req.dst0.h;
				
					ipp_req.src_vir_w= (ipp_req.dst0.w + 15) & (~15);
					ipp_req.dst_vir_w= (ipp_req.dst0.w + 15) & (~15);

					ipp_req.dst0.fmt = 3;
					ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
					ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);

					ipp_req.timeout = 100;
					ipp_blit_sync(&ipp_req);
					yuv_phy[0] = ipp_req.dst0.YrgbMst;
					yuv_phy[1] = ipp_req.dst0.CbrMst;
        		}
                fbprintk("yaddr=0x%x,uvaddr=0x%x\n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
				#else
				ipp_req.dst0.YrgbMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*2*dstoffset;
                ipp_req.dst0.CbrMst = inf->fb0->fix.mmio_start + screen->x_res*screen->y_res*(2*dstoffset+1);
                if(var->xres > screen->x_res)
                {
                    ipp_req.dst0.w = screen->x_res;
                    ipp_req.dst0.h = screen->y_res;
                }   else  {
					ipp_req.dst0.w = var->xres;
					ipp_req.dst0.h = var->yres;

                }
                ipp_req.dst_vir_w = (ipp_req.dst0.w + 15) & (~15);
                ipp_req.timeout = 100;
                if(var->rotate == 90)
                    ipp_req.flag = IPP_ROT_90;
                else if(var->rotate == 180)
                    ipp_req.flag = IPP_ROT_180;
                else if(var->rotate == 270)
                    ipp_req.flag = IPP_ROT_270;
                //ipp_do_blit(&ipp_req);
                ipp_blit_sync(&ipp_req);
				
                fbprintk("yaddr=0x%x,uvaddr=0x%x\n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
                yuv_phy[0] = ipp_req.dst0.YrgbMst;
                yuv_phy[1] = ipp_req.dst0.CbrMst; 
				#endif
                fix0->smem_start = yuv_phy[0];
                fix0->mmio_start = yuv_phy[1];
                #else //CONFIG_FB_ROTATE_VIDEO
                printk("LCDC not support rotate!\n");
                #endif
            }
      
            LcdWrReg(inf, WIN0_YRGB_MST, yuv_phy[0]);
            LcdWrReg(inf, WIN0_CBR_MST, yuv_phy[1]);
            // enable win0 after the win0 par is seted
            LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(par->par_seted && par->addr_seted));
            par->addr_seted = 1;
            LcdWrReg(inf, REG_CFG_DONE, 0x01);
            if(par->par_seted) {
                unsigned long flags;

                local_irq_save(flags);
                par->mirror.y_offset = yuv_phy[0];
                par->mirror.c_offset = yuv_phy[1];
                local_irq_restore(flags);

                mcu_refresh(inf);
                //printk("0x%.8x 0x%.8x mirror\n", par->mirror.y_offset, par->mirror.c_offset);
            }
        }
        break;

    case FB1_IOCTL_SET_ROTATE:    //change MCU panel scan direction
        fbprintk(">>>>>> change lcdc direction(%d) \n", (int)arg);
      #ifdef CONFIG_FB_ROTATE_VIDEO 
	  	//zyc add
    	has_set_rotate = true;
		#ifdef CONFIG_FB_MIRROR_X_Y
        if( ((arg&0x1ff)%90) == 0 && ( arg&(~0xfff))==0 )
			{
			if( (arg&(  X_MIRROR | Y_MIRROR ) )== (X_MIRROR | Y_MIRROR) )
				var->rotate = ROTATE_180;
			else if((arg&( ROTATE_90 | X_MIRROR | Y_MIRROR ) )== ( ROTATE_90 | X_MIRROR | Y_MIRROR))
				var->rotate = ROTATE_270;
			else if((arg&( ROTATE_180 | X_MIRROR | Y_MIRROR ) )== ( ROTATE_180 | X_MIRROR | Y_MIRROR))
				var->rotate = ROTATE_90;
			else if((arg&( ROTATE_270 | X_MIRROR | Y_MIRROR ) )== ( ROTATE_270 | X_MIRROR | Y_MIRROR))
				var->rotate = ROTATE_0;
			else
		#else 
		if(arg == ROTATE_0 || arg==ROTATE_90 || arg == ROTATE_180 || arg==ROTATE_270)
		{
		#endif
				var->rotate = arg;
		}
		else
			printk(">>>>>> %d rotate is not support!\n",(int)arg);
    	
      #else //CONFIG_FB_ROTATE_VIDEO
        printk("LCDC not support rotate!\n");
      #endif
        break;
    case FB1_IOCTL_SET_WIN0_TOP:
        fbprintk(">>>>>> FB1_IOCTL_SET_WIN0_TOP %d\n",arg);
        LcdMskReg(inf, DSP_CTRL0, m_W0_ON_TOP, v_W0_ON_TOP(arg));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;
    default:
        break;
    }
    return 0;
}

static struct fb_ops fb1_ops = {
	.owner		= THIS_MODULE,
	.fb_open    = fb1_open,
	.fb_release = fb1_release,
	.fb_check_var	= fb1_check_var,
	.fb_set_par	= fb1_set_par,
	.fb_blank	= fb1_blank,
    .fb_pan_display = fb1_pan_display,
    .fb_ioctl = fb1_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static struct fb_ops fb0_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= fb0_check_var,
	.fb_set_par = fb0_set_par,
	.fb_blank   = fb0_blank,
	.fb_pan_display = fb0_pan_display,
    .fb_ioctl = fb0_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	//.fb_cursor      = rk29_set_cursor,
};

int fb_get_video_mode(void)
{
	struct rk29fb_inf *inf;
	if(!g_pdev)
		return 0;
	inf = platform_get_drvdata(g_pdev);
	return inf->video_mode;
}
/*
enable: 1, switch to tv or hdmi; 0, switch to lcd
*/
int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable )
{
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
   // struct rk29fb_info *mach_info = g_pdev->dev.platform_data;
    struct rk29fb_info *mach_info = g_pdev->dev.platform_data;

    memcpy(&inf->panel2_info, screen, sizeof( struct rk29fb_screen ));

    if(enable)inf->cur_screen = &inf->panel2_info;
    else inf->cur_screen = &inf->panel1_info;

#ifdef CONFIG_FB_DSP_LUT
	rk29_fb_lut_config(1);
#endif
    /* Black out, because some display device need clock to standby */
    //LcdMskReg(inf, DSP_CTRL_REG1, m_BLACK_OUT, v_BLACK_OUT(1));
   // LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
   // LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
    LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(1));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
    wake_lock(&idlelock);
    msleep(20);
    wake_unlock(&idlelock);

    if(inf->cur_screen->standby)    inf->cur_screen->standby(1);
    // operate the display_on pin to power down the lcd
   
    if(enable && mach_info->io_disable)mach_info->io_disable();  //close lcd out
    else if (mach_info->io_enable)mach_info->io_enable();       //open lcd out
    
    load_screen(inf->fb0, 0);
	mcu_refresh(inf);

    fb1_set_par(inf->fb1);
    fb0_set_par(inf->fb0);
    LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(0));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
    rk29fb_notify(inf, enable ? RK29FB_EVENT_HDMI_ON : RK29FB_EVENT_HDMI_OFF);
    return 0;
}

static ssize_t dsp_win0_info_read(struct device *device,
			    struct device_attribute *attr, char *buf)
{
    //char * s = _buf;
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
    struct rk29fb_screen *screen = inf->cur_screen;

    u16 xpos=0,ypos=0,xsize=0,ysize=0;

    fbprintk("%s\n",__FUNCTION__);
    xpos = LcdRdReg(inf, WIN0_DSP_ST) & 0xffff;
    ypos = (LcdRdReg(inf, WIN0_DSP_ST)>>16) & 0xffff;

    xpos -= (screen->left_margin + screen->hsync_len);
    ypos -= (screen->upper_margin + screen->vsync_len);

    xsize=LcdRdReg(inf, WIN0_DSP_INFO)& 0xffff;
    ysize=(LcdRdReg(inf, WIN0_DSP_INFO)>>16) & 0xffff;
    fbprintk("%s %d , %d, %d ,%d\n",__FUNCTION__,xpos,ypos,xsize,ysize);
	rk29_set_cursor_test(inf->fb0);

    return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d\n", xpos,ypos,xsize,ysize);
}

static ssize_t dsp_win0_info_write(struct device *device,
			   struct device_attribute *attr, const char *buf, size_t count)
{
     printk("%s\n",__FUNCTION__);
     printk("%s %x \n",__FUNCTION__,*buf);
     return count;
}

static DEVICE_ATTR(dsp_win0_info,  S_IRUGO|S_IWUSR, dsp_win0_info_read, dsp_win0_info_write);

static ssize_t dsp_win0_scale_read(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
	return snprintf(buf, PAGE_SIZE, "xscale=%d yscale=%d\n", inf->scaleinfo.xrate, inf->scaleinfo.yrate);
}

static ssize_t dsp_win0_scale_write(struct device *device,
			   struct device_attribute *attr, const char *buf, size_t count)
{
	struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
	int scale;
	
	if(!strncmp(buf, "xscale", 6)) {
		sscanf(buf, "xscale=%d", &scale);
		if(scale >= 0 && scale <= 100)
			inf->scaleinfo.xrate = scale;
	}
	else if(!strncmp(buf, "yscale", 6)) {
		sscanf(buf, "yscale=%d", &scale);
		if(scale >= 0 && scale <= 100)
			inf->scaleinfo.yrate = scale;
	}
	else {
		sscanf(buf, "%d", &scale);
		if(scale >= 0 && scale <= 100) {
			inf->scaleinfo.xrate = scale;
			inf->scaleinfo.yrate = scale;
		}
	}
	printk("scale rate x %d y %d\n", inf->scaleinfo.xrate, inf->scaleinfo.yrate);
	fb0_set_par(inf->fb0);
	return count;
}

static DEVICE_ATTR(dsp_win0_scale, S_IRUGO|S_IWUSR, dsp_win0_scale_read, dsp_win0_scale_write);

static irqreturn_t rk29fb_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct win0_par *par = (struct win0_par *)inf->fb1->par;
    if(!inf)
        return IRQ_HANDLED;

	//fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    LcdMskReg(inf, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));

	if(SCREEN_MCU == inf->cur_screen->type)
	{
        inf->mcu_isrcnt = !inf->mcu_isrcnt;
        if(inf->mcu_isrcnt)
            return IRQ_HANDLED;

        if(IsMcuUseFmk())
        {
            if(LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST) && (inf->mcu_fmksync == 0))
            {
                inf->mcu_fmksync = 1;
                 if(inf->cur_screen->refresh)
                   inf->cur_screen->refresh(REFRESH_END);
                inf->mcu_fmksync = 0;
            }
            else
            {
                return IRQ_HANDLED;
            }
        }
        else
        {
            if(inf->mcu_needflush) {
                if(inf->cur_screen->refresh)
                    inf->cur_screen->refresh(REFRESH_PRE);
                inf->mcu_needflush = 0;
                inf->mcu_isrcnt = 0;
                LcdSetRegisterBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST);
            } else {
                if(inf->cur_screen->refresh)
                    inf->cur_screen->refresh(REFRESH_END);
            }
        }
	}
#if ANDROID_USE_THREE_BUFS
    new_frame_seted = 1;
#endif

    if(waitqueue_active(&par->wait)) {
        if (par->mirror.c_offset == 0)
            printk("error: no new buffer to display\n");

        par->done.y_offset = par->displ.y_offset;
        par->done.c_offset = par->displ.c_offset;
        par->displ.y_offset = par->mirror.y_offset;
        par->displ.c_offset = par->mirror.c_offset;
        par->mirror.y_offset = 0;
        par->mirror.c_offset = 0;

        //printk("0x%.8x 0x%.8x displaying\n", par->displ.y_offset, par->displ.c_offset);
        //printk("0x%.8x 0x%.8x done\n", par->done.y_offset, par->done.c_offset);
        wake_up_interruptible(&par->wait);
    }

    wq_condition2 = 1;
	wq_condition = 1;
 	wake_up_interruptible(&wq);

	rk29fb_irq_notify_ddr();
	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

struct suspend_info {
	struct early_suspend early_suspend;
	struct rk29fb_inf *inf;
};

static void rk29fb_early_suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);

    struct rk29fb_inf *inf = info->inf;
    struct rk29fb_info *mach_info = g_pdev->dev.platform_data;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk29fb_suspend fail! \n");
        return;
    }

#ifdef CONFIG_CLOSE_WIN1_DYNAMIC   
     cancel_delayed_work_sync(&rk29_win1_check_work);
#endif  

    if((inf->cur_screen != &inf->panel2_info) && mach_info->io_disable)  // close lcd pwr when output screen is lcd
       mach_info->io_disable();  //close lcd out 

	if(inf->cur_screen->standby)
	{
		fbprintk(">>>>>> power down the screen! \n");
		inf->cur_screen->standby(1);
	}
	LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE | m_W1_ENABLE, v_W0_ENABLE(0) | v_W1_ENABLE(0));

    LcdMskReg(inf, DSP_CTRL0, m_HSYNC_POLARITY | m_VSYNC_POLARITY | m_DEN_POLARITY ,
       v_HSYNC_POLARITY(1) | v_VSYNC_POLARITY(1) | v_DEN_POLARITY(1) );

    LcdMskReg(inf, DSP_CTRL1, m_BLANK_MODE , v_BLANK_MODE(1));
    LcdMskReg(inf, SYS_CONFIG, m_STANDBY, v_STANDBY(1));
   	LcdWrReg(inf, REG_CFG_DONE, 0x01);

	if(!inf->in_suspend)
	{
		fbprintk(">>>>>> diable the lcdc clk! \n");
		wake_lock(&idlelock);
		msleep(100);
		wake_unlock(&idlelock);
        clk_disable(inf->aclk_ddr_lcdc);
        clk_disable(inf->aclk_disp_matrix);
        clk_disable(inf->hclk_cpu_display);
        clk_disable(inf->clk);
    	if (inf->dclk){
            clk_disable(inf->dclk);
        }
        if(inf->clk){
            clk_disable(inf->aclk);
        }
        //clk_disable(inf->pd_display);

		inf->in_suspend = 1;
	}
}

static void rk29fb_early_resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
					early_suspend);

    struct rk29fb_inf *inf = info->inf;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct rk29fb_info *mach_info = g_pdev->dev.platform_data;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
    if(!inf) {
        printk("inf==0, rk29fb_resume fail! \n");
        return ;
    }

    if(inf->in_suspend)
	{
	    inf->in_suspend = 0;
    	fbprintk(">>>>>> enable the lcdc clk! \n");
       // clk_enable(inf->pd_display);
        clk_enable(inf->aclk_disp_matrix);
        clk_enable(inf->hclk_cpu_display);
        clk_enable(inf->clk);
        clk_enable(inf->aclk_ddr_lcdc);

        if (inf->dclk){
            clk_enable(inf->dclk);
        }
        if(inf->clk){
            clk_enable(inf->aclk);
        }
        usleep_range(100*1000, 100*1000);
	}
    LcdMskReg(inf, DSP_CTRL1, m_BLANK_MODE , v_BLANK_MODE(0));
    LcdMskReg(inf, SYS_CONFIG, m_STANDBY, v_STANDBY(0));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    LcdMskReg(inf, DSP_CTRL0, m_HSYNC_POLARITY | m_VSYNC_POLARITY | m_DEN_POLARITY ,
       v_HSYNC_POLARITY(screen->pin_hsync) | v_VSYNC_POLARITY(screen->pin_vsync) | v_DEN_POLARITY(screen->pin_den) );

	if(inf->cur_screen->standby)
	{
		fbprintk(">>>>>> power on the screen! \n");
		inf->cur_screen->standby(0);
	}
    usleep_range(10*1000, 10*1000);
    memcpy((u8*)inf->preg, (u8*)&inf->regbak, 0xa4);  //resume reg
#ifdef CONFIG_FB_DSP_LUT
	rk29_fb_lut_config(1);
#endif
    usleep_range(40*1000, 40*1000);
    
    if((inf->cur_screen != &inf->panel2_info) && mach_info->io_enable)  // open lcd pwr when output screen is lcd
       mach_info->io_enable();  //close lcd out 
       	
}

static struct suspend_info suspend_info = {
	.early_suspend.suspend = rk29fb_early_suspend,
	.early_suspend.resume = rk29fb_early_resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif
struct fb_info *g_fb0_inf = NULL;  //add cym@rk 20101027 for charger logo
static int __devinit rk29fb_probe (struct platform_device *pdev)
{
    struct rk29fb_inf *inf = NULL;
    struct resource *res = NULL;
    struct resource *mem = NULL;
    struct rk29fb_info *mach_info = NULL;
    struct rk29fb_screen *screen = NULL;
    struct win0_par* par = NULL;
	int irq = 0;
    int ret = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    /* Malloc rk29fb_inf and set it to pdev for drvdata */
    fbprintk(">> Malloc rk29fb_inf and set it to pdev for drvdata \n");
    inf = kmalloc(sizeof(struct rk29fb_inf), GFP_KERNEL);
    if(!inf)
    {
        dev_err(&pdev->dev, ">> inf kmalloc fail!");
        ret = -ENOMEM;
		goto release_drvdata;
    }
    memset(inf, 0, sizeof(struct rk29fb_inf));
	platform_set_drvdata(pdev, inf);

    mach_info = pdev->dev.platform_data;
    /* Fill screen info and set current screen */
    fbprintk(">> Fill screen info and set current screen \n");
   #ifdef CONFIG_DEFAULT_OUT_HDMI  // set hdmi for default output 
    hdmi_get_default_resolution(&inf->panel1_info);
   #else
    set_lcd_info(&inf->panel1_info, mach_info->lcd_info);
   #endif
#if  defined(CONFIG_MACH_RK29_ITV_HOTDOG) || defined(CONFIG_MACH_RK29_ITV)
	inf->scaleinfo.xrate = 100;
	inf->scaleinfo.yrate = 100;
#endif
    inf->cur_screen = &inf->panel1_info;
    screen = inf->cur_screen;
    if(SCREEN_NULL==screen->type)
    {
        dev_err(&pdev->dev, ">> Please select a display device! \n");
        ret = -EINVAL;
		goto release_drvdata;
    }

    /* get virtual basic address of lcdc register */
    fbprintk(">> get virtual basic address of lcdc register \n");
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcdc reg");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory registers\n");
        ret = -ENOENT;
		goto release_drvdata;
    }
    inf->reg_phy_base = res->start;
    inf->len = (res->end - res->start) + 1;
    mem = request_mem_region(inf->reg_phy_base, inf->len, pdev->name);
    if (mem == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory region\n");
        ret = -ENOENT;
		goto release_drvdata;
    }
    fbprintk("inf->reg_phy_base = 0x%08x, inf->len = %d \n", inf->reg_phy_base, inf->len);
    inf->reg_vir_base = ioremap(inf->reg_phy_base, inf->len);
    if (inf->reg_vir_base == NULL)
    {
        dev_err(&pdev->dev, "ioremap() of registers failed\n");
        ret = -ENXIO;
		goto release_drvdata;
    }
    inf->preg = (LCDC_REG*)inf->reg_vir_base;

    /* Prepare win1 info */
    fbprintk(">> Prepare win1 info \n");
   	inf->fb0 = framebuffer_alloc(sizeof(struct win0_par), &pdev->dev);
    if(!inf->fb0)
    {
        dev_err(&pdev->dev, ">> fb0 framebuffer_alloc fail!");
		inf->fb0 = NULL;
        ret = -ENOMEM;
		goto release_win1fb;
    }

    par = (struct win0_par*)inf->fb0->par;
    strcpy(inf->fb0->fix.id, "fb0");
    inf->fb0->fix.type        = FB_TYPE_PACKED_PIXELS;
    inf->fb0->fix.type_aux    = 0;
    inf->fb0->fix.xpanstep    = 1;
    inf->fb0->fix.ypanstep    = 1;
    inf->fb0->fix.ywrapstep   = 0;
    inf->fb0->fix.accel       = FB_ACCEL_NONE;
    inf->fb0->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->fb0->fix.smem_len    = 0;
    inf->fb0->fix.line_length = 0;
    inf->fb0->fix.smem_start  = 0;

    inf->fb0->var.xres = screen->x_res;
    inf->fb0->var.yres = screen->y_res;
    inf->fb0->var.bits_per_pixel = 16;
    inf->fb0_color_deepth = 0;
    inf->fb0->var.xres_virtual = screen->x_res;
    inf->fb0->var.yres_virtual = screen->y_res;
    inf->fb0->var.width = screen->width;
    inf->fb0->var.height = screen->height;
    //inf->fb0->var.pixclock = div_u64(1000000000000llu, screen->pixclock);
    inf->fb0->var.left_margin = screen->left_margin;
    inf->fb0->var.right_margin = screen->right_margin;
    inf->fb0->var.upper_margin = screen->upper_margin;
    inf->fb0->var.lower_margin = screen->lower_margin;
    inf->fb0->var.vsync_len = screen->vsync_len;
    inf->fb0->var.hsync_len = screen->hsync_len;
    inf->fb0->var.red    = def_rgb_16.red;
    inf->fb0->var.green  = def_rgb_16.green;
    inf->fb0->var.blue   = def_rgb_16.blue;
    inf->fb0->var.transp = def_rgb_16.transp;

    inf->fb0->var.nonstd      = 0;  //win1 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->fb0->var.grayscale   = 0;  //win1 transprent mode & value(mode<<8 + value)
    inf->fb0->var.activate    = FB_ACTIVATE_NOW;
    inf->fb0->var.accel_flags = 0;
    inf->fb0->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->fb0->fbops           = &fb0_ops;
    inf->fb0->flags           = FBINFO_FLAG_DEFAULT;
    inf->fb0->pseudo_palette  = par->pseudo_pal;
    inf->fb0->screen_base     = 0;

    memset(par, 0, sizeof(struct win0_par));

	ret = fb_alloc_cmap(&inf->fb0->cmap, 256, 0);
	if (ret < 0)
		goto release_cmap;

	g_fb0_inf = inf->fb0; //add cym@rk 20101027

    /* alloc win1 buf */
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "win1 buf");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get win1 memory \n");
        ret = -ENOENT;
        goto release_win1fb;
    }
    inf->fb0->fix.smem_start = res->start;
    inf->fb0->fix.smem_len = res->end - res->start + 1;
    inf->fb0->screen_base = ioremap(res->start, inf->fb0->fix.smem_len);
    memset(inf->fb0->screen_base, 0, inf->fb0->fix.smem_len);

    #ifdef CONFIG_FB_WORK_IPP
       /* alloc win1 ipp buf */
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "win1 ipp buf");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get win1 ipp memory \n");
        ret = -ENOENT;
        goto release_win1fb;
    }
    inf->fb0->fix.mmio_start = res->start;
    inf->fb0->fix.mmio_len = res->end - res->start + 1;
    #endif

    /* Prepare win0 info */
    fbprintk(">> Prepare win0 info \n");
    inf->fb1 = framebuffer_alloc(sizeof(struct win0_par), &pdev->dev);
    if(!inf->fb1)
    {
        dev_err(&pdev->dev, ">> fb1 framebuffer_alloc fail!");
		inf->fb1 = NULL;
		ret = -ENOMEM;
		goto release_win0fb;
    }

    par = (struct win0_par*)inf->fb1->par;

    strcpy(inf->fb1->fix.id, "fb1");
	inf->fb1->fix.type	      = FB_TYPE_PACKED_PIXELS;
	inf->fb1->fix.type_aux    = 0;
	inf->fb1->fix.xpanstep    = 1;
	inf->fb1->fix.ypanstep    = 1;
	inf->fb1->fix.ywrapstep   = 0;
	inf->fb1->fix.accel       = FB_ACCEL_NONE;
    inf->fb1->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->fb1->fix.smem_len    = 0;
    inf->fb1->fix.line_length = 0;
    inf->fb1->fix.smem_start  = 0;

    inf->fb1->var.xres = screen->x_res;
    inf->fb1->var.yres = screen->y_res;
    inf->fb1->var.bits_per_pixel = 16;
    inf->fb1->var.xres_virtual = screen->x_res;
    inf->fb1->var.yres_virtual = screen->y_res;
    inf->fb1->var.width = screen->width;
    inf->fb1->var.height = screen->height;
    //inf->fb1->var.pixclock = div_u64(1000000000000llu, screen->pixclock);
    inf->fb1->var.left_margin = screen->left_margin;
    inf->fb1->var.right_margin = screen->right_margin;
    inf->fb1->var.upper_margin = screen->upper_margin;
    inf->fb1->var.lower_margin = screen->lower_margin;
    inf->fb1->var.vsync_len = screen->vsync_len;
    inf->fb1->var.hsync_len = screen->hsync_len;
    inf->fb1->var.red    = def_rgb_16.red;
    inf->fb1->var.green  = def_rgb_16.green;
    inf->fb1->var.blue   = def_rgb_16.blue;
    inf->fb1->var.transp = def_rgb_16.transp;

    inf->fb1->var.nonstd      = 0;  //win0 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->fb1->var.grayscale   = ((inf->fb1->var.yres<<20)&0xfff00000) + ((inf->fb1->var.xres<<8)&0xfff00);//win0 xsize & ysize
    inf->fb1->var.activate    = FB_ACTIVATE_NOW;
    inf->fb1->var.accel_flags = 0;
    inf->fb1->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->fb1->fbops           = &fb1_ops;
	inf->fb1->flags		      = FBINFO_FLAG_DEFAULT;
	inf->fb1->pseudo_palette  = par->pseudo_pal;
	inf->fb1->screen_base     = 0;

    memset(par, 0, sizeof(struct win0_par));

	init_waitqueue_head(&par->wait);

 	/* Init all lcdc and lcd before register_framebuffer. */
 	/* because after register_framebuffer, the win1fb_check_par and winfb_set_par execute immediately */
 	fbprintk(">> Init all lcdc and lcd before register_framebuffer \n");
    init_lcdc(inf->fb0);

  #if 0 //def CONFIG_CPU_FREQ
   // inf->freq_transition.notifier_call = rk29fb_freq_transition;
   // cpufreq_register_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
  #endif
	fbprintk("got clock\n");

	if(mach_info)
    {
        struct rk29_fb_setting_info fb_setting;
        if( OUT_P888==inf->cur_screen->face )     // set lcdc iomux
        {
            fb_setting.data_num = 24;
        }
        else if(OUT_P666 == inf->cur_screen->face )
        {
            fb_setting.data_num = 18;
        }
        else
        {
            fb_setting.data_num = 16;
        }
        fb_setting.den_en = 1;
        fb_setting.vsync_en = 1;
        fb_setting.disp_on_en = 1;
        fb_setting.standby_en = 1;
        if( inf->cur_screen->mcu_usefmk )
            fb_setting.mcu_fmk_en =1;
        mach_info->io_init(&fb_setting);
    }

	//set_lcd_pin(pdev, 1);
	mdelay(10);
	g_pdev = pdev;
	inf->mcu_usetimer = 1;
    inf->mcu_fmksync = 0;
	load_screen(inf->fb0, 1);

    /* Register framebuffer(fb0 & fb1) */
    fbprintk(">> Register framebuffer(fb0) \n");
    ret = register_framebuffer(inf->fb0);
    if(ret<0)
    {
        printk(">> fb0 register_framebuffer fail!\n");
        ret = -EINVAL;
		goto release_win0fb;
    }
    fbprintk(">> Register framebuffer(fb1) \n");

    ret = register_framebuffer(inf->fb1);
    if(ret<0)
    {
        printk(">> fb1 register_framebuffer fail!\n");
        ret = -EINVAL;
		goto unregister_win1fb;
    }

#ifdef CONFIG_FB_DSP_LUT
	rk29_fb_lut_config(1);
    ret = device_create_file(inf->fb0->dev, &rk29fb_attrs[0]);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
    }
	ret = device_create_file(inf->fb1->dev, &rk29fb_attrs[0]);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
    }
#endif

    ret = device_create_file(inf->fb1->dev, &dev_attr_dsp_win0_info);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
    }

    ret = device_create_file(inf->fb1->dev, &dev_attr_dsp_win0_scale);
    if(ret)
    {
        printk(">> fb1 dsp win0 scale device_create_file err\n");
        ret = -EINVAL;
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
	suspend_info.inf = inf;
	register_early_suspend(&suspend_info.early_suspend);
#endif

    /* get and request irq */
    fbprintk(">> get and request irq \n");
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        dev_err(&pdev->dev, "no irq for device\n");
        ret = -ENOENT;
        goto unregister_win1fb;
    }
    ret = request_irq(irq, rk29fb_irq, IRQF_DISABLED, pdev->name, pdev);
    if (ret) {
        dev_err(&pdev->dev, "cannot get irq %d - err %d\n", irq, ret);
        ret = -EBUSY;
        goto release_irq;
    }

    if( inf->cur_screen->mcu_usefmk && (mach_info->mcu_fmk_pin != -1) )
    {
        ret = request_irq(gpio_to_irq(mach_info->mcu_fmk_pin), mcu_irqfmk, GPIOEdgelFalling, pdev->name, pdev);
        if (ret)
        {
            dev_err(&pdev->dev, "cannot get fmk irq %d - err %d\n", irq, ret);
            ret = -EBUSY;
            goto release_irq;
        }
    }

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
    fb0_set_par(inf->fb0);
    if (fb_prepare_logo(inf->fb0, FB_ROTATE_UR)) {
        /* Start display and show logo on boot */
        fb_set_cmap(&inf->fb0->cmap, inf->fb0);
        fb_show_logo(inf->fb0, FB_ROTATE_UR);
        fb0_blank(FB_BLANK_UNBLANK, inf->fb0);
    }
#endif

	#ifdef CONFIG_DISPLAY_LCD_SUPPORT
	lcd_register_display_sysfs(pdev);
	#endif

    printk(" %s ok\n", __FUNCTION__);
    return ret;

release_irq:
	if(irq>=0)
    	free_irq(irq, pdev);
unregister_win1fb:
    unregister_framebuffer(inf->fb0);
release_win0fb:
	if(inf->fb1)
		framebuffer_release(inf->fb1);
	inf->fb1 = NULL;
release_cmap:
    if(&inf->fb0->cmap)
        fb_dealloc_cmap(&inf->fb0->cmap);
release_win1fb:
	if(inf->fb0)
		framebuffer_release(inf->fb0);
	inf->fb0 = NULL;
release_drvdata:
	if(inf && inf->reg_vir_base)
    	iounmap(inf->reg_vir_base);
	if(inf && mem)
    	release_mem_region(inf->reg_phy_base, inf->len);
	if(inf)
    	kfree(inf);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int __devexit rk29fb_remove(struct platform_device *pdev)
{
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct fb_info *info = NULL;
	//pm_message_t msg;
    struct rk29fb_info *mach_info = NULL;
    int irq = 0;

	fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk29_fb_remove fail! \n");
        return -EINVAL;
    }
    device_remove_file(inf->fb1->dev, &dev_attr_dsp_win0_info);

    irq = platform_get_irq(pdev, 0);
    if (irq >0)
    {
    free_irq(irq, pdev);
    }

    mach_info = pdev->dev.platform_data;
    if(mach_info->mcu_fmk_pin)
    {
        free_irq(gpio_to_irq(mach_info->mcu_fmk_pin), pdev);
    }

    if(mach_info->io_disable)  
       mach_info->io_disable();  //close lcd out 

    // blank the lcdc
    if(inf->fb1)
        fb1_blank(FB_BLANK_POWERDOWN, inf->fb1);
    if(inf->fb0)
        fb0_blank(FB_BLANK_POWERDOWN, inf->fb0);

	// suspend the lcdc
	//rk29fb_suspend(pdev, msg);
    // unmap memory and release framebuffer
    if(inf->fb1) {
        info = inf->fb1;
        if (info->screen_base) {
	        //dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->fb1);
        framebuffer_release(inf->fb1);
        inf->fb1 = NULL;
    }
    if(inf->fb0) {
        info = inf->fb0;
        if (info->screen_base) {
	    //    dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->fb0);
        framebuffer_release(inf->fb0);
        inf->fb0 = NULL;
    }

  #if 0 //def CONFIG_CPU_FREQ
   // cpufreq_unregister_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
  #endif

    msleep(100);

	if (inf->clk)
    {
		clk_disable(inf->clk);
		clk_put(inf->clk);
		inf->clk = NULL;
	}
    if (inf->dclk)
    {
		clk_disable(inf->dclk);
		clk_put(inf->dclk);
		inf->dclk = NULL;
	}

    kfree(inf);
    platform_set_drvdata(pdev, NULL);

    return 0;
}

static void rk29fb_shutdown(struct platform_device *pdev)
{
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct rk29fb_info *mach_info = pdev->dev.platform_data;;

	fbprintk("----------------------------rk29fb_shutdown----------------------------\n");

    if(mach_info->io_disable)  
       mach_info->io_disable();  //close lcd out 
       
    if(!inf->in_suspend)
    {
        LcdMskReg(inf, DSP_CTRL1, m_BLANK_MODE , v_BLANK_MODE(1));
        LcdMskReg(inf, SYS_CONFIG, m_STANDBY, v_STANDBY(1));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        mdelay(100);
        clk_disable(inf->aclk_ddr_lcdc);
        clk_disable(inf->aclk_disp_matrix);
        clk_disable(inf->hclk_cpu_display);
        clk_disable(inf->clk);
        if(inf->dclk){
            clk_disable(inf->dclk);
        }
        if(inf->clk){
            clk_disable(inf->aclk);
        }
        clk_disable(inf->pd_display);
        //pmu_set_power_domain(PD_DISPLAY, 0);
		inf->in_suspend = 1;
	}

}

static struct platform_driver rk29fb_driver = {
	.probe		= rk29fb_probe,
	.remove		= __devexit_p(rk29fb_remove),
	.driver		= {
		.name	= "rk29-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk29fb_shutdown,
};

static int __init rk29fb_init(void)
{
	wake_lock_init(&idlelock, WAKE_LOCK_IDLE, "fb");
    return platform_driver_register(&rk29fb_driver);
}

static void __exit rk29fb_exit(void)
{
    platform_driver_unregister(&rk29fb_driver);
}

//fs_initcall(rk29fb_init);
subsys_initcall(rk29fb_init);
module_exit(rk29fb_exit);


MODULE_AUTHOR("  zyw@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk29 fb device");
MODULE_LICENSE("GPL");


