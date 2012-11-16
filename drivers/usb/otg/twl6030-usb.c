/*
 * twl6030_usb - TWL6030 USB transceiver, talking to OMAP OTG driver.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Hema HK <hemahk@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/usb/otg.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/delay.h>

//#include <plat/usb.h>

/* usb register definitions */
#define USB_VENDOR_ID_LSB		0x00
#define USB_VENDOR_ID_MSB		0x01
#define USB_PRODUCT_ID_LSB		0x02
#define USB_PRODUCT_ID_MSB		0x03
#define USB_VBUS_CTRL_SET		0x04
#define USB_VBUS_CTRL_CLR		0x05
#define USB_ID_CTRL_SET			0x06
#define USB_ID_CTRL_CLR			0x07
#define USB_VBUS_INT_SRC		0x08
#define USB_VBUS_INT_LATCH_SET		0x09
#define USB_VBUS_INT_LATCH_CLR		0x0A
#define USB_VBUS_INT_EN_LO_SET		0x0B
#define USB_VBUS_INT_EN_LO_CLR		0x0C
#define USB_VBUS_INT_EN_HI_SET		0x0D
#define USB_VBUS_INT_EN_HI_CLR		0x0E
#define USB_ID_INT_SRC			0x0F
#define USB_ID_INT_LATCH_SET		0x10
#define USB_ID_INT_LATCH_CLR		0x11

#define USB_ID_INT_EN_LO_SET		0x12
#define USB_ID_INT_EN_LO_CLR		0x13
#define USB_ID_INT_EN_HI_SET		0x14
#define USB_ID_INT_EN_HI_CLR		0x15
#define USB_OTG_ADP_CTRL		0x16
#define USB_OTG_ADP_HIGH		0x17
#define USB_OTG_ADP_LOW			0x18
#define USB_OTG_ADP_RISE		0x19
#define USB_OTG_REVISION		0x1A

#define TWL6030_MISC2			0xE5
#define TWL6030_BACKUP_REG		0xFA

#define STS_HW_CONDITIONS		0x21

/* In module TWL6030_MODULE_PM_MASTER */
#define STS_HW_CONDITIONS		0x21
#define STS_USB_ID			BIT(2)

/* In module TWL6030_MODULE_PM_RECEIVER */
#define VUSB_CFG_TRANS			0x71
#define VUSB_CFG_STATE			0x72
#define VUSB_CFG_VOLTAGE		0x73

/* in module TWL6030_MODULE_MAIN_CHARGE */

#define CHARGERUSB_CTRL1		0x8
#define SUSPEND_BOOT    (1 << 7)
#define OPA_MODE        (1 << 6)
#define HZ_MODE         (1 << 5)
#define TERM            (1 << 4)


#define CONTROLLER_STAT1		0x03
#define	VBUS_DET			BIT(2)

extern int get_msc_connect_flag(void);


struct twl6030_usb {
	struct otg_transceiver	otg;
	struct device		*dev;

	/* for vbus reporting with irqs disabled */
	spinlock_t		lock;

	struct regulator		*usb3v3;

	/* used to set vbus, in atomic path */
	struct work_struct	set_vbus_work;

	int			irq1;
	int			irq2;
	unsigned int		usb_cinlimit_mA;
	u8			linkstat;
	u8			asleep;
	u8			prev_vbus;
	bool			irq_enabled;
	bool			vbus_enable;
	bool			is_phy_suspended;
	unsigned long		features;
};

#define xceiv_to_twl(x)		container_of((x), struct twl6030_usb, otg)

/*-------------------------------------------------------------------------*/

static inline int twl6030_writeb(struct twl6030_usb *twl, u8 module,
						u8 data, u8 address)
{
	int ret = 0;

	ret = twl_i2c_write_u8(module, data, address);
	if (ret < 0)
		dev_err(twl->dev,
			"Write[0x%x] Error %d\n", address, ret);
	return ret;
}

static inline u8 twl6030_readb(struct twl6030_usb *twl, u8 module, u8 address)
{
	u8 data, ret = 0;

	ret = twl_i2c_read_u8(module, &data, address);
	if (ret >= 0)
		ret = data;
	else
		dev_err(twl->dev,
			"readb[0x%x,0x%x] Error %d\n",
					module, address, ret);
	return ret;
}

