#ifdef BUILD_LK
#else
#include <linux/string.h>
#if defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include <mach/mt_gpio.h>
#endif
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
#define LCM_PRINT printf
#else
#if defined(BUILD_UBOOT)
	#define LCM_PRINT printf
#else
	#define LCM_PRINT printk
#endif
#endif

#define LCM_DBG(fmt, arg...) \
	LCM_PRINT("[LCM_OTM8019a_FWVGA_DSI_BOE] %s (line:%d) :" fmt "\r\n", __func__, __LINE__, ## arg)


// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  										(480)
#define FRAME_HEIGHT 										(854)

#define REGFLAG_DELAY             							0xFE
#define REGFLAG_END_OF_TABLE      							0xFFF   // END OF REGISTERS MARKER

#define LCM_ID_OTM8018B	0x18b

#define LCM_DSI_CMD_MODE									0

extern void Tinno_set_HS_read();
extern void Tinno_restore_HS_read();
//static unsigned int lcm_adc_read_chip_id();

#ifndef TRUE
    #define   TRUE     1
#endif
 
#ifndef FALSE
    #define   FALSE    0
#endif

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    								(lcm_util.set_reset_pin((v)))

#define UDELAY(n) 											(lcm_util.udelay(n))
#define MDELAY(n) 											(lcm_util.mdelay(n))

static unsigned char esdSwitch =  1;
// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)										lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)					lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg											lcm_util.dsi_read_reg()
#define read_reg_V2(cmd, buffer, buffer_size)				lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

 struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};


static struct LCM_setting_table lcm_initialization_setting[] = {
	
	/*
	Note :

	Data ID will depends on the following rule.
	
		count of parameters > 1	=> Data ID = 0x39
		count of parameters = 1	=> Data ID = 0x15
		count of parameters = 0	=> Data ID = 0x05

	Structure Format :

	{DCS command, count of parameters, {parameter list}}
	{REGFLAG_DELAY, milliseconds of time, {}},

	...

	Setting ending by predefined flag
	
	{REGFLAG_END_OF_TABLE, 0x00, {}}
	*/

	{0x00,1,{0x00}},	
	{0xFF,3,{0x80,0x19,0x01}},//enable EXTC

	{0x00,1,{0x80}},
	{0xFF,2,{0x80,0x19}},//enable orise mode

	{0x00,1,{0x03}},              
	{0xFF,1,{0x01}},

	{0x00,1,{0x90}},              
	{0xB3,1,{0x02}},

	{0x00,1,{0x92}},              
	{0xB3,1,{0x45}}, 

	{0x00,1,{0xA2}},              
	{0xC0,2,{0x00,0x1B}},

	{0x00,1,{0x80}},              
	{0xC0,9,{0x00,0x58,0x00,0x14,0x16,0x00,0x58,0x14,0x16}},

	{0x00,1,{0xB4}},              
	{0xC0,1,{0x00}},

	{0x00,1,{0xB5}},             
	{0xC0,1,{0x18}},

	{0x00,1,{0xB5}},            
	{0xC0,1,{0x48}},

	{0x00,1,{0x81}},              
	{0xC4,1,{0x04}},

	{0x00,1,{0x8A}},              
	{0xC4,1,{0x40}},

	{0x00,1,{0x80}},              
	{0xC1,1,{0x03}},

	{0x00,1,{0x90}},              
	{0xC0,6,{0x00,0x15,0x00,0x00,0x00,0x03}},

	{0x00,1,{0x80}},             
	{0xC4,1,{0x30}},

	{0x00,1,{0xA0}},              
	{0xC1,1,{0xE8}},

	{0x00,1,{0x98}},              
	{0xC0,1,{0x00}},

	{0x00,1,{0xA9}},              
	{0xC0,1,{0x0A}},

	{0x00,1,{0xB0}},              
	{0xC1,3,{0x20,0x00,0x00}},

	{0x00,1,{0xE1}},              
	{0xC0,2,{0x40,0x30}},

	{0x00,1,{0x90}},              
	{0xB6,1,{0xB4}},

	{0x00,1,{0x87}},              
	{0xC4,1,{0x18}}, 


	{0x00,1,{0x89}},              
	{0xC4,1,{0x08}},

