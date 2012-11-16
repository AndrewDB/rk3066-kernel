/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the bluetooth device bcm4329's driver,
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/rfkill.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>
#include <asm/irq.h>
#include <mach/iomux.h>
#include <linux/wakelock.h>
#include <linux/timer.h>
#include <mach/board.h>

#if 0
#define DBG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)
#else
#define DBG(x...)
#endif

#define LOG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)

#ifdef CONFIG_BCM4329
#define WIFI_BT_POWER_TOGGLE	1
#else
#define WIFI_BT_POWER_TOGGLE	0
#endif

#define BT_WAKE_LOCK_TIMEOUT    10 //s

#define BT_AUTO_SLEEP_TIMEOUT   3

/*
 * IO Configuration for RK29
 */
#ifdef CONFIG_ARCH_RK29

#define BT_WAKE_HOST_SUPPORT    0

/* IO configuration */
// BT power pin
#define BT_GPIO_POWER           RK29_PIN5_PD6
#define IOMUX_BT_GPIO_POWER()     rk29_mux_api_set(GPIO5D6_SDMMC1PWREN_NAME, GPIO5H_GPIO5D6);

// BT reset pin
#define BT_GPIO_RESET           RK29_PIN6_PC4
#define IOMUX_BT_GPIO_RESET()

// BT wakeup pin
#define BT_GPIO_WAKE_UP         RK29_PIN6_PC5
#define IOMUX_BT_GPIO_WAKE_UP()

// BT wakeup host pin
#define BT_GPIO_WAKE_UP_HOST
#define IOMUX_BT_GPIO_WAKE_UP_HOST()

//bt cts paired to uart rts
#define UART_RTS                RK29_PIN2_PA7
#define IOMUX_UART_RTS_GPIO()   rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME, GPIO2L_GPIO2A7)
#define IOMUX_UART_RTS()        rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME, GPIO2L_UART2_RTS_N)

/*
 * IO Configuration for RK30
 */
#elif defined (CONFIG_ARCH_RK30)

#define BT_WAKE_HOST_SUPPORT    1

/* IO configuration */
// BT power pin
#define BT_GPIO_POWER           RK30_PIN3_PC7
#define IOMUX_BT_GPIO_POWER()     rk29_mux_api_set(GPIO3C7_SDMMC1WRITEPRT_NAME, GPIO3C_GPIO3C7);

// BT reset pin
#define BT_GPIO_RESET           RK30_PIN3_PD1
#define IOMUX_BT_GPIO_RESET()     rk29_mux_api_set(GPIO3D1_SDMMC1BACKENDPWR_NAME, GPIO3D_GPIO3D1);

// BT wakeup pin
#define BT_GPIO_WAKE_UP         RK30_PIN3_PC6
#define IOMUX_BT_GPIO_WAKE_UP() rk29_mux_api_set(GPIO3C6_SDMMC1DETECTN_NAME, GPIO3C_GPIO3C6);

// BT wakeup host pin
#define BT_GPIO_WAKE_UP_HOST    RK30_PIN6_PA7
#define BT_IRQ_WAKE_UP_HOST     gpio_to_irq(BT_GPIO_WAKE_UP_HOST)
#define IOMUX_BT_GPIO_WAKE_UP_HOST()

//bt cts paired to uart rts
#define UART_RTS                RK30_PIN1_PA3
#define IOMUX_UART_RTS_GPIO()     rk29_mux_api_set(GPIO1A3_UART0RTSN_NAME, GPIO1A_GPIO1A3)
#define IOMUX_UART_RTS()          rk29_mux_api_set(GPIO1A3_UART0RTSN_NAME, GPIO1A_UART0_RTS_N)

#endif

struct bt_ctrl
{
    struct rfkill *bt_rfk;
#if BT_WAKE_HOST_SUPPORT
    struct timer_list tl;
    bool b_HostWake;
    struct wake_lock bt_wakelock;
#endif
};

