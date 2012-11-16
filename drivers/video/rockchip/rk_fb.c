/*
 * drivers/video/rockchip/rk_fb.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *Author:yzq<yzq@rock-chips.com>
 	yxj<yxj@rock-chips.com>
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
#include <linux/platform_device.h>
#include <linux/earlysuspend.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include<linux/rk_fb.h>
#include <plat/ipp.h>
#include "hdmi/rk_hdmi.h"



#ifdef	CONFIG_FB_MIRRORING


int (*video_data_to_mirroring)(struct fb_info *info,u32 yuv_phy[2]) = NULL;
EXPORT_SYMBOL(video_data_to_mirroring);

#endif
static struct platform_device *g_fb_pdev;

static struct rk_fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
};


/***************************************************************************
fb0-----------lcdc0------------win1  for ui
fb1-----------lcdc0------------win0  for video,win0 support 3d display
fb2-----------lcdc1------------win1  for ui
fb3-----------lcdc1-------------win0 for video ,win0 support 3d display

defautl:we alloc three buffer,one for fb0 and fb2 display ui,one for ipp rotate
        fb1 and fb3 are used for video play,the buffer is alloc by android,and
        pass the phy addr to fix.smem_start by ioctl
****************************************************************************/

int get_fb_layer_id(struct fb_fix_screeninfo *fix)
{
	int layer_id;
	if(!strcmp(fix->id,"fb1")||!strcmp(fix->id,"fb3"))
	{
		layer_id = 0;
	}
	else if(!strcmp(fix->id,"fb0")||!strcmp(fix->id,"fb2"))
	{
		layer_id = 1;
	}
	else
	{
		printk(KERN_ERR "unsupported %s",fix->id);
		layer_id = -ENODEV;
	}

	return layer_id;
}
EXPORT_SYMBOL(get_fb_layer_id);

/**********************************************************************
this is for hdmi
name: lcdc device name ,lcdc0 , lcdc1
***********************************************************************/
struct rk_lcdc_device_driver * rk_get_lcdc_drv(char *name)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	int i = 0;
	for( i = 0; i < inf->num_lcdc; i++)
	{
		if(!strcmp(inf->lcdc_dev_drv[i]->name,name))
			break;
	}
	return inf->lcdc_dev_drv[i];
	
}
static int rk_fb_open(struct fb_info *info,int user)
{
    struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
    int layer_id;
  
    layer_id = get_fb_layer_id(&info->fix);
    if(dev_drv->layer_par[layer_id]->state)
    {
    	return 0;    // if this layer aready opened ,no need to reopen
    }
    else
    {
    	dev_drv->open(dev_drv,layer_id,1);
    }
    
    return 0;
    
}

static int rk_fb_close(struct fb_info *info,int user)
{
	/*struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
    	int layer_id;
    	CHK_SUSPEND(dev_drv);
    	layer_id = get_fb_layer_id(&info->fix);
	if(!dev_drv->layer_par[layer_id]->state)
	{
		return 0;
	}
	else
	{
    		dev_drv->open(dev_drv,layer_id,0);
	}*/
	
    	return 0;
}
static void fb_copy_by_ipp(struct fb_info *dst_info, struct fb_info *src_info,int offset)
{
	struct rk29_ipp_req ipp_req;
 
	memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
	ipp_req.src0.YrgbMst = src_info->fix.smem_start + offset;
	ipp_req.src0.w = src_info->var.xres;
	ipp_req.src0.h = src_info->var.yres;

	ipp_req.dst0.YrgbMst = dst_info->fix.smem_start + offset;
	ipp_req.dst0.w = src_info->var.xres;
	ipp_req.dst0.h = src_info->var.yres;

	ipp_req.src_vir_w = src_info->var.xres_virtual;
	ipp_req.dst_vir_w = src_info->var.xres_virtual;
	ipp_req.timeout = 100;
	ipp_req.flag = IPP_ROT_0;
	ipp_blit_sync(&ipp_req);
	
}
static void hdmi_post_work(struct work_struct *work)
{
	
	struct rk_fb_inf *inf = container_of(to_delayed_work(work), struct rk_fb_inf, delay_work);
	struct rk_lcdc_device_driver * dev_drv = inf->lcdc_dev_drv[1];
	dev_drv->pan_display(dev_drv,1);
	
}