/*-------------------------------------------------------------------------*/
static int twl6030_set_phy_clk(struct otg_transceiver *x, int on)
{
	struct twl6030_usb *twl;
	struct device *dev;
	struct twl4030_usb_data *pdata;

	twl = xceiv_to_twl(x);
	dev  = twl->dev;
	pdata = dev->platform_data;

	pdata->phy_set_clock(twl->dev, on);

	return 0;
}

static int twl6030_phy_init(struct otg_transceiver *x)
{
	struct twl6030_usb *twl;
	struct device *dev;
	struct twl4030_usb_data *pdata;

	twl = xceiv_to_twl(x);
	dev  = twl->dev;
	pdata = dev->platform_data;

	if (twl->linkstat == USB_EVENT_ID)
		pdata->phy_power(twl->dev, 1, 1);
	else
		pdata->phy_power(twl->dev, 0, 1);

	return 0;
}

static void twl6030_phy_shutdown(struct otg_transceiver *x)
{
	struct twl6030_usb *twl;
	struct device *dev;
	struct twl4030_usb_data *pdata;

	twl = xceiv_to_twl(x);
	dev  = twl->dev;
	pdata = dev->platform_data;
	pdata->phy_power(twl->dev, 0, 0);
}

static int twl6030_phy_suspend(struct otg_transceiver *x, int suspend)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);
	struct device *dev = twl->dev;
	struct twl4030_usb_data *pdata = dev->platform_data;

	if (suspend && !twl->is_phy_suspended) {
		pdata->phy_suspend(dev, 1);
		twl->is_phy_suspended = true;
	} else if (!suspend && twl->is_phy_suspended) {
		pdata->phy_suspend(dev, 0);
		twl->is_phy_suspended = false;
	}
	return 0;
}

static int twl6030_start_srp(struct otg_transceiver *x)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);

	twl6030_writeb(twl, TWL_MODULE_USB, 0x24, USB_VBUS_CTRL_SET);
	twl6030_writeb(twl, TWL_MODULE_USB, 0x84, USB_VBUS_CTRL_SET);

	mdelay(100);
	twl6030_writeb(twl, TWL_MODULE_USB, 0xa0, USB_VBUS_CTRL_CLR);

	return 0;
}

static int twl6030_usb_ldo_init(struct twl6030_usb *twl)
{
	char *regulator_name;
	u8 misc2_data = 0;

	if (twl->features & TWL6032_SUBCLASS)
		regulator_name = "ldousb";
	else
		regulator_name = "vusb";

	/* Set to OTG_REV 1.3 and turn on the ID_WAKEUP_COMP */
	twl6030_writeb(twl, TWL6030_MODULE_ID0 , 0x1, TWL6030_BACKUP_REG);

	twl->usb3v3 = regulator_get(twl->dev, regulator_name);
	if (IS_ERR(twl->usb3v3))
		return -ENODEV;

	/* Program the USB_VBUS_CTRL_SET and set VBUS_ACT_COMP bit */
	twl6030_writeb(twl, TWL_MODULE_USB, 0x4, USB_VBUS_CTRL_SET);

	/*
	 * Program the USB_ID_CTRL_SET register to enable GND drive
	 * and the ID comparators
	 */
	twl6030_writeb(twl, TWL_MODULE_USB, 0x14, USB_ID_CTRL_SET);

	/* Program MISC2 register and clear bit VUSB_IN_VBAT */
	misc2_data = twl6030_readb(twl, TWL6030_MODULE_ID0, TWL6030_MISC2);
	misc2_data &= 0xEF;
	twl6030_writeb(twl, TWL6030_MODULE_ID0, misc2_data, TWL6030_MISC2);

	return 0;
}

static ssize_t twl6030_usb_vbus_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct twl6030_usb *twl = dev_get_drvdata(dev);
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&twl->lock, flags);

	switch (twl->linkstat) {
	case USB_EVENT_VBUS:
		ret = snprintf(buf, PAGE_SIZE, "vbus\n");
		break;
	case USB_EVENT_ID:
		ret = snprintf(buf, PAGE_SIZE, "id\n");
		break;
	case USB_EVENT_NONE:
		ret = snprintf(buf, PAGE_SIZE, "none\n");
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "UNKNOWN\n");
	}
	spin_unlock_irqrestore(&twl->lock, flags);

	return ret;
}
static DEVICE_ATTR(vbus, 0444, twl6030_usb_vbus_show, NULL);