static const char bt_name[] = 
#if defined(CONFIG_RKWIFI)
    #if defined(CONFIG_RKWIFI_26M)
        "rk903_26M"
    #else
        "rk903"
    #endif
#elif defined(CONFIG_BCM4329)
    "bcm4329"
#elif defined(CONFIG_MV8787)
    "mv8787"
#else
    "bt_default"
#endif
;

#if WIFI_BT_POWER_TOGGLE
extern int rk29sdk_bt_power_state;
extern int rk29sdk_wifi_power_state;
#endif

struct bt_ctrl gBtCtrl;
struct timer_list bt_sleep_tl;

void bcm4325_sleep(unsigned long bSleep);

#if BT_WAKE_HOST_SUPPORT
void resetBtHostSleepTimer(void)
{
    mod_timer(&(gBtCtrl.tl),jiffies + BT_WAKE_LOCK_TIMEOUT*HZ);//���������ó�ʱֵ��    
}

void btWakeupHostLock(void)
{
    if(gBtCtrl.b_HostWake == false){
        DBG("** Lock **\n");
        wake_lock(&(gBtCtrl.bt_wakelock));
        gBtCtrl.b_HostWake = true;
    }
}

void btWakeupHostUnlock(void)
{
    if(gBtCtrl.b_HostWake == true){        
        DBG("** UnLock **\n");
        wake_unlock(&(gBtCtrl.bt_wakelock));  //��ϵͳ˯��    
        gBtCtrl.b_HostWake = false;
    }    
}

static void timer_hostSleep(unsigned long arg)
{     
	DBG("b_HostWake=%d\n", gBtCtrl.b_HostWake);
    btWakeupHostUnlock();
}

#ifdef CONFIG_PM
static irqreturn_t bcm4329_wake_host_irq(int irq, void *dev)
{
    DBG("%s\n",__FUNCTION__);

    btWakeupHostLock();
    resetBtHostSleepTimer();
	return IRQ_HANDLED;
}

static void rfkill_do_wakeup(struct work_struct *work)
{
    // disable bt wakeup host
    DBG("** free irq\n");
    free_irq(BT_IRQ_WAKE_UP_HOST, NULL);

    DBG("Enable UART_RTS\n");
    gpio_set_value(UART_RTS, GPIO_LOW);
    IOMUX_UART_RTS();
}

static DECLARE_DELAYED_WORK(wakeup_work, rfkill_do_wakeup);

static int bcm4329_rfkill_suspend(struct platform_device *pdev, pm_message_t state)
{
    DBG("%s\n",__FUNCTION__);

    cancel_delayed_work(&wakeup_work);

#ifdef CONFIG_BT_AUTOSLEEP
    bcm4325_sleep(1);
#endif

    DBG("Disable UART_RTS\n");
	//To prevent uart to receive bt data when suspended
	IOMUX_UART_RTS_GPIO();
	gpio_request(UART_RTS, "uart_rts");
	gpio_set_value(UART_RTS, GPIO_HIGH);

    // enable bt wakeup host
    DBG("Request irq for bt wakeup host\n");
	if (0 == request_irq(BT_IRQ_WAKE_UP_HOST,
                    bcm4329_wake_host_irq,
                    IRQF_TRIGGER_FALLING,
                    "bt_wake",
                    NULL))
        enable_irq_wake(BT_IRQ_WAKE_UP_HOST);
    else
		LOG("Failed to request BT_WAKE_UP_HOST irq\n");

#ifdef CONFIG_RFKILL_RESET
    extern void rfkill_set_block(struct rfkill *rfkill, bool blocked);
    rfkill_set_block(gBtCtrl.bt_rfk, true);
#endif

    return 0;
}

