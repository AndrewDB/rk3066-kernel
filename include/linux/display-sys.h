#ifndef _LINUX_DISPLAY_RK_H
#define _LINUX_DISPLAY_RK_H
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/list.h>

struct rk_display_device;

enum rk_display_priority {
	DISPLAY_PRIORITY_TV = 0,
	DISPLAY_PRIORITY_YPbPr,
	DISPLAY_PRIORITY_VGA,
	DISPLAY_PRIORITY_HDMI,
	DISPLAY_PRIORITY_LCD,
};

enum {
	DISPLAY_SCALE_X = 0,
	DISPLAY_SCALE_Y
};

enum rk_display_property {
	DISPLAY_MAIN = 0,
	DISPLAY_AUX
};

enum rk_display_source {
	DISPLAY_SOURCE_LCDC0 = 0,
	DISPLAY_SOURCE_LCDC1
};

/* This structure defines all the properties of a Display. */
struct rk_display_driver {
	void (*suspend)(struct rk_display_device *, pm_message_t state);
	void (*resume)(struct rk_display_device *);
	int  (*probe)(struct rk_display_device *, void *);
	int  (*remove)(struct rk_display_device *);
};

struct rk_display_ops {
	int (*setenable)(struct rk_display_device *, int enable);
	int (*getenable)(struct rk_display_device *);
	int (*getstatus)(struct rk_display_device *);
	int (*getmodelist)(struct rk_display_device *, struct list_head **modelist);
	int (*setmode)(struct rk_display_device *, struct fb_videomode *mode);
	int (*getmode)(struct rk_display_device *, struct fb_videomode *mode);
	int (*setscale)(struct rk_display_device *, int, int);
	int (*getscale)(struct rk_display_device *, int);
};

struct rk_display_device {
	struct module *owner;			/* Owner module */
	struct rk_display_driver *driver;
	struct device *parent;			/* This is the parent */
	struct device *dev;			/* This is this display device */
	struct mutex lock;
	void *priv_data;
	char type[16];
	char *name;
	int idx;
	struct rk_display_ops *ops;
	int priority;
	int property;
	struct list_head list;
};

struct rk_display_devicelist {
	struct list_head list;
	struct rk_display_device *dev;
};

struct rkdisplay_platform_data {
	int property;			//display screen property: main display or aux display.
	int video_source;		//display screen video source
	int io_pwr_pin;			//power control gpio
	int io_reset_pin;		//reset control gpio
	int io_switch_pin;		//cvbs/ypbpr output switch gpio
};


extern struct rk_display_device *rk_display_device_register(struct rk_display_driver *driver,
					struct device *dev, void *devdata);
extern void rk_display_device_unregister(struct rk_display_device *dev);

extern void rk_display_device_enable(struct rk_display_device *ddev);

extern void rk_display_device_enable_other(struct rk_display_device *ddev);
extern void rk_display_device_disable_other(struct rk_display_device *ddev);


extern void rk_display_device_select(int property, int priority);

#define to_rk_display_device(obj) container_of(obj, struct rk_display_device, class_dev)

#endif
//$_rbox_$_modify_end