static int rk_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb_inf *inf = dev_get_drvdata(info->device);
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct fb_info * info2 = NULL; 
	struct rk_lcdc_device_driver * dev_drv1  = NULL; 
    	struct layer_par *par = NULL;
	struct layer_par *par2 = NULL;
    	int layer_id = 0;
	u32 xoffset = var->xoffset;		// offset from virtual to visible 
	u32 yoffset = var->yoffset;				
	u32 xvir = var->xres_virtual;
	u8 data_format = var->nonstd&0xff;
	
	layer_id = get_fb_layer_id(fix);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		 par = dev_drv->layer_par[layer_id];
	}
	switch (par->format)
    	{
		case ARGB888:
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case  RGB888:
			par->y_offset = (yoffset*xvir + xoffset)*3;
			break;
		case RGB565:
			par->y_offset = (yoffset*xvir + xoffset)*2;
	            	break;
		case  YUV422:
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = par->y_offset;
	            	break;
		case  YUV420:
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = (yoffset>>1)*xvir + xoffset;
	            	break;
		case  YUV444 : // yuv444
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = yoffset*2*xvir +(xoffset<<1);
			break;
		default:
			printk("un supported format:0x%x\n",data_format);
            		return -EINVAL;
    	}

	#if defined(CONFIG_HDMI_RK30)
		#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
			if(hdmi_get_hotplug() == HDMI_HPD_ACTIVED)
			{
				if(inf->num_fb >= 2)
				{
					info2 = inf->fb[2];
					dev_drv1 = (struct rk_lcdc_device_driver * )info2->par;
					par2 = dev_drv1->layer_par[layer_id];
					par2->y_offset = par->y_offset;
					//memcpy(info2->screen_base+par2->y_offset,info->screen_base+par->y_offset,
					//	var->xres*var->yres*var->bits_per_pixel>>3);
					#if !defined(CONFIG_THREE_FB_BUFFER)
					fb_copy_by_ipp(info2,info,par->y_offset);
					#endif
					dev_drv1->pan_display(dev_drv1,layer_id);
					//queue_delayed_work(inf->workqueue, &inf->delay_work,0);
				}
			}
		#endif
	#endif
	dev_drv->pan_display(dev_drv,layer_id);
	#ifdef	CONFIG_FB_MIRRORING
	if(video_data_to_mirroring!=NULL)
		video_data_to_mirroring(info,NULL);
 	#endif
	return 0;
}
static int rk_fb_ioctl(struct fb_info *info, unsigned int cmd,unsigned long arg)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_device_driver *dev_drv = (struct rk_lcdc_device_driver * )info->par;
	u32 yuv_phy[2];
	int layer_id = get_fb_layer_id(&info->fix);
	int enable; // enable fb:1 enable;0 disable 
	int ovl;	//overlay:0 win1 on the top of win0;1,win0 on the top of win1
	int num_buf; //buffer_number
	void __user *argp = (void __user *)arg;

	#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
	struct rk_fb_inf *inf = dev_get_drvdata(info->device);
	struct fb_info * info2 = inf->fb[2];
	struct rk_lcdc_device_driver * dev_drv1  = (struct rk_lcdc_device_driver * )info2->par;
	#endif
	
	switch(cmd)
	{
 		case FBIOPUT_FBPHYADD:
			return info->fix.smem_start;
		case FBIOSET_YUV_ADDR:   //when in video mode, buff alloc by android
			if((!strcmp(fix->id,"fb1"))||(!strcmp(fix->id,"fb3")))
			{
				if (copy_from_user(yuv_phy, argp, 8))
					return -EFAULT;
				info->fix.smem_start = yuv_phy[0];  //four y
				info->fix.mmio_start = yuv_phy[1];  //four uv
			}
			break;
		case FBIOSET_ENABLE:
			if (copy_from_user(&enable, argp, sizeof(enable)))
				return -EFAULT;
			dev_drv->open(dev_drv,layer_id,enable);
			break;
		case FBIOGET_ENABLE:
			enable = dev_drv->get_layer_state(dev_drv,layer_id);
			if(copy_to_user(argp,&enable,sizeof(enable)))
				return -EFAULT;
			break;
		case FBIOSET_OVERLAY_STATE:
			if (copy_from_user(&ovl, argp, sizeof(ovl)))
				return -EFAULT;
			dev_drv->ovl_mgr(dev_drv,ovl,1);
			break;
		case FBIOGET_OVERLAY_STATE:
			ovl = dev_drv->ovl_mgr(dev_drv,0,0);
			if (copy_to_user(argp, &ovl, sizeof(ovl)))
				return -EFAULT;
			break;
		case FBIOPUT_NUM_BUFFERS:
			if (copy_from_user(&num_buf, argp, sizeof(num_buf)))
				return -EFAULT;
			dev_drv->num_buf = num_buf;
			#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
			dev_drv1->num_buf = num_buf;
			#endif
			break;
		case FBIOGET_SCREEN_STATE:
		case FBIOPUT_SET_CURSOR_EN:
		case FBIOPUT_SET_CURSOR_POS:
		case FBIOPUT_SET_CURSOR_IMG:
		case FBIOPUT_SET_CURSOR_CMAP:
		case FBIOPUT_GET_CURSOR_RESOLUTION:
		case FBIOPUT_GET_CURSOR_EN:
		case FB0_IOCTL_STOP_TIMER_FLUSH:    //stop timer flush mcu panel after android is runing
		case FBIOPUT_16OR32:
		case FBIOGET_16OR32:
		case FBIOGET_IDLEFBUff_16OR32:
		case FBIOSET_COMPOSE_LAYER_COUNTS:
        	default:
			dev_drv->ioctl(dev_drv,cmd,arg,layer_id);
			#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
			dev_drv1->ioctl(dev_drv1,cmd,arg,layer_id);
			#endif
            break;
    }
    return 0;
}