static int bcm4329_rfkill_resume(struct platform_device *pdev)
{  
    DBG("%s\n",__FUNCTION__);

    // ϵͳ�˳�����˯�ߺ���Ҫ����RTS���Ӷ�������BT�����ݹ���
    // ����Ŀǰ������resume������ֱ������RTS�ᵼ��BT���ݶ�ʧ
    // �����ӳ�1s��������RTS
    // ϵͳ�˳�����˯��ʱ�ͷŵ�BT_IRQ_WAKE_UP_HOST����˯��ʱ����
    // �����룬Ŀǰ�����жϻص�������resume����ִ�У����resume
    // ʱֱ��free��IRQ���ᵼ���жϻص��������ᱻִ�У�
    DBG("delay 1s\n");
    schedule_delayed_work(&wakeup_work, HZ);

    return 0;
}
#else
#define bcm4329_rfkill_suspend NULL
#define bcm4329_rfkill_resume  NULL
#endif

#else
#ifdef CONFIG_PM
static int bcm4329_rfkill_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef CONFIG_BT_AUTOSLEEP
    bcm4325_sleep(1);
#endif
    return 0;
}
#else
#define bcm4329_rfkill_suspend  NULL
#endif
#define bcm4329_rfkill_resume  NULL
#endif

void bcm4325_sleep(unsigned long bSleep)
{
    DBG("*** bt sleep: %d ***\n", bSleep);
#ifdef CONFIG_BT_AUTOSLEEP
    del_timer(&bt_sleep_tl);// cmy: ȷ���ڻ���BTʱ�������򴥷�bt_sleep_tl������˯��
#endif

    IOMUX_BT_GPIO_WAKE_UP();
    gpio_set_value(BT_GPIO_WAKE_UP, bSleep?GPIO_LOW:GPIO_HIGH);

#ifdef CONFIG_BT_AUTOSLEEP
    if(!bSleep)
        mod_timer(&bt_sleep_tl, jiffies + BT_AUTO_SLEEP_TIMEOUT*HZ);//���������ó�ʱֵ��
#endif
}

static int bcm4329_set_block(void *data, bool blocked)
{
	DBG("set blocked :%d\n", blocked);

    IOMUX_BT_GPIO_POWER();
    IOMUX_BT_GPIO_RESET();

	if (false == blocked) { 
   		gpio_set_value(BT_GPIO_POWER, GPIO_HIGH);  /* bt power on */
		mdelay(20);

        gpio_set_value(BT_GPIO_RESET, GPIO_LOW);
        mdelay(20);
		gpio_set_value(BT_GPIO_RESET, GPIO_HIGH);  /* bt reset deactive*/

        mdelay(20);
        bcm4325_sleep(0); // ensure bt is wakeup
        
    	pr_info("bt turn on power\n");
	} else {
#if WIFI_BT_POWER_TOGGLE
		if (!rk29sdk_wifi_power_state) {
#endif
			gpio_set_value(BT_GPIO_POWER, GPIO_LOW);  /* bt power off */
    		mdelay(20);	
    		pr_info("bt shut off power\n");
#if WIFI_BT_POWER_TOGGLE
		}else {
			pr_info("bt shouldn't shut off power, wifi is using it!\n");
		}
#endif

        gpio_set_value(BT_GPIO_RESET, GPIO_LOW);  /* bt reset active*/
        mdelay(20);
	}

#if WIFI_BT_POWER_TOGGLE
	rk29sdk_bt_power_state = !blocked;
#endif
	return 0;
}

static const struct rfkill_ops bcm4329_rfk_ops = {
	.set_block = bcm4329_set_block,
};