	{0x00,1,{0x82}},              
	{0xC5,1,{0x03}},

	{0x00,1,{0x90}},              
	{0xC5,5,{0x4E,0x29,0x00,0x7B,0x44}},

	{0x00,1,{0x00}},              
	{0xD8,1,{0x5F}},

	{0x00,1,{0x01}},              
	{0xD8,1,{0x5F}},

//	{0x00,1,{0x00}},              
//	{0xD9,1,{0x42}},

	{0x00,1,{0x81}},              
	{0xC1,1,{0x03}},

	{0x00,1,{0xA1}},              
	{0xC1,1,{0x08}},

	{0x00,1,{0xB1}},              
	{0xC5,1,{0x29}},

	{0x00,1,{0x00}},              
	{0xE1,20,{0x1C,0x26,0x2F,0x3A,0x46,0x53,0x53,0x7B,0x6B,0x84,0x7F,0x6A,0x7C,0x57,0x53,0x43,0x35,0x2B,0x23,0x1F}},

	{0x00,1,{0x00 }},             
	{0xE2,20,{0x1C,0x27,0x30,0x3A,0x47,0x53,0x54,0x7C,0x6C,0x85,0x7F,0x6A,0x7C,0x57,0x54,0x44,0x36,0x2B,0x23,0x1F}},

	{0x00,1,{0x80}},              
	{0xCE,12,{0x86,0x01,0x00,0x85,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xA0}},              
	{0xCE,14,{0x18,0x05,0x83,0x5A,0x86,0x04,0x00,0x18,0x04,0x83,0x5B,0x86,0x04,0x00}},

	{0x00,1,{0xB0}},              
	{0xCE,14,{0x18,0x03,0x83,0x5C,0x86,0x04,0x00,0x18,0x02,0x83,0x5D,0x86,0x04,0x00}},

	{0x00,1,{0xC0}},             
	{0xCF,10,{0x01,0x01,0x20,0x20,0x00,0x00,0x01,0x01,0x00,0x00}},

	{0x00,1,{0xD0}},              
	{0xCF,1,{0x00}},

	{0x00,1,{0x80}},              
	{0xCB,10,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0x90}},              
	{0xCB,15,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xA0}},              
	{0xCB,15,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xB0}},              
	{0xCB,10,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xC0}},              
	{0xCB,15,{0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xD0}},              
	{0xCB,15,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xE0}},              
	{0xCB,10,{0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xF0}},              
	{0xCB,10,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},

	{0x00,1,{0x80}},              
	{0xCC,10,{0x00,0x26,0x09,0x0B,0x01,0x25,0x00,0x00,0x00,0x00}},

	{0x00,1,{0x90 }},             
	{0xCC,15,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xA0}},              
	{0xCC,15,{0x00,0x00,0x00,0x00,0x00,0x25,0x02,0x0C,0x0A,0x26,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xB0}},              
	{0xCC,10,{0x00,0x25,0x0A,0x0C,0x02,0x26,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xC0}},              
	{0xCC,15,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1,{0xD0}},              
	{0xCC,15,{0x00,0x00,0x00,0x00,0x00,0x26,0x01,0x0B,0x09,0x25,0x00,0x00,0x00,0x00,0x00}},


  {0x00,1,{0x90}}, 
  {0xC0,6,{0x00,0x15,0x00,0x00,0x00,0x04}}, 

	{0x00,1,{0x80}}, 
	{0xC1,3,{0x03,0x33,0x84}}, 

	{0x00,1,{0xB0}}, 
	{0xC1,6,{0x20,0x00,0x00,0x00,0x02,0x01}}, 

	{0x00,1,{0x90}}, 
	{0xB6,2,{0xB4,0x5A}}, 

	//{0x00,1,{0x83}}, 
	//{0xC5,1,{0x01}}, 
	{0x00,1,{0x00}},             
	{0xFB,1,{0x01}},

	{0x00,1,{0x00}},              
	{0xFF,3,{0xFF,0xFF,0xFF}},

	 {0x11,1,   {0x00}}, 
	 {REGFLAG_DELAY, 200, {}},
	 {0x29,1,	{0x00}}, 
	 {REGFLAG_DELAY, 20, {}},

//	{0x2C,	1,	{0x00}},	// Setting ending by predefined flag
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};