static int rk_fb_blank(int blank_mode, struct fb_info *info)
{
    	struct rk_lcdc_device_driver *dev_drv = (struct rk_lcdc_device_driver * )info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	int layer_id;
	
	layer_id = get_fb_layer_id(fix);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	
    	dev_drv->blank(dev_drv,layer_id,blank_mode);

    	return 0;
}

static int rk_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	
	if( 0==var->xres_virtual || 0==var->yres_virtual ||
		 0==var->xres || 0==var->yres || var->xres<16 ||
		 ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
	 {
		 printk("%s check var fail 1!!! \n",info->fix.id);
		 printk("xres_vir:%d>>yres_vir:%d\n", var->xres_virtual,var->yres_virtual);
		 printk("xres:%d>>yres:%d\n", var->xres,var->yres);
		 printk("bits_per_pixel:%d \n", var->bits_per_pixel);
		 return -EINVAL;
	 }
 
	 if( ((var->xoffset+var->xres) > info->var.xres_virtual) ||
	     ((var->yoffset+var->yres) > (info->var.yres_virtual*2)) )
	 {
		 printk("%s check_var fail 2!!! \n",info->fix.id);
		 printk("xoffset:%d>>xres:%d>>xres_vir:%d\n",var->xoffset,var->xres,info->var.xres_virtual);
		 printk("yoffset:%d>>yres:%d>>yres_vir:%d\n",var->yoffset,var->yres,info->var.yres_virtual);
		 return -EINVAL;
	 }

    return 0;
}


static int rk_fb_set_par(struct fb_info *info)
{
	struct rk_fb_inf *inf = dev_get_drvdata(info->device);
    	struct fb_var_screeninfo *var = &info->var;
    	struct fb_fix_screeninfo *fix = &info->fix;
    	struct rk_lcdc_device_driver * dev_drv = (struct rk_lcdc_device_driver * )info->par;
    	struct layer_par *par = NULL;
   	rk_screen *screen =dev_drv->screen;
	struct fb_info * info2 = NULL;
	struct rk_lcdc_device_driver * dev_drv1  = NULL;
	struct layer_par *par2 = NULL;
    	int layer_id = 0;	
    	u32 cblen = 0,crlen = 0;
    	u16 xsize =0,ysize = 0;              //winx display window height/width --->LCDC_WINx_DSP_INFO
    	u32 xoffset = var->xoffset;		// offset from virtual to visible 
	u32 yoffset = var->yoffset;		//resolution			
	u16 xpos = (var->nonstd>>8) & 0xfff; //visiable pos in panel
	u16 ypos = (var->nonstd>>20) & 0xfff;
	u32 xvir = var->xres_virtual;
	u32 yvir = var->yres_virtual;
	u8 data_format = var->nonstd&0xff;
//	var->pixclock = dev_drv->pixclock;
	#if defined(CONFIG_HDMI_RK30)
		#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
			if(hdmi_get_hotplug() == HDMI_HPD_ACTIVED)
			{
				if(inf->num_fb >= 2)
				{
					info2 = inf->fb[2];
					dev_drv1 = (struct rk_lcdc_device_driver * )info2->par;
				}
			}
		#endif 
	#endif
	layer_id = get_fb_layer_id(fix);
	if(layer_id < 0)
	{
		return  -ENODEV;
	}
	else
	{
		par = dev_drv->layer_par[layer_id];
		if(dev_drv1)
		{
			par2 = dev_drv1->layer_par[layer_id];
		}
	}
	
	if(var->grayscale>>8)
	{
		xsize = (var->grayscale>>8) & 0xfff;  //visiable size in panel ,for vide0
		ysize = (var->grayscale>>20) & 0xfff;
	}
	else
	{
		xsize = screen->x_res;
              	ysize = screen->y_res;
	}
		
	/* calculate y_offset,c_offset,line_length,cblen and crlen  */
#if 1
	switch (data_format)
	{
		case HAL_PIXEL_FORMAT_RGBA_8888 :      // rgb
		case HAL_PIXEL_FORMAT_RGBX_8888: 
			par->format = ARGB888;
			fix->line_length = 4 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case HAL_PIXEL_FORMAT_RGB_888 :
			par->format = RGB888;
			fix->line_length = 3 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*3;
			break;
		case HAL_PIXEL_FORMAT_RGB_565:  //RGB565
			par->format = RGB565;
			fix->line_length = 2 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*2;
		    	break;
		case HAL_PIXEL_FORMAT_YCbCr_422_SP : // yuv422
			par->format = YUV422;
			fix->line_length = xvir;
			cblen = crlen = (xvir*yvir)>>1;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = par->y_offset;
		    	break;
		case HAL_PIXEL_FORMAT_YCrCb_NV12   : // YUV420---uvuvuv
			par->format = YUV420;
			fix->line_length = xvir;
			cblen = crlen = (xvir*yvir)>>2;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = (yoffset>>1)*xvir + xoffset;
		    	break;
		case HAL_PIXEL_FORMAT_YCrCb_444 : // yuv444
			par->format = 5;
			fix->line_length = xvir<<2;
			par->y_offset = yoffset*xvir + xoffset;
			par->c_offset = yoffset*2*xvir +(xoffset<<1);
			cblen = crlen = (xvir*yvir);
			break;
		default:
			printk("%s:un supported format:0x%x\n",__func__,data_format);
		    return -EINVAL;
	}
#else
	switch(var->bits_per_pixel)
	{
		case 32:
			par->format = ARGB888;
			fix->line_length = 4 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*4;
			break;
		case 16:
			par->format = RGB565;
			fix->line_length = 2 * xvir;
			par->y_offset = (yoffset*xvir + xoffset)*2;
	    		break;
			
	}
#endif

	par->xpos = xpos;
	par->ypos = ypos;
	par->xsize = xsize;
	par->ysize = ysize;

	par->smem_start =fix->smem_start;
	par->cbr_start = fix->mmio_start;
	par->xact = var->xres;              //winx active window height,is a part of vir
	par->yact = var->yres;
	par->xvir =  var->xres_virtual;		// virtual resolution	 stride --->LCDC_WINx_VIR
	par->yvir =  var->yres_virtual;

	#if defined(CONFIG_HDMI_RK30)
		#if defined(CONFIG_DUAL_DISP_IN_KERNEL)
			if(hdmi_get_hotplug() == HDMI_HPD_ACTIVED)
			{
				if(info != info2)
				{
					par2->xact = par->xact;
					par2->yact = par->yact;
					par2->format = par->format;
					if(par2->xsize == 0)
						par2->xsize = par->xsize;
					if(par2->ysize == 0)
						par2->ysize = par->ysize;
					if(par2->xvir == 0)
						par2->xvir = par->xvir;
					if(par2->yvir == 0)
						par2->yvir = par->yvir;
					dev_drv1->set_par(dev_drv1,layer_id);
				}
				//$_rbox_$_modify_$ zhy modified for box display system
				else
				{
					struct fb_info * info0 = inf->fb[0]; 
					struct rk_lcdc_device_driver * dev_drv0  = (struct rk_lcdc_device_driver * )info0->par;
					struct layer_par *par0 = dev_drv0->layer_par[layer_id];
					par->format = par0->format;
					par->xact = par0->xact;
					par->yact = par0->yact;
				}
				//$_rbox_$_modify_end
			}
		#endif
	#endif
	dev_drv->set_par(dev_drv,layer_id);

    
	return 0;
}

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

