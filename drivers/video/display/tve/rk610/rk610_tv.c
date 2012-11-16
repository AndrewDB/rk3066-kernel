/*
 * rk610_tv.c 
 *
 * Driver for rockchip rk610 tv control
 *  Copyright (C) 2009 
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
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include "rk610_tv.h"
#include "../../../rk29_fb.h"

#define DRV_NAME "rk610_tvout"
#define RK610_I2C_RATE     100*1000

struct rk610 rk610;

#define VIDEO_SWITCH_CVBS		GPIO_LOW
#define VIDEO_SWITCH_OTHER		GPIO_HIGH

#ifdef CONFIG_ARCH_RK29
extern int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable );
#else
#include <linux/rk_fb.h>
static int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable )
{
	return rk_fb_switch_screen(screen, enable , rk610.video_source);
}
#endif

int rk610_tv_wirte_reg(u8 reg, u8 data)
{
    int ret;
	if(rk610.client == NULL)
		return -1;
    ret = i2c_master_reg8_send(rk610.client, reg, &data, 1, RK610_I2C_RATE);
	if (ret > 0)
		ret = 0;
	return ret;
}

int rk610_switch_fb(const struct fb_videomode *modedb, int tv_mode)
{	
	struct rk29fb_screen *screen;
	
	if(modedb == NULL)
		return -1;
	screen =  kzalloc(sizeof(struct rk29fb_screen), GFP_KERNEL);
	if(screen == NULL)
		return -1;
	
	memset(screen, 0, sizeof(struct rk29fb_screen));	
	/* screen type & face */
    screen->type = SCREEN_HDMI;
	screen->mode = modedb->vmode;
	screen->face = modedb->flag;
	/* Screen size */
	screen->x_res = modedb->xres;
    screen->y_res = modedb->yres;

    /* Timing */
    screen->pixclock = modedb->pixclock;

	screen->lcdc_aclk = 500000000;
	screen->left_margin = modedb->left_margin;
	screen->right_margin = modedb->right_margin;
	screen->hsync_len = modedb->hsync_len;
	screen->upper_margin = modedb->upper_margin;
	screen->lower_margin = modedb->lower_margin;
	screen->vsync_len = modedb->vsync_len;

	/* Pin polarity */
	if(FB_SYNC_HOR_HIGH_ACT & modedb->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(FB_SYNC_VERT_HIGH_ACT & modedb->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;	
	screen->pin_den = 0;
	screen->pin_dclk = 1;

	/* Swap rule */
    screen->swap_rb = 0;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = NULL;
    screen->standby = NULL;	
    
    switch(tv_mode)
   	{
#ifdef CONFIG_RK610_TVOUT_CVBS
   		case TVOUT_CVBS_NTSC:
		case TVOUT_CVBS_PAL:
            screen->init = rk610_tv_cvbs_init;;
		break;
#endif

#ifdef CONFIG_RK610_TVOUT_YPbPr
		case TVOUT_YPbPr_720x480p_60:
		case TVOUT_YPbPr_720x576p_50:
		case TVOUT_YPbPr_1280x720p_50:
		case TVOUT_YPbPr_1280x720p_60:
		//case TVOUT_YPbPr_1920x1080i_50:
		case TVOUT_YPbPr_1920x1080i_60:
		case TVOUT_YPbPr_1920x1080p_50:
		case TVOUT_YPbPr_1920x1080p_60:
			screen->init = rk610_tv_ypbpr_init;
			break;
#endif
		default:{
			kfree(screen);
			return -1;
   		}
   		break;
   	}
   	rk610.mode = tv_mode;
   	FB_Switch_Screen(screen, 1);
   	kfree(screen);
   	
   	if(rk610.io_switch_pin != INVALID_GPIO) {
   		if(tv_mode < TVOUT_YPbPr_720x480p_60)
   			gpio_direction_output(rk610.io_switch_pin, VIDEO_SWITCH_CVBS);
   		else
   			gpio_direction_output(rk610.io_switch_pin, VIDEO_SWITCH_OTHER);
   	}
	return 0;
}

int rk610_tv_standby(int type)
{
	int ret;
	int ypbpr = 0, cvbs = 0;
	
	if(rk610.ypbpr)
		ypbpr = rk610.ypbpr->enable;
	if(rk610.cvbs)
		cvbs = rk610.cvbs->enable;
	if(cvbs || ypbpr)
		return 0;
	
	ret = rk610_tv_wirte_reg(TVE_POWERCR, 0);
	if(ret < 0){
		printk("[%s] rk610_tv_wirte_reg err!\n", __FUNCTION__);
		return ret;
	}
	
	ret = rk610_control_send_byte(TVE_CON, 0);
	if(ret < 0){
		printk("[%s] rk610_control_send_byte err!\n", __FUNCTION__);
		return ret;
	}
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk610_early_suspend(struct early_suspend *h)
{
	printk("rk610 enter early suspend");
	if(rk610.ypbpr)
		rk610.ypbpr->ddev->ops->setenable(rk610.ypbpr->ddev, 0);
	if(rk610.cvbs)
		rk610.cvbs->ddev->ops->setenable(rk610.cvbs->ddev, 0);
	return;
}

static void rk610_early_resume(struct early_suspend *h)
{
	printk("rk610 exit early resume");
	if( rk610.cvbs && (rk610.mode < TVOUT_YPbPr_720x480p_60) ) {
		rk_display_device_enable((rk610.cvbs)->ddev);
	}
	else if( rk610.ypbpr && (rk610.mode > TVOUT_CVBS_PAL) ) {
		rk_display_device_enable((rk610.ypbpr)->ddev);
	}
	return;
}
#endif

static int rk610_tv_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int rc = 0;
	struct rkdisplay_platform_data *tv_data;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENODEV;
		goto failout;
	}
	memset(&rk610, 0, sizeof(struct rk610));
	rk610.io_switch_pin = INVALID_GPIO;
	rk610.client = client;
	if(client->dev.platform_data) {
		tv_data = client->dev.platform_data;
		rk610.video_source = tv_data->video_source;
		rk610.property = tv_data->property;
	}
	else {
		rk610.video_source = DISPLAY_SOURCE_LCDC0;
		rk610.property = DISPLAY_MAIN;
	}
	if(rk610.io_switch_pin != INVALID_GPIO) {
		rc = gpio_request(rk610.io_switch_pin, NULL);
		if(rc) {
			gpio_free(rk610.io_switch_pin);
			printk(KERN_ERR "RK610 request video switch gpio error\n");
			return -1;
		}
	}
	
	rk610.mode = RK610_TVOUT_DEAULT;
	
#ifdef CONFIG_RK610_TVOUT_YPbPr
	rk610_register_display_ypbpr(&client->dev);
#endif

#ifdef CONFIG_RK610_TVOUT_CVBS
	rk610_register_display_cvbs(&client->dev);
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	rk610.early_suspend.suspend = rk610_early_suspend;
	rk610.early_suspend.resume = rk610_early_resume;
	rk610.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 10;
	register_early_suspend(&(rk610.early_suspend));
#endif
    printk(KERN_INFO "rk610_tv ver 1.0 probe ok\n");
    return 0;
failout:
	kfree(client);
	return rc;
}

static int rk610_tv_remove(struct i2c_client *client)
{
	return 0;
}


static const struct i2c_device_id rk610_tv_id[] = {
	{ DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk610_tv_id);

static struct i2c_driver rk610_tv_driver = {
	.driver 	= {
		.name	= DRV_NAME,
	},
	.id_table = rk610_tv_id,
	.probe = rk610_tv_probe,
	.remove = rk610_tv_remove,
};

static int __init rk610_tv_init(void)
{
	int ret = 0;
	ret = i2c_add_driver(&rk610_tv_driver);
	if(ret < 0){
		printk("i2c_add_driver err, ret = %d\n", ret);
	}
	return ret;
}

static void __exit rk610_tv_exit(void)
{
    i2c_del_driver(&rk610_tv_driver);
}

module_init(rk610_tv_init);
//late_initcall(rk610_tv_init);
module_exit(rk610_tv_exit);

/* Module information */
MODULE_DESCRIPTION("ROCKCHIP RK610 TV Output");
MODULE_LICENSE("GPL");


