#include <linux/fb.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/rk_fb.h>
#include <linux/rk_screen.h>

#define OUT_TYPE		SCREEN_HDMI
#define OUT_FACE		OUT_P888
#define LCDC_ACLK		312000000

#define OUT_CLK	    	74250000
#define H_PW			40
#define H_BP			220
#define H_VD			1280
#define H_FP			110
#define V_PW			5
#define V_BP			20
#define V_VD			720
#define V_FP			5

#define LCD_WIDTH       1280
#define LCD_HEIGHT      720
/* Other */
#define DCLK_POL		1
#define SWAP_RB			0


void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
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
	screen->pin_hsync = 1;
	screen->pin_vsync = 1;
	screen->pin_den = 0;
	screen->pin_dclk = DCLK_POL;

	/* Swap rule */
    screen->swap_rb = SWAP_RB;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = NULL;
    screen->standby = NULL;
}