static struct fb_ops fb_ops = {
    .owner          = THIS_MODULE,
    .fb_open        = rk_fb_open,
    .fb_release     = rk_fb_close,
    .fb_check_var   = rk_fb_check_var,
    .fb_set_par     = rk_fb_set_par,
    .fb_blank       = rk_fb_blank,
    .fb_ioctl       = rk_fb_ioctl,
    .fb_pan_display = rk_pan_display,
    .fb_setcolreg   = fb_setcolreg,
    .fb_fillrect    = cfb_fillrect,
    .fb_copyarea    = cfb_copyarea,
    .fb_imageblit   = cfb_imageblit,
};



static struct fb_var_screeninfo def_var = {
    .red    = {11,5,0},//default set to rgb565,the boot logo is rgb565
    .green  = {5,6,0},
    .blue   = {0,5,0},
    .transp = {0,0,0},	
    .nonstd      = HAL_PIXEL_FORMAT_RGB_565,   //(ypos<<20+xpos<<8+format) format
    .grayscale   = 0,  //(ysize<<20+xsize<<8)
    .activate    = FB_ACTIVATE_NOW,
    .accel_flags = 0,
    .vmode       = FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo def_fix = {
	.type		 = FB_TYPE_PACKED_PIXELS,
	.type_aux	 = 0,
	.xpanstep	 = 1,
	.ypanstep	 = 1,
	.ywrapstep	 = 0,
	.accel		 = FB_ACCEL_NONE,
	.visual 	 = FB_VISUAL_TRUECOLOR,
		
};


/*****************************************************************
this two function is for other module that in the kernel which
need show image directly through fb
fb_id:we have 4 fb here,default we use fb0 for ui display
*******************************************************************/
struct fb_info * rk_get_fb(int fb_id)
{
    struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
    struct fb_info *fb = inf->fb[fb_id];
    return fb;
}
EXPORT_SYMBOL(rk_get_fb);

void rk_direct_fb_show(struct fb_info * fbi)
{
    rk_fb_set_par(fbi);
    rk_pan_display(&fbi->var, fbi);
}
EXPORT_SYMBOL(rk_direct_fb_show);

//$_rbox_$_modify_$ zhengyang_20120618
int rk_fb_switch_screen(rk_screen *screen ,int enable ,int lcdc_id)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	struct fb_info *info = NULL;
	struct rk_lcdc_device_driver * dev_drv = NULL;
	struct fb_var_screeninfo *var = NULL;
	struct fb_fix_screeninfo *fix = NULL;
	char name[6];
	int ret, i, layer_id;
	u16 xpos, ypos, xsize, ysize;
	