static int __devinit bcm4329_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	bool default_state = true;
	
	DBG("Enter %s\n",__FUNCTION__);
	
	/* default to bluetooth off */
 	bcm4329_set_block(NULL, default_state); /* blocked -> bt off */
	 
	gBtCtrl.bt_rfk = rfkill_alloc(bt_name, 
                NULL, 
                RFKILL_TYPE_BLUETOOTH, 
                &bcm4329_rfk_ops, 
                NULL);

	if (!gBtCtrl.bt_rfk)
	{
		LOG("fail to rfkill_allocate\n");
		return -ENOMEM;
	}
	
	rfkill_set_states(gBtCtrl.bt_rfk, default_state, false);

	rc = rfkill_register(gBtCtrl.bt_rfk);
	if (rc)
	{
		LOG("failed to rfkill_register,rc=0x%x\n",rc);
		rfkill_destroy(gBtCtrl.bt_rfk);
	}
	
	gpio_request(BT_GPIO_POWER, NULL);
	gpio_request(BT_GPIO_RESET, NULL);
	gpio_request(BT_GPIO_WAKE_UP, NULL);
    
#ifdef CONFIG_BT_AUTOSLEEP
    init_timer(&bt_sleep_tl);
    bt_sleep_tl.expires = 0;
    bt_sleep_tl.function = bcm4325_sleep;
    bt_sleep_tl.data = 1;
    add_timer(&bt_sleep_tl);
#endif

#if BT_WAKE_HOST_SUPPORT
    init_timer(&(gBtCtrl.tl));
    gBtCtrl.tl.expires = 0;
    gBtCtrl.tl.function = timer_hostSleep;
    add_timer(&(gBtCtrl.tl));
    gBtCtrl.b_HostWake = false;
    
	wake_lock_init(&(gBtCtrl.bt_wakelock), WAKE_LOCK_SUSPEND, "bt_wake");
	
	rc = gpio_request(BT_GPIO_WAKE_UP_HOST, "bt_wake");
	if (rc) {
		LOG("Failed to request BT_WAKE_UP_HOST\n");
	}
	
	IOMUX_BT_GPIO_WAKE_UP_HOST();
	gpio_pull_updown(BT_GPIO_WAKE_UP_HOST,GPIOPullUp);
 #endif
 
    LOG("bcm4329 module has been initialized,rc=0x%x\n",rc);
 
	return rc;
}


static int __devexit bcm4329_rfkill_remove(struct platform_device *pdev)
{
	if (gBtCtrl.bt_rfk)
		rfkill_unregister(gBtCtrl.bt_rfk);
	gBtCtrl.bt_rfk = NULL;
#if BT_WAKE_HOST_SUPPORT
    del_timer(&(gBtCtrl.tl));//ɾ����ʱ��
    btWakeupHostUnlock();
    wake_lock_destroy(&(gBtCtrl.bt_wakelock));
#endif
#ifdef CONFIG_BT_AUTOSLEEP
    del_timer(&bt_sleep_tl);
#endif

	platform_set_drvdata(pdev, NULL);

	DBG("Enter %s\n",__FUNCTION__);
	return 0;
}

static struct platform_driver bcm4329_rfkill_driver = {
	.probe = bcm4329_rfkill_probe,
	.remove = __devexit_p(bcm4329_rfkill_remove),
	.driver = {
		.name = "rk29sdk_rfkill", 
		.owner = THIS_MODULE,
	},	
    .suspend = bcm4329_rfkill_suspend,
    .resume = bcm4329_rfkill_resume,
};

/*
 * Module initialization
 */
static int __init bcm4329_mod_init(void)
{
	int ret;
	DBG("Enter %s\n",__FUNCTION__);
	ret = platform_driver_register(&bcm4329_rfkill_driver);
	LOG("ret=0x%x\n", ret);
	return ret;
}

static void __exit bcm4329_mod_exit(void)
{
	platform_driver_unregister(&bcm4329_rfkill_driver);
}

module_init(bcm4329_mod_init);
module_exit(bcm4329_mod_exit);
MODULE_DESCRIPTION("bcm4329 Bluetooth driver");
MODULE_AUTHOR("roger_chen cz@rock-chips.com, cmy@rock-chips.com");
MODULE_LICENSE("GPL");

