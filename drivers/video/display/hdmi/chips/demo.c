#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

static struct hdmi *hdmi = NULL;
static int irq_handle;


/* HDMI transmitter control interface */

/* 
 *	���ܣ���ʼ��HDMI������
 *	������hdmi		hdmi�豸ָ�룬����
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_init(struct hdmi *hdmi)
{
	
}

/* 
 *	���ܣ����Hotplug
 *	������hdmi			hdmi�豸ָ�룬����
 *		  hpdstatus		Hotplug״̬�����, �˵�ַ�ɵ����߷���
 �� 						TRUE: hotplug insert, FALSE: hotplug remove
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_detect_hpd(struct hdmi *hdmi, int *hpdstatus)
{

}

/* 
 *	���ܣ������ʾ�豸�Ƿ��ڼ���״̬
 *	������hdmi			hdmi�豸ָ�룬����
 *		  sink_status	��ʾ�豸״̬��������˵�ַ�ɵ����߷���
 ��						TRUE: sink active, FALSE: sink inactive
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_detect_sink(struct hdmi *hdmi, int *sink_status)
{

}

/* 
 *	���ܣ�HDMI�豸�����Ķ���
 *	������hdmi			hdmi�豸ָ�룬����
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_insert(struct hdmi *hdmi)
{

}

/* 
 *	���ܣ�HDMI�豸�γ���Ķ���
 *	������hdmi			hdmi�豸ָ�룬����
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_remove(struct hdmi *hdmi)
{

}

/* 
 *	���ܣ���ȡEDID����
 *	������hdmi			hdmi�豸ָ�룬����
 *		  block			EDID����block��ţ�����
 *		  buff			���ص�EDID����ָ�룬������˵�ַ�ɵ����߷���
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_read_edid(struct hdmi *hdmi, int block, unsigned char *buff)
{

}

/* 
 *	���ܣ�������Ƶ���
 *	������hdmi			hdmi�豸ָ�룬����
 *		  vic			HDMI��Ƶ��ʽ���룬���룬ȡֵ���hdmi-itv.h��enum hdmi_video_mode�ṹ��
 *		  input_color	�����LCDC��Ƶ������ɫģʽ��Ĭ��ΪHDMI_DEFAULT_COLOR
 *		  output_color	�����HDMI��Ƶ��ɫģʽ��Ĭ��ΪHDMI_DEFAULT_COLOR
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_config_video(struct hdmi *hdmi, int vic, int input_color, int output_color)
{
	// ������Ƶ���
}

/* 
 *	���ܣ�������Ƶ���
 *	������hdmi			hdmi�豸ָ�룬����
 *		  channel_num	��Ƶ���������������룬Ĭ��HDMI_AUDIO_DEFAULT_CHANNEL_NUM
 *		  fs			��Ƶ�����ʣ����룬Ĭ��ΪHDMI_AUDIO_DEFAULT_Fs
 *		  word_length	��Ƶ����λ�����룬Ĭ��ΪHDMI_AUDIO_DEFAULT_WORD_LENGTH
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_config_audio(struct hdmi *hdmi, int channel_num, int fs, int word_length)
{

}

/* 
 *	���ܣ�������Ƶ���
 *	������hdmi			hdmi�豸ָ�룬����
 *		  enable		�Ƿ���HDCP�����룬Ĭ��HDMI_HDCP_CONFIG
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_config_hdcp(struct hdmi *hdmi, int enable)
{

}

/* 
 *	���ܣ�������Ƶ���
 *	������hdmi			hdmi�豸ָ�룬����
 *		  enable		�Ƿ���HDMI���������
 *	���أ�HDMI_ERROR_SUCESS		�ɹ�
 *		  HDMI_ERROR_FALSE			ʧ��
 */
static int demo_sys_enalbe_output(struct hdmi *hdmi, int enable)
{

}

#ifdef HDMI_USE_IRQ
static irqreturn_t demo_detect_irq(int irq, void *dev_id)
{
	hdmi_changed(hdmi, 300);
    return IRQ_HANDLED;
}
#endif
static struct hdmi_ops demo_ops = {
	.init = demo_sys_init,
	.insert = demo_sys_insert,
	.remove = demo_sys_remove,
	.detect_hpd = demo_sys_detect_hpd,
	.detect_sink = demo_sys_detect_sink,
	.read_edid = demo_sys_read_edid,
	.config_video = demo_sys_config_video,
	.config_audio = demo_sys_config_audio,
	.config_hdcp = demo_sys_config_hdcp,
	.enable = demo_sys_enalbe_output,
};
/* I2C driver functions */
static int demo_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int rc = 0;
	struct demo_pdata *anx = NULL;
	
	// Register HDMI device
	hdmi = hdmi_register(&client->dev, &demo_ops);
	if(hdmi == NULL)
	{
		dev_err(&client->dev, "fail to register hdmi\n");
		goto err_hdmi_register;
	}
#ifdef HDMI_USE_IRQ
	//�жϷ�ʽ
	//����HDMI�����߳�
	hdmi_changed(hdmi, 1);
	
	//ע���ж�
    if((rc = gpio_request(client->irq, "hdmi gpio")) < 0)
    {
        dev_err(&client->dev, "fail to request gpio %d\n", client->irq);
        goto err_request_gpio;
    }
    irq_handle = gpio_to_irq(client->irq);
    gpio_pull_updown(client->irq,GPIOPullDown);
    gpio_direction_input(client->irq);
    if((rc = request_irq(irq_handle, demo_detect_irq,IRQF_TRIGGER_FALLING,NULL,hdmi)) <0)
    {
        dev_err(&client->dev, "fail to request hdmi irq\n");
        goto err_request_irq;
    }
#else
	//��ѯ��ʽ
	//����HDMI�����߳�
	queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, 200);
#endif
	hdmi_enable(hdmi);
	dev_info(&client->dev, "demo probe ok\n");

    return 0;
	
err_request_irq:
	gpio_free(client->irq);
err_request_gpio:
	hdmi_unregister(hdmi);
err_kzalloc_anx:
	kfree(hdmi);
	hdmi = NULL;
	dev_err(&client->dev, "demo probe error\n");
	return rc;

}

static int __devexit demo_i2c_remove(struct i2c_client *client)
{
	#ifdef HDMI_USE_IRG
	free_irq(irq_handle, NULL);
	gpio_free(client->irq);
	#endif
	hdmi_unregister(hdmi);
	hdmi_dbg(hdmi->dev, "%s\n", __func__);
    return 0;
}

static const struct i2c_device_id demo_id[] = {
	{ "demo", 0 },
	{ }
};

static struct i2c_driver demo_i2c_driver  = {
    .driver = {
        .name  = "demo",
        .owner = THIS_MODULE,
    },
    .probe =    &demo_i2c_probe,
    .remove     = &demo_i2c_remove,
    .id_table	= demo_id,
};

static int __init demo_init(void)
{
    return i2c_add_driver(&demo_i2c_driver);
}

static void __exit demo_exit(void)
{
    i2c_del_driver(&demo_i2c_driver);
}

fs_initcall(demo_init);
module_exit(demo_exit);