	sprintf(name, "lcdc%d",lcdc_id);
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if(!strcmp(inf->lcdc_dev_drv[i]->name,name))
		{
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	if(i == inf->num_lcdc)
	{
		printk(KERN_ERR "%s driver not found!",name);
		return -ENODEV;
		
	}
	//$_rbox_$_modify_$ zhengyang_20120618
	if(enable)
		memcpy(dev_drv->screen, screen, sizeof(rk_screen));
	//$_rbox_$_modify_$ end
	
	if((lcdc_id == 0) || (inf->num_lcdc == 1))
	{
		info = inf->fb[0];
	}
	else if((lcdc_id == 1)&&(inf->num_lcdc == 2))
	{
		info = inf->fb[2];
	}

	layer_id = get_fb_layer_id(&info->fix);
	if(!enable)
	{
		if(dev_drv->layer_par[layer_id]->state) 
		{
			dev_drv->open(dev_drv,layer_id,enable); //disable the layer which attached to this fb
		}
		return 0;
	}
	
	xpos = (dev_drv->screen->x_res - dev_drv->screen->x_res*dev_drv->x_scale/100)>>1;
	ypos = (dev_drv->screen->y_res - dev_drv->screen->y_res*dev_drv->y_scale/100)>>1;
	xsize = dev_drv->screen->x_res * dev_drv->x_scale/100;
	ysize = dev_drv->screen->y_res * dev_drv->y_scale/100;
	
	var = &info->var;
	var->nonstd &= 0xff;
	var->nonstd |= (xpos << 8) + (ypos << 20);
	var->grayscale &= 0xff;
	var->grayscale |= (xsize << 8) + (ysize << 20);
	ret = dev_drv->load_screen(dev_drv,1);
	if(dev_drv->layer_par[layer_id]->state != enable)
		ret = info->fbops->fb_open(info,1);
	ret = info->fbops->fb_set_par(info);
	ret = info->fbops->fb_pan_display(var, info);
	return 0;

}
//$_rbox_$_modify_$ end

int rk_fb_disp_scale(u8 scale_x, u8 scale_y,u8 lcdc_id)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	struct fb_info *info = NULL;
	struct fb_var_screeninfo *var = NULL;
	struct rk_lcdc_device_driver * dev_drv = NULL;
	u16 screen_x,screen_y;
	u16 xpos,ypos;
	u16 xsize,ysize;
	
	char name[6];
	int i;
	sprintf(name, "lcdc%d",lcdc_id);
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if(!strcmp(inf->lcdc_dev_drv[i]->name,name))
		{
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	if(i == inf->num_lcdc)
	{
		printk(KERN_ERR "%s driver not found!",name);
		return -ENODEV;
		
	}

	if((lcdc_id == 0) || (inf->num_lcdc == 1))
	{
		info = inf->fb[0];
	}
	else if( (inf->num_lcdc == 2)&&(lcdc_id == 1))
	{
		info = inf->fb[2];
	}

	var = &info->var;
	screen_x = dev_drv->screen->x_res;
	screen_y = dev_drv->screen->y_res;
	xpos = (screen_x-screen_x*scale_x/100)>>1;
	ypos = (screen_y-screen_y*scale_y/100)>>1;
	xsize = screen_x*scale_x/100;
	ysize = screen_y*scale_y/100;
	var->nonstd &= 0xff;
	var->nonstd |= (xpos<<8) + (ypos<<20);
	var->grayscale &= 0xff;
	var->grayscale |= (xsize<<8) + (ysize<<20);

	info->fbops->fb_set_par(info);
	return 0;
	
	
}

static int rk_request_fb_buffer(struct fb_info *fbi,int fb_id)
{
	struct resource *res;
	struct resource *mem;
	int ret = 0;
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	switch(fb_id)
	{
        	case 0:
            		res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb0 buf");
            		if (res == NULL)
            		{
                		dev_err(&g_fb_pdev->dev, "failed to get win0 memory \n");
                		ret = -ENOENT;
            		}
		 	fbi->fix.smem_start = res->start;
	            	fbi->fix.smem_len = res->end - res->start + 1;
		    	mem = request_mem_region(res->start, resource_size(res), g_fb_pdev->name);
	            	fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
	            	memset(fbi->screen_base, 0, fbi->fix.smem_len);
		    	printk("fb%d:phy:%lx>>vir:%p>>len:0x%x\n",fb_id,
				fbi->fix.smem_start,fbi->screen_base,fbi->fix.smem_len);
        	#ifdef CONFIG_FB_WORK_IPP // alloc ipp buf for rotate
	            	res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "ipp buf");
	            	if (res == NULL)
	            	{
	                	dev_err(&g_fb_pdev->dev, "failed to get win1 ipp memory \n");
	               		ret = -ENOENT;
	            	}
	            	fbi->fix.mmio_start = res->start;
	            	fbi->fix.mmio_len = res->end - res->start + 1;
	 	#endif
		    	break;
        	case 2:
			#if !defined(CONFIG_THREE_FB_BUFFER)
            		res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb2 buf");
			if (res == NULL)
			{
			dev_err(&g_fb_pdev->dev, "failed to get win0 memory \n");
			ret = -ENOENT;
			}
			fbi->fix.smem_start = res->start;
			fbi->fix.smem_len = res->end - res->start + 1;
			mem = request_mem_region(res->start, resource_size(res), g_fb_pdev->name);
			fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
			memset(fbi->screen_base, 0, fbi->fix.smem_len);
			#else    //three buffer no need to copy
			fbi->fix.smem_start = fb_inf->fb[0]->fix.smem_start;
			fbi->fix.smem_len   = fb_inf->fb[0]->fix.smem_len;
			fbi->screen_base    = fb_inf->fb[0]->screen_base;
			#endif
			printk("fb%d:phy:%lx>>vir:%p>>len:0x%x\n",fb_id,
				fbi->fix.smem_start,fbi->screen_base,fbi->fix.smem_len);
			break;
        	default:
            		ret = -EINVAL;
            		break;		
	}
    return ret;
}