static irqreturn_t twl6030_usb_irq(int irq, void *_twl)
{
	struct twl6030_usb *twl = _twl;
	int status;
	u8 vbus_state, hw_state, misc2_data;
	unsigned charger_type;

	hw_state = twl6030_readb(twl, TWL6030_MODULE_ID0, STS_HW_CONDITIONS);

	vbus_state = twl6030_readb(twl, TWL_MODULE_MAIN_CHARGE,
						CONTROLLER_STAT1);
	vbus_state = vbus_state & VBUS_DET;

	/* Ignore charger events other than VBUS */
	if (vbus_state == twl->prev_vbus)
		return IRQ_HANDLED;

	if ((vbus_state) && !(hw_state & STS_USB_ID)) {
		/* Program MISC2 register and set bit VUSB_IN_VBAT */
		misc2_data = twl6030_readb(twl, TWL6030_MODULE_ID0,
						TWL6030_MISC2);
		misc2_data |= 0x10;
		twl6030_writeb(twl, TWL6030_MODULE_ID0, misc2_data,
						TWL6030_MISC2);

		regulator_enable(twl->usb3v3);
		twl6030_phy_suspend(&twl->otg, 0);
		if(0 == get_msc_connect_flag())
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		else
			charger_type = POWER_SUPPLY_TYPE_USB;
		
		twl6030_phy_suspend(&twl->otg, 1);
		if ((charger_type == POWER_SUPPLY_TYPE_USB_CDP)
				|| (charger_type == POWER_SUPPLY_TYPE_USB)) {

			status = USB_EVENT_VBUS;
			twl->otg.default_a = false;
			twl->asleep = 1;
			twl->otg.state = OTG_STATE_B_IDLE;
			twl->linkstat = status;
			twl->otg.last_event = status;
		} else if (charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
			regulator_disable(twl->usb3v3);
			status = USB_EVENT_CHARGER;
			twl->usb_cinlimit_mA = 1800;
			twl->otg.state = OTG_STATE_B_IDLE;
			twl->linkstat = status;
			twl->otg.last_event = status;
		} else {
			regulator_disable(twl->usb3v3);
			goto vbus_notify;
		}
		atomic_notifier_call_chain(&twl->otg.notifier,
				status, &charger_type);
	}
	if (!vbus_state) {
		status = USB_EVENT_NONE;
		twl->linkstat = status;
		twl->otg.last_event = status;
		atomic_notifier_call_chain(&twl->otg.notifier,
				status, twl->otg.gadget);
		if (twl->asleep) {
			regulator_disable(twl->usb3v3);
			twl->asleep = 0;
			/* Program MISC2 register and clear bit VUSB_IN_VBAT */
			misc2_data = twl6030_readb(twl, TWL6030_MODULE_ID0,
							TWL6030_MISC2);
			misc2_data &= 0xEF;
			twl6030_writeb(twl, TWL6030_MODULE_ID0, misc2_data,
							TWL6030_MISC2);
		}
	}

vbus_notify:
	sysfs_notify(&twl->dev->kobj, NULL, "vbus");
	twl->prev_vbus = vbus_state;
	return IRQ_HANDLED;
}

static irqreturn_t twl6030_usbotg_irq(int irq, void *_twl)
{

#ifndef CONFIG_USB_MUSB_PERIPHERAL
	struct twl6030_usb *twl = _twl;
	int status = USB_EVENT_NONE;
	u8 hw_state, misc2_data;

	hw_state = twl6030_readb(twl, TWL6030_MODULE_ID0, STS_HW_CONDITIONS);

	if (hw_state & STS_USB_ID) {

		if (twl->otg.state == OTG_STATE_A_IDLE)
			return IRQ_HANDLED;

		/* Program MISC2 register and set bit VUSB_IN_VBAT */
		misc2_data = twl6030_readb(twl, TWL6030_MODULE_ID0,
						TWL6030_MISC2);
		misc2_data |= 0x10;
		twl6030_writeb(twl, TWL6030_MODULE_ID0, misc2_data,
						TWL6030_MISC2);
		regulator_enable(twl->usb3v3);
		twl->asleep = 1;
		twl6030_writeb(twl, TWL_MODULE_USB, 0x1, USB_ID_INT_EN_HI_CLR);
		twl6030_writeb(twl, TWL_MODULE_USB, 0x10, USB_ID_INT_EN_HI_SET);
		status = USB_EVENT_ID;
		twl->otg.default_a = true;
		twl->otg.state = OTG_STATE_A_IDLE;
		twl->linkstat = status;
		twl->otg.last_event = status;
		atomic_notifier_call_chain(&twl->otg.notifier, status,
							twl->otg.gadget);
	} else  {
		twl6030_writeb(twl, TWL_MODULE_USB, 0x10, USB_ID_INT_EN_HI_CLR);
		twl6030_writeb(twl, TWL_MODULE_USB, 0x1, USB_ID_INT_EN_HI_SET);
	}
	twl6030_writeb(twl, TWL_MODULE_USB, status, USB_ID_INT_LATCH_CLR);
#endif

	return IRQ_HANDLED;
}

