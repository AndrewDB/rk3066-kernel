#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/display-sys.h>
#include <linux/interrupt.h>
#include "rk30_hdmi.h"

static int hdmi_get_enable(struct rk_display_device *device)
{
	struct hdmi *hdmi = device->priv_data;
	int enable;
	
	mutex_lock(&hdmi->enable_mutex);
	enable = hdmi->enable;
	mutex_unlock(&hdmi->enable_mutex);
	
	return enable;
}

static int hdmi_set_enable(struct rk_display_device *device, int enable)
{
	struct hdmi *hdmi = device->priv_data;
	
	mutex_lock(&hdmi->enable_mutex);
	if(hdmi->enable == enable) {
		mutex_unlock(&hdmi->enable_mutex);
		return 0;
	}
	hdmi->enable = enable;
	
	if(hdmi->suspend ) {
		mutex_unlock(&hdmi->enable_mutex);
		return 0;
	}
	
	if(enable == 0) {
		disable_irq(hdmi->irq);
		mutex_unlock(&hdmi->enable_mutex);
		hdmi->command = HDMI_CONFIG_ENABLE;
		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 0);
	}
	else {
		enable_irq(hdmi->irq);
		mutex_unlock(&hdmi->enable_mutex);
	}
	return 0;
}

static int hdmi_get_status(struct rk_display_device *device)
{
	struct hdmi *hdmi = device->priv_data;
	if(hdmi->hotplug == HDMI_HPD_ACTIVED)
		return 1;
	else
		return 0;
}

static int hdmi_get_modelist(struct rk_display_device *device, struct list_head **modelist)
{
	struct hdmi *hdmi = device->priv_data;
//	if(!hdmi->hotplug)
//		return -1;
	*modelist = &hdmi->edid.modelist;
	return 0;
}

static int hdmi_set_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	struct hdmi *hdmi = device->priv_data;
	int vic = hdmi_videomode_to_vic(mode);
	
	if(!hdmi->hotplug)
		return -1;
	hdmi->autoconfig = HDMI_DISABLE;
	if(vic && hdmi->vic != vic)
	{
		hdmi->vic = vic;
		hdmi->command = HDMI_CONFIG_VIDEO;
		init_completion(&hdmi->complete);
		hdmi->wait = 1;
		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 0);
		wait_for_completion_interruptible_timeout(&hdmi->complete,
								msecs_to_jiffies(10000));
	}
	return 0;
}

static int hdmi_get_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	struct hdmi *hdmi = device->priv_data;
	struct fb_videomode *vmode;
	
//	if(!hdmi->hotplug)
//		return -1;
		
	vmode = (struct fb_videomode*) hdmi_vic_to_videomode(hdmi->vic);
	if(unlikely(vmode == NULL))
		return -1;
	*mode = *vmode;
	return 0;
}

static int hdmi_set_scale(struct rk_display_device *device, int direction, int value)
{
	struct hdmi *hdmi = device->priv_data;
	
	if(!hdmi || value < 0 || value > 100)
		return -1;
			
	if(direction == DISPLAY_SCALE_X)
		hdmi->xscale = value;
	else if(direction == DISPLAY_SCALE_Y)
		hdmi->yscale = value;
	else
		return -1;
//	rk_fb_disp_scale(hdmi->xscale, hdmi->yscale, HDMI_SOURCE_DEFAULT);
	return 0;
}

static int hdmi_get_scale(struct rk_display_device *device, int direction)
{
	struct hdmi *hdmi = device->priv_data;
	
	if(!hdmi)
		return -1;
		
	if(direction == DISPLAY_SCALE_X)
		return hdmi->xscale;
	else if(direction == DISPLAY_SCALE_Y)
		return hdmi->yscale;
	else
		return -1;
}

static struct rk_display_ops hdmi_display_ops = {
	.setenable = hdmi_set_enable,
	.getenable = hdmi_get_enable,
	.getstatus = hdmi_get_status,
	.getmodelist = hdmi_get_modelist,
	.setmode = hdmi_set_mode,
	.getmode = hdmi_get_mode,
	.setscale = hdmi_set_scale,
	.getscale = hdmi_get_scale,
};

static int hdmi_display_probe(struct rk_display_device *device, void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "HDMI");
	device->priority = DISPLAY_PRIORITY_HDMI;
	device->name = "hdmi-soc";
	device->property = DISPLAY_MAIN;
	device->priv_data = devdata;
	device->ops = &hdmi_display_ops;
	return 1;
}

static struct rk_display_driver display_hdmi = {
	.probe = hdmi_display_probe,
};

struct rk_display_device* hdmi_register_display_sysfs(struct hdmi *hdmi, struct device *parent)
{
	return rk_display_device_register(&display_hdmi, parent, hdmi);
}

void hdmi_unregister_display_sysfs(struct hdmi *hdmi)
{
	if(hdmi->ddev)
		rk_display_device_unregister(hdmi->ddev);
}