static int rk_release_fb_buffer(struct fb_info *fbi)
{
	if(!fbi)
	{
		printk("no need release null fb buffer!\n");
		return -EINVAL;
	}
	if(!strcmp(fbi->fix.id,"fb1")||!strcmp(fbi->fix.id,"fb3"))  //buffer for fb1 and fb3 are alloc by android
		return 0;
	iounmap(fbi->screen_base);
	release_mem_region(fbi->fix.smem_start,fbi->fix.smem_len);
	return 0;
	
}
static int init_layer_par(struct rk_lcdc_device_driver *dev_drv)
{
       int i;
       struct layer_par * def_par = NULL;
       int num_par = dev_drv->num_layer;
       for(i = 0; i < num_par; i++)
       {
               struct layer_par *par = NULL;
               par =  kzalloc(sizeof(struct layer_par), GFP_KERNEL);
               if(!par)
               {
                       printk(KERN_ERR "kzmalloc for layer_par fail!");
                       return   -ENOMEM;
                       
               }
	       def_par = &dev_drv->def_layer_par[i];
               strcpy(par->name,def_par->name);
               par->id = def_par->id;
               par->support_3d = def_par->support_3d;
               dev_drv->layer_par[i] = par;
       }
               
       return 0;
       
       
}


static int init_lcdc_device_driver(struct rk_lcdc_device_driver *dev_drv,
	struct rk_lcdc_device_driver *def_drv,int id)
{
	if(!def_drv)
	{
		printk(KERN_ERR "default lcdc device driver is null!\n");
		return -EINVAL;
	}
	if(!dev_drv)
	{
		printk(KERN_ERR "lcdc device driver is null!\n");
		return -EINVAL;	
	}
	sprintf(dev_drv->name, "lcdc%d",id);
	dev_drv->id		= id;
	dev_drv->open      	= def_drv->open;
	dev_drv->init_lcdc 	= def_drv->init_lcdc;
	dev_drv->ioctl 		= def_drv->ioctl;
	dev_drv->blank 		= def_drv->blank;
	dev_drv->set_par 	= def_drv->set_par;
	dev_drv->pan_display 	= def_drv->pan_display;
	dev_drv->suspend 	= def_drv->suspend;
	dev_drv->resume 	= def_drv->resume;
	dev_drv->load_screen 	= def_drv->load_screen;
	dev_drv->def_layer_par 	= def_drv->def_layer_par;
	dev_drv->num_layer	= def_drv->num_layer;
	dev_drv->get_layer_state= def_drv->get_layer_state;
	dev_drv->get_disp_info  = def_drv->get_disp_info;
	dev_drv->ovl_mgr	= def_drv->ovl_mgr;
	dev_drv->fps_mgr	= def_drv->fps_mgr;
	//$_rbox_$_modify_$ zhy added for lut modify
	if(def_drv->set_dsp_lut)
		dev_drv->set_dsp_lut    = def_drv->set_dsp_lut;
	if(def_drv->read_dsp_lut)
		dev_drv->read_dsp_lut   = def_drv->read_dsp_lut;
	dev_drv->x_scale = 100;
	dev_drv->y_scale = 100;
	//$_rbox_$_modify_end
	init_layer_par(dev_drv);
	init_completion(&dev_drv->frame_done);
	spin_lock_init(&dev_drv->cpl_lock);
	dev_drv->first_frame = 1;
	
	return 0;
}

