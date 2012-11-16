/*$_rbox_$_ modify _$hzb,20120522*/
/*$_rbox_$_ modify _$add this file for rk29 remotectl*/

#ifndef __RKXX_REMOTECTL_H__
#define __RKXX_REMOTECTL_H__
#include <linux/input.h>

/********************************************************************
**                            �궨��                                *
********************************************************************/
#define TIME_BIT0_MIN  625  /*Bit0  1.125ms*/
#define TIME_BIT0_MAX  1625

#define TIME_BIT1_MIN  1650  /*Bit1  2.25ms*/
#define TIME_BIT1_MAX  2650

#define TIME_PRE_MIN   13000   /*4500*/
#define TIME_PRE_MAX   14000   /*5500*/           /*PreLoad 4.5+0.56 = 5.06ms*/

#define TIME_RPT_MIN   98100   /*101000*/
#define TIME_RPT_MAX   98300   /*103000*/         /*Repeat  105-2.81=102.19ms*/  //110-9-2.25-0.56=98.19ms

#define TIME_SEQ_MIN   11200   /*2650*/
#define TIME_SEQ_MAX   11300   /*3000*/           /*sequence  2.25+0.56=2.81ms*/ //11.25ms


/********************************************************************
**                          �ṹ����                                *
********************************************************************/
typedef enum _RMC_STATE
{
    RMC_IDLE,
    RMC_PRELOAD,
    RMC_USERCODE,
    RMC_GETDATA,
    RMC_SEQUENCE
}eRMC_STATE;


struct RKxx_remotectl_platform_data {
	//struct rkxx_remotectl_button *buttons;
	int nbuttons;
	int rep;
	int gpio;
	int active_low;
	int timer;
	int wakeup;
	void (*set_iomux)(void);
};

#endif

