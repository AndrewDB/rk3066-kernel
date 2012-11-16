#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P888
#define OUT_CLK			 27000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA Ƶ��

/* Timing */
#define H_PW			10
#define H_BP			206
#define H_VD			800
#define H_FP			40

#define V_PW			10
#define V_BP			25
#define V_VD			480
#define V_FP			10


#define LCD_WIDTH       800    //need modify
#define LCD_HEIGHT      480

/* Other */
#define DCLK_POL		1 ///0
#define SWAP_RB			0

#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)

static struct rk29lcd_info *gLcd_info = NULL;

#define DRVDelayUs(i)   udelay(i*2)

int init(void);
int standby(u8 enable);

void set_lcd_info(struct rk29fb_screen *screen, struct rk2918lcd_info *lcd_info )
{
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;

    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    screen->width = LCD_WIDTH;
    screen->height = LCD_HEIGHT;

    /* Timing */
    screen->lcdc_aclk = LCDC_ACLK;
    screen->pixclock = OUT_CLK;
	screen->left_margin = H_BP;
	screen->right_margin = H_FP;
	screen->hsync_len = H_PW;
	screen->upper_margin = V_BP;
	screen->lower_margin = V_FP;
	screen->vsync_len = V_PW;

	/* Pin polarity */
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	screen->pin_den = 0;
	screen->pin_dclk = DCLK_POL;

	/* Swap rule */
    screen->swap_rb = SWAP_RB;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = init;
    screen->standby = standby;
    if(lcd_info)
        gLcd_info = lcd_info;
}


//void spi_screenreg_set(uint32 Addr, uint32 Data)
void spi_screenreg_set(u32 Data)
{
    u32 i;
    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_SET();
    DRVDelayUs(2);

	CS_CLR();
	for(i = 0; i < 16; i++)  //reg
	{
		if(Data &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();

		// \u6a21\u62dfCLK
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}

/*
	TXD_CLR();  //write

	// \u6a21\u62dfCLK
    CLK_CLR();
    DRVDelayUs(2);
    CLK_SET();
    DRVDelayUs(2);

	TXD_SET();  //highz

	// \u6a21\u62dfCLK
    CLK_CLR();
    DRVDelayUs(2);
    CLK_SET();
    DRVDelayUs(2);


	//for(i = 0; i < 8; i++)  //data
	for(i = 0; i < 16; i++)
	{
		if(Data &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();

		// \u6a21\u62dfCLK
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}
*/
	CS_SET();
	CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);

}


int init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();
/*
r0 00000010 11011011
r1 00010001 01101111
r2 00100000 10000000
r3 00110000 00001000
r4 01000001 10010000-->>01000001 10011111
r5 01100001 11001110
*/
	spi_screenreg_set(0x02db);
	spi_screenreg_set(0x116f);
	spi_screenreg_set(0x2080);
	spi_screenreg_set(0x3008);
	spi_screenreg_set(0x419f);
	spi_screenreg_set(0x61ce);
    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}

int standby(u8 enable)
{
    if(gLcd_info)
        gLcd_info->io_init();
	if(!enable) {
		init();
	} //else {
//		spi_screenreg_set(0x03, 0x5f);
//	}
    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}