int rk_fb_register(struct rk_lcdc_device_driver *dev_drv,
	struct rk_lcdc_device_driver *def_drv,int id)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi;
	int i=0,ret = 0;
	int lcdc_id = 0;
	if(NULL == dev_drv)
	{
        	printk("null lcdc device driver?");
        	return -ENOENT;
    	}
    	for(i=0;i<RK30_MAX_LCDC_SUPPORT;i++)
	{
        	if(NULL==fb_inf->lcdc_dev_drv[i])
		{
            		fb_inf->lcdc_dev_drv[i] = dev_drv;
            		fb_inf->lcdc_dev_drv[i]->id = id;
            		fb_inf->num_lcdc++;
            		break;
        	}
    	}
    	if(i==RK30_MAX_LCDC_SUPPORT)
	{
        	printk("rk_fb_register lcdc out of support %d",i);
        	return -ENOENT;
    	}
    	lcdc_id = i;
	init_lcdc_device_driver(dev_drv, def_drv,id);
	if(dev_drv->screen_ctr_info->set_screen_info)
	{
		dev_drv->screen_ctr_info->set_screen_info(dev_drv->screen,
			dev_drv->screen_ctr_info->lcd_info);
		if(SCREEN_NULL==dev_drv->screen->type)
		{
			printk(KERN_WARNING "no display device on lcdc%d!?\n",dev_drv->id);
			fb_inf->num_lcdc--;
			return -ENODEV;
		}
		if(dev_drv->screen_ctr_info->io_init)
			dev_drv->screen_ctr_info->io_init(NULL);
	}
	else
	{
		printk(KERN_WARNING "no display device on lcdc%d!?\n",dev_drv->id);
		fb_inf->num_lcdc--;
		return -ENODEV;
	}
		
	dev_drv->init_lcdc(dev_drv);
	dev_drv->load_screen(dev_drv,1);
	/************fb set,one layer one fb ***********/
	dev_drv->fb_index_base = fb_inf->num_fb;
    for(i=0;i<dev_drv->num_layer;i++)
    {
        fbi= framebuffer_alloc(0, &g_fb_pdev->dev);
        if(!fbi)
        {
            dev_err(&g_fb_pdev->dev,">> fb framebuffer_alloc fail!");
            fbi = NULL;
            ret = -ENOMEM;
        }
	fbi->par = dev_drv;
        fbi->var = def_var;
        fbi->fix = def_fix;
        sprintf(fbi->fix.id,"fb%d",fb_inf->num_fb);
        fbi->var.xres = fb_inf->lcdc_dev_drv[lcdc_id]->screen->x_res;
        fbi->var.yres = fb_inf->lcdc_dev_drv[lcdc_id]->screen->y_res;
	fbi->var.grayscale |= (fbi->var.xres<<8) + (fbi->var.yres<<20);
        fbi->var.bits_per_pixel = 16;
        fbi->var.xres_virtual = fb_inf->lcdc_dev_drv[lcdc_id]->screen->x_res;
        fbi->var.yres_virtual = fb_inf->lcdc_dev_drv[lcdc_id]->screen->y_res;
        fbi->var.width = fb_inf->lcdc_dev_drv[lcdc_id]->screen->width;
        fbi->var.height = fb_inf->lcdc_dev_drv[lcdc_id]->screen->height;
        fbi->var.pixclock = fb_inf->lcdc_dev_drv[lcdc_id]->pixclock;
        fbi->var.left_margin = fb_inf->lcdc_dev_drv[lcdc_id]->screen->left_margin;
        fbi->var.right_margin = fb_inf->lcdc_dev_drv[lcdc_id]->screen->right_margin;
        fbi->var.upper_margin = fb_inf->lcdc_dev_drv[lcdc_id]->screen->upper_margin;
        fbi->var.lower_margin = fb_inf->lcdc_dev_drv[lcdc_id]->screen->lower_margin;
        fbi->var.vsync_len = fb_inf->lcdc_dev_drv[lcdc_id]->screen->vsync_len;
        fbi->var.hsync_len = fb_inf->lcdc_dev_drv[lcdc_id]->screen->hsync_len;
        fbi->fbops			 = &fb_ops;
        fbi->flags			 = FBINFO_FLAG_DEFAULT;
        fbi->pseudo_palette  = fb_inf->lcdc_dev_drv[lcdc_id]->layer_par[i]->pseudo_pal;
        rk_request_fb_buffer(fbi,fb_inf->num_fb);
        ret = register_framebuffer(fbi);
        if(ret<0)
        {
            printk("%s>>fb%d register_framebuffer fail!\n",__func__,fb_inf->num_fb);
            ret = -EINVAL;
        }
	rkfb_create_sysfs(fbi);
        fb_inf->fb[fb_inf->num_fb] = fbi;
        printk("%s>>>>>%s\n",__func__,fb_inf->fb[fb_inf->num_fb]->fix.id);
        fb_inf->num_fb++;	
	}
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)

    if(id == 0)
    {
	    fb_inf->fb[fb_inf->num_fb-2]->fbops->fb_open(fb_inf->fb[fb_inf->num_fb-2],1);
	    fb_inf->fb[fb_inf->num_fb-2]->fbops->fb_set_par(fb_inf->fb[fb_inf->num_fb-2]);
	    if(fb_prepare_logo(fb_inf->fb[fb_inf->num_fb-2], FB_ROTATE_UR)) {
	        /* Start display and show logo on boot */
	        fb_set_cmap(&fb_inf->fb[fb_inf->num_fb-2]->cmap, fb_inf->fb[fb_inf->num_fb-2]);
	        fb_show_logo(fb_inf->fb[fb_inf->num_fb-2], FB_ROTATE_UR);
		fb_inf->fb[fb_inf->num_fb-2]->fbops->fb_pan_display(&(fb_inf->fb[fb_inf->num_fb-2]->var), fb_inf->fb[fb_inf->num_fb-2]);
	    }
    }