static int twl6030_set_peripheral(struct otg_transceiver *x,
		struct usb_gadget *gadget)
{
	struct twl6030_usb *twl;

	if (!x)
		return -ENODEV;

	twl = xceiv_to_twl(x);
	twl->otg.gadget = gadget;
	if (!gadget)
		twl->otg.state = OTG_STATE_UNDEFINED;

	return 0;
}

static int twl6030_enable_irq(struct otg_transceiver *x)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);

	twl6030_writeb(twl, TWL_MODULE_USB, 0x1, USB_ID_INT_EN_HI_SET);
	twl6030_interrupt_unmask(0x05, REG_INT_MSK_LINE_C);
	twl6030_interrupt_unmask(0x05, REG_INT_MSK_STS_C);

	twl6030_interrupt_unmask(TWL6030_CHARGER_CTRL_INT_MASK,
				REG_INT_MSK_LINE_C);
	twl6030_interrupt_unmask(TWL6030_CHARGER_CTRL_INT_MASK,
				REG_INT_MSK_STS_C);
	twl6030_usb_irq(twl->irq2, twl);
	twl6030_usbotg_irq(twl->irq1, twl);

	return 0;
}

unsigned int twl6030_get_usb_max_power(struct otg_transceiver *x)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);

	return twl->usb_cinlimit_mA;
}

static void otg_set_vbus_work(struct work_struct *data)
{
	struct twl6030_usb *twl = container_of(data, struct twl6030_usb,
								set_vbus_work);
	/*
	 * Start driving VBUS. Set OPA_MODE bit in CHARGERUSB_CTRL1
	 * register. This enables boost mode.
	 */
	if (twl->vbus_enable)
		twl6030_writeb(twl, TWL_MODULE_MAIN_CHARGE , 0x40,
						CHARGERUSB_CTRL1);
	else
		twl6030_writeb(twl, TWL_MODULE_MAIN_CHARGE , 0x00,
						CHARGERUSB_CTRL1);
}

static int twl6030_set_hz_mode(struct otg_transceiver *x, bool enabled)
{
	u8 val;
	struct twl6030_usb *twl;

	if (!x)
		return -ENODEV;

	twl = xceiv_to_twl(x);

	/* set/reset USB charger in High impedence mode on VBUS */
	val = twl6030_readb(twl, TWL_MODULE_MAIN_CHARGE,
						CHARGERUSB_CTRL1);

	if (enabled)
		val |= HZ_MODE;
	else
		val &= ~HZ_MODE;

	twl6030_writeb(twl, TWL_MODULE_MAIN_CHARGE , val,
						CHARGERUSB_CTRL1);

	return 0;
}

static int twl6030_set_vbus(struct otg_transceiver *x, bool enabled)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);

	twl->vbus_enable = enabled;
	schedule_work(&twl->set_vbus_work);
	return 0;
}

static int twl6030_set_host(struct otg_transceiver *x, struct usb_bus *host)
{
	struct twl6030_usb *twl;

	if (!x)
		return -ENODEV;

	twl = xceiv_to_twl(x);
	twl->otg.host = host;
	if (!host)
		twl->otg.state = OTG_STATE_UNDEFINED;
	return 0;
}

static int twl6030_set_power(struct otg_transceiver *x, unsigned int mA)
{
	struct twl6030_usb *twl = xceiv_to_twl(x);

	twl->usb_cinlimit_mA = mA;
	if (mA && (twl->otg.last_event != USB_EVENT_NONE))
		atomic_notifier_call_chain(&twl->otg.notifier, USB_EVENT_ENUMERATED,
				&twl->usb_cinlimit_mA);
	return 0;
}