static struct LCM_setting_table lcm_set_window[] = {
	{0x2A,	4,	{0x00, 0x00, (FRAME_WIDTH>>8), (FRAME_WIDTH&0xFF)}},
	{0x2B,	4,	{0x00, 0x00, (FRAME_HEIGHT>>8), (FRAME_HEIGHT&0xFF)}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_sleep_out_setting[] = {
    // Sleep Out
	{0x11, 1, {0x00}},
    {REGFLAG_DELAY, 50, {}},

    // Display ON
	{0x29, 1, {0x00}},
	{REGFLAG_DELAY, 20, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
	// Display off sequence
	{0x28, 1, {0x00}},
	{REGFLAG_DELAY, 50, {}},

    // Sleep Mode On
	{0x10, 1, {0x00}},
	{REGFLAG_DELAY, 20, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_compare_id_setting[] = {

	{0x00,	1,	{0x00}},
	{0xff,	3,	{0x80,0x09,0x01}}, 
	{REGFLAG_DELAY, 10, {}},

	{0x00,	1,	{0x80}},
	{0xff,	2,	{0x80,0x09}}, 
	{REGFLAG_DELAY, 10, {}},
	
        {0x00,  1,      {0xC6}},
        {0xB0,  1,      {0x03}},
        
	{0x00,	1,	{0x02}},

	{REGFLAG_END_OF_TABLE, 0x00, {}}

};


static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;

    for(i = 0; i < count; i++) {
		
        unsigned cmd;
        cmd = table[i].cmd;
		
        switch (cmd) {
			
            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;
				
            case REGFLAG_END_OF_TABLE :
                break;
				
            default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
       	}
    }
	
}


// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
		memset(params, 0, sizeof(LCM_PARAMS));
	
		params->type   = LCM_TYPE_DSI;

		params->width  = FRAME_WIDTH;
		params->height = FRAME_HEIGHT;

		// enable tearing-free
		//params->dbi.te_mode 				= LCM_DBI_TE_MODE_VSYNC_ONLY;
		params->dbi.te_mode 				= LCM_DBI_TE_MODE_DISABLED;
		//params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;

#if (LCM_DSI_CMD_MODE)
		params->dsi.mode   = CMD_MODE;
#else
		params->dsi.mode   = SYNC_PULSE_VDO_MODE;
#endif
	
	
		// DSI
		/* Command mode setting */
		params->dsi.LANE_NUM				= LCM_TWO_LANE;
		//The following defined the fomat for data coming from LCD engine.
		params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
		params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
		params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
		params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

		// Highly depends on LCD driver capability.
		// Not support in MT6573
		params->dsi.packet_size=256;

		// Video mode setting		
		params->dsi.intermediat_buffer_num = 2;

		params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
		params->dsi.word_count=480*3;

 //edit by Magnum 2013-7-25 , solve esd read id error
	//	 cycle_time = (4 * 1000 * div2 * div1 * pre_div * post_div)/ (fbk_sel * (fbk_div+0x01) * 26) + 
	// 1 = 
  // ui = (1000 * div2 * div1 * pre_div * post_div)/ (fbk_sel * (fbk_div+0x01) * 26 * 2) + 1;
		
		
		params->dsi.vertical_sync_active				= 4;
		params->dsi.vertical_backporch					= 15;
		params->dsi.vertical_frontporch					= 15;
		params->dsi.vertical_active_line				= FRAME_HEIGHT; 

		params->dsi.horizontal_sync_active				= 6;
		params->dsi.horizontal_backporch				= 44;
		params->dsi.horizontal_frontporch				= 40;
		params->dsi.horizontal_active_pixel				= FRAME_WIDTH;
		params->dsi.compatibility_for_nvk = 0;	
		params->dsi.ssc_disable=0;
		params->dsi.ssc_range=6;
	       params->dsi.PLL_CLOCK				= 202;
		params->dsi.esd_check_enable = 1;
		params->dsi.customization_esd_check_enable = 1; 
		params->dsi.lcm_esd_check_table[0].cmd          = 0x0A;
		params->dsi.lcm_esd_check_table[0].count        = 1;
		params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
		/* ESD or noise interference recovery For video mode LCM only. */
		// Send TE packet to LCM in a period of n frames and check the response.
	/*	params->dsi.lcm_int_te_monitor = FALSE;
		params->dsi.lcm_int_te_period = 1;		// Unit : frames

		// Need longer FP for more opportunity to do int. TE monitor applicably.
		if(params->dsi.lcm_int_te_monitor)
			params->dsi.vertical_frontporch *= 2;
		
		// Monitor external TE (or named VSYNC) from LCM once per 2 sec. (LCM VSYNC must be wired to baseband TE pin.)
		params->dsi.lcm_ext_te_monitor = FALSE;
		// Non-continuous clock
		params->dsi.noncont_clock = TRUE;
		params->dsi.noncont_clock_period = 2;	// Unit : frames  */
}


static void lcm_init(void)
{
	LCM_DBG(" Magnum lcm_init...\n");
	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(50);
	SET_RESET_PIN(1);
	MDELAY(100);

	push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}


static void lcm_suspend(void)
{
	esdSwitch = 0;
	push_table(lcm_deep_sleep_mode_in_setting, sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(20);
	
}

static struct LCM_setting_table read_protect[] = {
	{0x00,	1,	{0x00}},
       {0xff,	3,	{0x80, 0x09, 0x01}},       	     	  			
	{0x00,	1,	{0x80}},
       {0xff,	2,	{0x80, 0x09}},

        {0x00,  1,      {0xC6}},
        {0xB0,  1,      {0x03}},
};


static void lcm_resume(void)
{
	esdSwitch = 1;
	lcm_init();
	//lcm_adc_read_chip_id();
	//push_table(lcm_sleep_out_setting, sizeof(lcm_sleep_out_setting) / sizeof(struct LCM_setting_table), 1);
}

#include "cust_adc.h"
#define LCM_MAX_VOLTAGE 290
#define LCM_MIN_VOLTAGE  0

extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);
#if 0
static unsigned int lcm_adc_read_chip_id()
{	
	LCM_DBG("read lcm_adc_read_chip_id\n");
	int data[4] = {0, 0, 0, 0};
	int tmp = 0, rc = 0, iVoltage = -1;
	rc = IMM_GetOneChannelValue(AUXADC_LCD_ID_CHANNEL, data, &tmp);
	if(rc < 0) {
		LCM_DBG("read LCD_ID vol erro\n");
		return 0;
	}
	else {
		iVoltage = (data[0]*1000) + (data[1]*10) + (data[2]);
		LCM_DBG("read LCD_ID success, data[0]=%d, data[1]=%d, data[2]=%d, data[3]=%d, iVoltage=%d\n", 
			data[0], data[1], data[2], data[3], iVoltage);
		if(LCM_MIN_VOLTAGE < iVoltage &&
			iVoltage < LCM_MAX_VOLTAGE)
			return 1;
		else
			return 0;
	}
}
#endif
static unsigned int lcm_compare_id(void)
{
	int array[4];
	char buffer[5];
	char id_high=0;
	char id_low=0;
	int id=0;

	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(50);
	SET_RESET_PIN(1);
	MDELAY(100);

	push_table(lcm_compare_id_setting, sizeof(lcm_compare_id_setting) / sizeof(struct LCM_setting_table), 1);

	array[0] = 0x00023700;// set return byte number
	dsi_set_cmdq(array, 1, 1);

	read_reg_V2(0xD2, &buffer, 2);
	LCM_DBG("buffer0 == 0x%02x, buffer1 == 0x%02x \n",buffer[0],buffer[1]);

	id = buffer[0]<<8 |buffer[1]; 
	LCM_DBG("OTM8018B 0x%x , 0x%x , 0x%x \n",buffer[0],buffer[1],id);
	//return (id == LCM_ID_OTM8018B)?1:0;
	if(LCM_ID_OTM8018B == id){
		#if defined(BUILD_UBOOT) || defined(BUILD_LK)
			//return lcm_adc_read_chip_id();			
		#endif
		return 1;
   	}
	else
		return 0;
}


LCM_DRIVER otm8019a_fwvga_dsi_vdo_boe_lcm_drv = 
{
  .name			= "otm8019a_fwvga_dsi_vdo_boe",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
	.compare_id    = lcm_compare_id,	
#if (LCM_DSI_CMD_MODE)
    .update         = lcm_update,
#endif
};