#endif
	return 0;
	
	
}
int rk_fb_unregister(struct rk_lcdc_device_driver *dev_drv)
{

	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi;
	int fb_index_base = dev_drv->fb_index_base;
	int fb_num = dev_drv->num_layer;
	int i=0;
	if(NULL == dev_drv)
	{
		printk(" no need to unregister null lcdc device driver!\n");
		return -ENOENT;
	}

	for(i = 0; i < fb_num; i++)
	{
		kfree(dev_drv->layer_par[i]);
	}

	for(i=fb_index_base;i<(fb_index_base+fb_num);i++)
	{
		fbi = fb_inf->fb[i];
		unregister_framebuffer(fbi);
		//rk_release_fb_buffer(fbi);
		framebuffer_release(fbi);	
	}
	fb_inf->lcdc_dev_drv[dev_drv->id]= NULL;
	fb_inf->num_lcdc--;

	return 0;
}



#ifdef CONFIG_HAS_EARLYSUSPEND
struct suspend_info {
	struct early_suspend early_suspend;
	struct rk_fb_inf *inf;
};

static void rkfb_early_suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb_inf *inf = info->inf;
	int i;
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if (!inf->lcdc_dev_drv[i])
			continue;
		if(inf->lcdc_dev_drv[i]->screen_ctr_info->io_disable)
			inf->lcdc_dev_drv[i]->screen_ctr_info->io_disable();
		if(inf->lcdc_dev_drv[i]->screen->standby)
			inf->lcdc_dev_drv[i]->screen->standby(1);
		
		inf->lcdc_dev_drv[i]->suspend(inf->lcdc_dev_drv[i]);
	}
}
static void rkfb_early_resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb_inf *inf = info->inf;
	int i;
	for(i = 0; i < inf->num_lcdc; i++)
	{
		if (!inf->lcdc_dev_drv[i])
			continue;
		if(inf->lcdc_dev_drv[i]->screen_ctr_info->io_enable)
			inf->lcdc_dev_drv[i]->screen_ctr_info->io_enable();
		if(inf->lcdc_dev_drv[i]->screen->standby)
			inf->lcdc_dev_drv[i]->screen->standby(0);
		
		inf->lcdc_dev_drv[i]->resume(inf->lcdc_dev_drv[i]);
	}

}



static struct suspend_info suspend_info = {
	.early_suspend.suspend = rkfb_early_suspend,
	.early_suspend.resume = rkfb_early_resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif

static int __devinit rk_fb_probe (struct platform_device *pdev)
{
	struct rk_fb_inf *fb_inf = NULL;
	int ret = 0;
	g_fb_pdev=pdev;
    	/* Malloc rk_fb_inf and set it to pdev for drvdata */
	fb_inf = kzalloc(sizeof(struct rk_fb_inf), GFP_KERNEL);
	if(!fb_inf)
	{
        	dev_err(&pdev->dev, ">>fb inf kmalloc fail!");
        	ret = -ENOMEM;
    	}
	platform_set_drvdata(pdev,fb_inf);
//#if defined(CONFIG_HDMI_RK30)
//		#if defined(CONFIG_DUAL_DISP_IN_KERNEL)		
//			fb_inf->workqueue = create_singlethread_workqueue("hdmi_post");
//			INIT_DELAYED_WORK(&(fb_inf->delay_work), hdmi_post_work);
//		#endif
//#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	suspend_info.inf = fb_inf;
	register_early_suspend(&suspend_info.early_suspend);
#endif
	printk("rk fb probe ok!\n");
    return 0;
}

static int __devexit rk_fb_remove(struct platform_device *pdev)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(pdev);
	kfree(fb_inf);
    	platform_set_drvdata(pdev, NULL);
    	return 0;
}

static void rk_fb_shutdown(struct platform_device *pdev)
{
//	struct rk_fb_inf *fb_inf = platform_get_drvdata(pdev);
//	kfree(fb_inf);
//	platform_set_drvdata(pdev, NULL);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&suspend_info.early_suspend);
#endif
}

static struct platform_driver rk_fb_driver = {
	.probe		= rk_fb_probe,
	.remove		= __devexit_p(rk_fb_remove),
	.driver		= {
		.name	= "rk-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk_fb_shutdown,
};

static int __init rk_fb_init(void)
{
    return platform_driver_register(&rk_fb_driver);
}

static void __exit rk_fb_exit(void)
{
    platform_driver_unregister(&rk_fb_driver);
}

subsys_initcall_sync(rk_fb_init);
module_exit(rk_fb_exit);