static int __devinit twl6030_usb_probe(struct platform_device *pdev)
{
	struct twl6030_usb	*twl;
	int			status, err;
	struct twl4030_usb_data *pdata;
	struct device *dev = &pdev->dev;
	pdata = dev->platform_data;

	twl = kzalloc(sizeof *twl, GFP_KERNEL);
	if (!twl)
		return -ENOMEM;

	twl->dev		= &pdev->dev;
	twl->irq1		= platform_get_irq(pdev, 0);
	twl->irq2		= platform_get_irq(pdev, 1);
	twl->features		= pdata->features;
	twl->otg.dev		= twl->dev;
	twl->otg.label		= "twl6030";
	twl->otg.set_host	= twl6030_set_host;
	twl->otg.set_peripheral	= twl6030_set_peripheral;
	twl->otg.set_vbus	= twl6030_set_vbus;
	twl->otg.set_hz_mode	= twl6030_set_hz_mode;
	twl->otg.init		= twl6030_phy_init;
	twl->otg.set_power    = twl6030_set_power;
	twl->otg.shutdown	= twl6030_phy_shutdown;
	twl->otg.set_suspend	= twl6030_phy_suspend;
	twl->otg.start_srp	= twl6030_start_srp;
	twl->otg.state		= OTG_STATE_UNDEFINED;

	/* init spinlock for workqueue */
	spin_lock_init(&twl->lock);

	err = twl6030_usb_ldo_init(twl);
	if (err) {
		dev_err(&pdev->dev, "ldo init failed\n");
		kfree(twl);
		return err;
	}
	otg_set_transceiver(&twl->otg);

	platform_set_drvdata(pdev, twl);
	if (device_create_file(&pdev->dev, &dev_attr_vbus))
		dev_warn(&pdev->dev, "could not create sysfs file\n");

	ATOMIC_INIT_NOTIFIER_HEAD(&twl->otg.notifier);

	INIT_WORK(&twl->set_vbus_work, otg_set_vbus_work);

	twl->vbus_enable = false;
	twl->irq_enabled = true;
	status = request_threaded_irq(twl->irq1, NULL, twl6030_usbotg_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"twl6030_usb", twl);
	if (status < 0) {
		dev_err(&pdev->dev, "can't get IRQ %d, err %d\n",
			twl->irq1, status);
		device_remove_file(twl->dev, &dev_attr_vbus);
		kfree(twl);
		return status;
	}

	status = request_threaded_irq(twl->irq2, NULL, twl6030_usb_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"twl6030_usb", twl);
	if (status < 0) {
		dev_err(&pdev->dev, "can't get IRQ %d, err %d\n",
			twl->irq2, status);
		free_irq(twl->irq1, twl);
		device_remove_file(twl->dev, &dev_attr_vbus);
		kfree(twl);
		return status;
	}

	twl->asleep = 0;
	twl->is_phy_suspended = true;
	pdata->phy_init(dev);
	twl6030_phy_suspend(&twl->otg, 0);
	twl6030_enable_irq(&twl->otg);
	dev_info(&pdev->dev, "Initialized TWL6030 USB module\n");

	return 0;
}

static int __exit twl6030_usb_remove(struct platform_device *pdev)
{
	struct twl6030_usb *twl = platform_get_drvdata(pdev);

	struct twl4030_usb_data *pdata;
	struct device *dev = &pdev->dev;
	pdata = dev->platform_data;

	twl6030_interrupt_mask(TWL6030_USBOTG_INT_MASK,
		REG_INT_MSK_LINE_C);
	twl6030_interrupt_mask(TWL6030_USBOTG_INT_MASK,
			REG_INT_MSK_STS_C);
	free_irq(twl->irq1, twl);
	free_irq(twl->irq2, twl);
	regulator_put(twl->usb3v3);
	pdata->phy_exit(twl->dev);
	device_remove_file(twl->dev, &dev_attr_vbus);
	cancel_work_sync(&twl->set_vbus_work);
	kfree(twl);

	return 0;
}

static struct platform_driver twl6030_usb_driver = {
	.probe		= twl6030_usb_probe,
	.remove		= __exit_p(twl6030_usb_remove),
	.driver		= {
		.name	= "twl6030_usb",
		.owner	= THIS_MODULE,
	},
};

static int __init twl6030_usb_init(void)
{
	return platform_driver_register(&twl6030_usb_driver);
}
subsys_initcall(twl6030_usb_init);

static void __exit twl6030_usb_exit(void)
{
	platform_driver_unregister(&twl6030_usb_driver);
}
module_exit(twl6030_usb_exit);

MODULE_ALIAS("platform:twl6030_usb");
MODULE_AUTHOR("Hema HK <hemahk@ti.com>");
MODULE_DESCRIPTION("TWL6030 USB transceiver driver");
MODULE_LICENSE("GPL");
