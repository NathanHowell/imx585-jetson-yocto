// SPDX-License-Identifier: GPL-2.0
/*
 * Tegra tegracam driver for the Sony IMX585 image sensor.
 */

#include <linux/clk.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/math64.h>

#include <media/camera_common.h>
#include <media/tegra_v4l2_camera.h>
#include <linux/property.h>
#include <media/v4l2-async.h>
#include <media/tegracam_core.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ctrls.h>
#include <linux/videodev2.h>

/* --------------------------------------------------------------------------
 * Sensor register definitions
 * --------------------------------------------------------------------------
 */

#define IMX585_REG_MODE_SELECT          CCI_REG8(0x3000)
#define IMX585_MODE_STANDBY             0x01
#define IMX585_MODE_STREAMING           0x00
#define IMX585_STREAM_DELAY_US          25000
#define IMX585_STREAM_DELAY_RANGE_US    1000

#define IMX585_REG_XMSTA                CCI_REG8(0x3002)
#define IMX585_REG_XXS_DRV              CCI_REG8(0x30a6)
#define IMX585_REG_EXTMODE              CCI_REG8(0x30ce)
#define IMX585_REG_XXS_OUTSEL           CCI_REG8(0x30a4)
#define IMX585_REG_XVSLNG               CCI_REG8(0x30cc)
#define IMX585_REG_XHSLNG               CCI_REG8(0x30cd)

#define IMX585_REG_INCK_SEL             CCI_REG8(0x3014)
#define IMX585_REG_DATARATE_SEL         CCI_REG8(0x3015)
#define IMX585_REG_BIN_MODE             CCI_REG8(0x3019)
#define IMX585_REG_LANEMODE             CCI_REG8(0x3040)

#define IMX585_REG_VMAX                 CCI_REG24_LE(0x3028)
#define IMX585_REG_HMAX                 CCI_REG16_LE(0x302c)
#define IMX585_REG_SHR                  CCI_REG24_LE(0x3050)
#define IMX585_REG_BLACK_LEVEL          CCI_REG16_LE(0x30dc)
#define IMX585_REG_DIGITAL_CLAMP        CCI_REG8(0x3458)
#define IMX585_REG_ANALOG_GAIN          CCI_REG16_LE(0x306c)
/* Analog gain is 0.3 dB per step, 0..240 == 0..72 dB. Past 240 the sensor moves
 * into digital gain, which is not what a linear-raw pipeline wants applied
 * silently, so set_gain() clamps here. */
#define IMX585_ANALOG_GAIN_MAX          240
/* ClearHDR companding costs analog headroom: the ceiling drops to 80 steps
 * (24 dB) from 240 (72 dB). Same split as the pre-conversion driver's
 * IMX585_ANA_GAIN_MAX_HDR / _NORMAL. */
#define IMX585_ANALOG_GAIN_MAX_HDR      80
/* Group hold: writing 1 defers exposure/gain register updates until it is
 * cleared, so a frame cannot be captured with exposure from one setting and gain
 * from the next. */
#define IMX585_REG_REGHOLD              CCI_REG8(0x3001)
#define IMX585_REG_FDG_SEL0             CCI_REG8(0x3030)
#define IMX585_BLKLEVEL_DEFAULT         50

#define IMX585_REG_MODEL_ID_MSB         CCI_REG8(0x0016)
#define IMX585_REG_MODEL_ID_LSB         CCI_REG8(0x0017)

#define IMX585_VMAX_DEFAULT             2250
#define IMX585_VMAX_MAX                 0xfffff
#define IMX585_HMAX_MAX                 0xffff
#define IMX585_SHR_MIN                  8
#define IMX585_SHR_MIN_HDR              10
#define IMX585_PIXEL_RATE               74250000U

/* Native array */
#define IMX585_NATIVE_WIDTH             3856U
#define IMX585_NATIVE_HEIGHT            2180U
#define IMX585_ARRAY_LEFT               8U
#define IMX585_ARRAY_TOP                8U
#define IMX585_ARRAY_WIDTH              3840U
#define IMX585_ARRAY_HEIGHT             2160U
#define IMX585_MCLK_FREQ                24000000U

enum imx585_link_freq {
	IMX585_LINK_FREQ_297MHZ,
	IMX585_LINK_FREQ_360MHZ,
	IMX585_LINK_FREQ_445MHZ,
	IMX585_LINK_FREQ_594MHZ,
	IMX585_LINK_FREQ_720MHZ,
	IMX585_LINK_FREQ_891MHZ,
	IMX585_LINK_FREQ_1039MHZ,
	IMX585_LINK_FREQ_1188MHZ,
};

static const u8 imx585_link_freq_regval[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 0x07,
	[IMX585_LINK_FREQ_360MHZ]  = 0x06,
	[IMX585_LINK_FREQ_445MHZ]  = 0x05,
	[IMX585_LINK_FREQ_594MHZ]  = 0x04,
	[IMX585_LINK_FREQ_720MHZ]  = 0x03,
	[IMX585_LINK_FREQ_891MHZ]  = 0x02,
	[IMX585_LINK_FREQ_1039MHZ] = 0x01,
	[IMX585_LINK_FREQ_1188MHZ] = 0x00,
};

static const u64 imx585_link_freq_table[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 297000000ULL,
	[IMX585_LINK_FREQ_360MHZ]  = 360000000ULL,
	[IMX585_LINK_FREQ_445MHZ]  = 445500000ULL,
	[IMX585_LINK_FREQ_594MHZ]  = 594000000ULL,
	[IMX585_LINK_FREQ_720MHZ]  = 720000000ULL,
	[IMX585_LINK_FREQ_891MHZ]  = 891000000ULL,
	[IMX585_LINK_FREQ_1039MHZ] = 1039500000ULL,
	[IMX585_LINK_FREQ_1188MHZ] = 1188000000ULL,
};

static const u16 imx585_min_hmax_4lane_4k[] = {
	[IMX585_LINK_FREQ_297MHZ]  = 1584,
	[IMX585_LINK_FREQ_360MHZ]  = 1320,
	[IMX585_LINK_FREQ_445MHZ]  = 1100,
	[IMX585_LINK_FREQ_594MHZ]  = 792,
	[IMX585_LINK_FREQ_720MHZ]  = 660,
	[IMX585_LINK_FREQ_891MHZ]  = 550,
	[IMX585_LINK_FREQ_1039MHZ] = 440,
	[IMX585_LINK_FREQ_1188MHZ] = 396,
};

struct imx585_inck_cfg {
	u32 xclk_hz;
	u8  inck_sel;
};

static const struct imx585_inck_cfg imx585_inck_table[] = {
	{ 74250000, 0x00 },
	{ 37125000, 0x01 },
	{ 72000000, 0x02 },
	{ 27000000, 0x03 },
	{ 24000000, 0x04 },
};

/* --------------------------------------------------------------------------
 * Mode descriptions
 * --------------------------------------------------------------------------
 */

struct imx585_mode {
	u32 width;
	u32 height;
	u8 hmax_div;
	u32 default_vmax;
	const struct cci_reg_sequence *reg_list;
	u32 reg_list_length;
};

/* Register tables copied from the original V4L2 driver */
static const struct cci_reg_sequence imx585_common_regs[] = {
	{ CCI_REG8(0x3002), 0x01 },
	{ CCI_REG8(0x3069), 0x00 },
	{ CCI_REG8(0x3074), 0x64 },
	{ CCI_REG8(0x30d5), 0x04 },
	{ CCI_REG8(0x3030), 0x00 },
	{ CCI_REG8(0x30a6), 0x00 },
	{ CCI_REG8(0x3081), 0x00 },
	{ CCI_REG8(0x303a), 0x03 },
	{ CCI_REG8(0x3460), 0x21 },
	{ CCI_REG8(0x3478), 0xa1 },
	{ CCI_REG8(0x347c), 0x01 },
	{ CCI_REG8(0x3480), 0x01 },
	{ CCI_REG8(0x3a4e), 0x14 },
	{ CCI_REG8(0x3a52), 0x14 },
	{ CCI_REG8(0x3a56), 0x00 },
	{ CCI_REG8(0x3a5a), 0x00 },
	{ CCI_REG8(0x3a5e), 0x00 },
	{ CCI_REG8(0x3a62), 0x00 },
	{ CCI_REG8(0x3a6a), 0x20 },
	{ CCI_REG8(0x3a6c), 0x42 },
	{ CCI_REG8(0x3a6e), 0xa0 },
	{ CCI_REG8(0x3b2c), 0x0c },
	{ CCI_REG8(0x3b30), 0x1c },
	{ CCI_REG8(0x3b34), 0x0c },
	{ CCI_REG8(0x3b38), 0x1c },
	{ CCI_REG8(0x3ba0), 0x0c },
	{ CCI_REG8(0x3ba4), 0x1c },
	{ CCI_REG8(0x3ba8), 0x0c },
	{ CCI_REG8(0x3bac), 0x1c },
	{ CCI_REG8(0x3d3c), 0x11 },
	{ CCI_REG8(0x3d46), 0x0b },
	{ CCI_REG8(0x3de0), 0x3f },
	{ CCI_REG8(0x3de1), 0x08 },
	{ CCI_REG8(0x3e14), 0x87 },
	{ CCI_REG8(0x3e16), 0x91 },
	{ CCI_REG8(0x3e18), 0x91 },
	{ CCI_REG8(0x3e1a), 0x87 },
	{ CCI_REG8(0x3e1c), 0x78 },
	{ CCI_REG8(0x3e1e), 0x50 },
	{ CCI_REG8(0x3e20), 0x50 },
	{ CCI_REG8(0x3e22), 0x50 },
	{ CCI_REG8(0x3e24), 0x87 },
	{ CCI_REG8(0x3e26), 0x91 },
	{ CCI_REG8(0x3e28), 0x91 },
	{ CCI_REG8(0x3e2a), 0x87 },
	{ CCI_REG8(0x3e2c), 0x78 },
	{ CCI_REG8(0x3e2e), 0x50 },
	{ CCI_REG8(0x3e30), 0x50 },
	{ CCI_REG8(0x3e32), 0x50 },
	{ CCI_REG8(0x3e34), 0x87 },
	{ CCI_REG8(0x3e36), 0x91 },
	{ CCI_REG8(0x3e38), 0x91 },
	{ CCI_REG8(0x3e3a), 0x87 },
	{ CCI_REG8(0x3e3c), 0x78 },
	{ CCI_REG8(0x3e3e), 0x50 },
	{ CCI_REG8(0x3e40), 0x50 },
	{ CCI_REG8(0x3e42), 0x50 },
	{ CCI_REG8(0x4054), 0x64 },
	{ CCI_REG8(0x4148), 0xfe },
	{ CCI_REG8(0x4149), 0x05 },
	{ CCI_REG8(0x414a), 0xff },
	{ CCI_REG8(0x414b), 0x05 },
	{ CCI_REG8(0x420a), 0x03 },
	{ CCI_REG8(0x4231), 0x08 },
	{ CCI_REG8(0x423d), 0x9c },
	{ CCI_REG8(0x4242), 0xb4 },
	{ CCI_REG8(0x4246), 0xb4 },
	{ CCI_REG8(0x424e), 0xb4 },
	{ CCI_REG8(0x425c), 0xb4 },
	{ CCI_REG8(0x425e), 0xb6 },
	{ CCI_REG8(0x426c), 0xb4 },
	{ CCI_REG8(0x426e), 0xb6 },
	{ CCI_REG8(0x428c), 0xb4 },
	{ CCI_REG8(0x428e), 0xb6 },
	{ CCI_REG8(0x4708), 0x00 },
	{ CCI_REG8(0x4709), 0x00 },
	{ CCI_REG8(0x470a), 0xff },
	{ CCI_REG8(0x470b), 0x03 },
	{ CCI_REG8(0x470c), 0x00 },
	{ CCI_REG8(0x470d), 0x00 },
	{ CCI_REG8(0x470e), 0xff },
	{ CCI_REG8(0x470f), 0x03 },
	{ CCI_REG8(0x47eb), 0x1c },
	{ CCI_REG8(0x47f0), 0xa6 },
	{ CCI_REG8(0x47f2), 0xa6 },
	{ CCI_REG8(0x47f4), 0xa0 },
	{ CCI_REG8(0x47f6), 0x96 },
	{ CCI_REG8(0x4808), 0xa6 },
	{ CCI_REG8(0x480a), 0xa6 },
	{ CCI_REG8(0x480c), 0xa0 },
	{ CCI_REG8(0x480e), 0x96 },
	{ CCI_REG8(0x492c), 0xb2 },
	{ CCI_REG8(0x4930), 0x03 },
	{ CCI_REG8(0x4932), 0x03 },
	{ CCI_REG8(0x4936), 0x5b },
	{ CCI_REG8(0x4938), 0x82 },
	{ CCI_REG8(0x493e), 0x23 },
	{ CCI_REG8(0x4ba8), 0x1c },
	{ CCI_REG8(0x4ba9), 0x03 },
	{ CCI_REG8(0x4bac), 0x1c },
	{ CCI_REG8(0x4bad), 0x1c },
	{ CCI_REG8(0x4bae), 0x1c },
	{ CCI_REG8(0x4baf), 0x1c },
	{ CCI_REG8(0x4bb0), 0x1c },
	{ CCI_REG8(0x4bb1), 0x1c },
	{ CCI_REG8(0x4bb2), 0x1c },
	{ CCI_REG8(0x4bb3), 0x1c },
	{ CCI_REG8(0x4bb4), 0x1c },
	{ CCI_REG8(0x4bb8), 0x03 },
	{ CCI_REG8(0x4bb9), 0x03 },
	{ CCI_REG8(0x4bba), 0x03 },
	{ CCI_REG8(0x4bbb), 0x03 },
	{ CCI_REG8(0x4bbc), 0x03 },
	{ CCI_REG8(0x4bbd), 0x03 },
	{ CCI_REG8(0x4bbe), 0x03 },
	{ CCI_REG8(0x4bbf), 0x03 },
	{ CCI_REG8(0x4bc0), 0x03 },
	/* Sony's undocumented "shall be set to this value" calibration registers.
	 * Kept two per line, matching will127534's driver, so the block can be
	 * diffed against his tables -- he remains the reference for the values. */
	{ CCI_REG8(0x4c14), 0x87 },
	{ CCI_REG8(0x4c16), 0x91 }, { CCI_REG8(0x4c18), 0x91 },
	{ CCI_REG8(0x4c1a), 0x87 }, { CCI_REG8(0x4c1c), 0x78 },
	{ CCI_REG8(0x4c1e), 0x50 }, { CCI_REG8(0x4c20), 0x50 },
	{ CCI_REG8(0x4c22), 0x50 }, { CCI_REG8(0x4c24), 0x87 },
	{ CCI_REG8(0x4c26), 0x91 }, { CCI_REG8(0x4c28), 0x91 },
	{ CCI_REG8(0x4c2a), 0x87 }, { CCI_REG8(0x4c2c), 0x78 },
	{ CCI_REG8(0x4c2e), 0x50 }, { CCI_REG8(0x4c30), 0x50 },
	{ CCI_REG8(0x4c32), 0x50 }, { CCI_REG8(0x4c34), 0x87 },
	{ CCI_REG8(0x4c36), 0x91 }, { CCI_REG8(0x4c38), 0x91 },
	{ CCI_REG8(0x4c3a), 0x87 }, { CCI_REG8(0x4c3c), 0x78 },
	{ CCI_REG8(0x4c3e), 0x50 }, { CCI_REG8(0x4c40), 0x50 },
	{ CCI_REG8(0x4c42), 0x50 }, { CCI_REG8(0x4d12), 0x1f },
	{ CCI_REG8(0x4d13), 0x1e }, { CCI_REG8(0x4d26), 0x33 },
	{ CCI_REG8(0x4e0e), 0x59 }, { CCI_REG8(0x4e14), 0x55 },
	{ CCI_REG8(0x4e16), 0x59 }, { CCI_REG8(0x4e1e), 0x3b },
	{ CCI_REG8(0x4e20), 0x47 }, { CCI_REG8(0x4e22), 0x54 },
	{ CCI_REG8(0x4e26), 0x81 }, { CCI_REG8(0x4e2c), 0x7d },
	{ CCI_REG8(0x4e2e), 0x81 }, { CCI_REG8(0x4e36), 0x63 },
	{ CCI_REG8(0x4e38), 0x6f }, { CCI_REG8(0x4e3a), 0x7c },
	{ CCI_REG8(0x4f3a), 0x3c }, { CCI_REG8(0x4f3c), 0x46 },
	{ CCI_REG8(0x4f3e), 0x59 }, { CCI_REG8(0x4f42), 0x64 },
	{ CCI_REG8(0x4f44), 0x6e }, { CCI_REG8(0x4f46), 0x81 },
	{ CCI_REG8(0x4f4a), 0x82 }, { CCI_REG8(0x4f5a), 0x81 },
	{ CCI_REG8(0x4f62), 0xaa }, { CCI_REG8(0x4f72), 0xa9 },
	{ CCI_REG8(0x4f78), 0x36 }, { CCI_REG8(0x4f7a), 0x41 },
	{ CCI_REG8(0x4f7c), 0x61 }, { CCI_REG8(0x4f7d), 0x01 },
	{ CCI_REG8(0x4f7e), 0x7c }, { CCI_REG8(0x4f7f), 0x01 },
	{ CCI_REG8(0x4f80), 0x77 }, { CCI_REG8(0x4f82), 0x7b },
	{ CCI_REG8(0x4f88), 0x37 }, { CCI_REG8(0x4f8a), 0x40 },
	{ CCI_REG8(0x4f8c), 0x62 }, { CCI_REG8(0x4f8d), 0x01 },
	{ CCI_REG8(0x4f8e), 0x76 }, { CCI_REG8(0x4f8f), 0x01 },
	{ CCI_REG8(0x4f90), 0x5e }, { CCI_REG8(0x4f91), 0x02 },
	{ CCI_REG8(0x4f92), 0x69 }, { CCI_REG8(0x4f93), 0x02 },
	{ CCI_REG8(0x4f94), 0x89 }, { CCI_REG8(0x4f95), 0x02 },
	{ CCI_REG8(0x4f96), 0xa4 }, { CCI_REG8(0x4f97), 0x02 },
	{ CCI_REG8(0x4f98), 0x9f }, { CCI_REG8(0x4f99), 0x02 },
	{ CCI_REG8(0x4f9a), 0xa3 }, { CCI_REG8(0x4f9b), 0x02 },
	{ CCI_REG8(0x4fa0), 0x5f }, { CCI_REG8(0x4fa1), 0x02 },
	{ CCI_REG8(0x4fa2), 0x68 }, { CCI_REG8(0x4fa3), 0x02 },
	{ CCI_REG8(0x4fa4), 0x8a }, { CCI_REG8(0x4fa5), 0x02 },
	{ CCI_REG8(0x4fa6), 0x9e }, { CCI_REG8(0x4fa7), 0x02 },
	{ CCI_REG8(0x519e), 0x79 }, { CCI_REG8(0x51a6), 0xa1 },
	{ CCI_REG8(0x51f0), 0xac }, { CCI_REG8(0x51f2), 0xaa },
	{ CCI_REG8(0x51f4), 0xa5 }, { CCI_REG8(0x51f6), 0xa0 },
	{ CCI_REG8(0x5200), 0x9b }, { CCI_REG8(0x5202), 0x91 },
	{ CCI_REG8(0x5204), 0x87 }, { CCI_REG8(0x5206), 0x82 },
	{ CCI_REG8(0x5208), 0xac }, { CCI_REG8(0x520a), 0xaa },
	{ CCI_REG8(0x520c), 0xa5 }, { CCI_REG8(0x520e), 0xa0 },
	{ CCI_REG8(0x5210), 0x9b }, { CCI_REG8(0x5212), 0x91 },
	{ CCI_REG8(0x5214), 0x87 }, { CCI_REG8(0x5216), 0x82 },
	{ CCI_REG8(0x5218), 0xac }, { CCI_REG8(0x521a), 0xaa },
	{ CCI_REG8(0x521c), 0xa5 }, { CCI_REG8(0x521e), 0xa0 },
	{ CCI_REG8(0x5220), 0x9b }, { CCI_REG8(0x5222), 0x91 },
	{ CCI_REG8(0x5224), 0x87 }, { CCI_REG8(0x5226), 0x82 },
};

static const struct cci_reg_sequence imx585_clearhdr_regs[] = {
	{ CCI_REG8(0x301a), 0x10 },
	{ CCI_REG8(0x3024), 0x02 },
	{ CCI_REG8(0x3069), 0x02 },
	{ CCI_REG8(0x3074), 0x63 },
	{ CCI_REG8(0x3930), 0xe6 },
	{ CCI_REG8(0x3931), 0x00 },
	{ CCI_REG8(0x3a4c), 0x61 },
	{ CCI_REG8(0x3a4d), 0x02 },
	{ CCI_REG8(0x3a50), 0x70 },
	{ CCI_REG8(0x3a51), 0x02 },
	{ CCI_REG8(0x3e10), 0x17 },
	{ CCI_REG8(0x493c), 0x41 },
	{ CCI_REG8(0x4940), 0x41 },
	{ CCI_REG8(0x3081), 0x02 },
};

static const struct cci_reg_sequence imx585_normal_regs[] = {
	{ CCI_REG8(0x301a), 0x00 },
	{ CCI_REG8(0x3024), 0x00 },
	{ CCI_REG8(0x3069), 0x00 },
	{ CCI_REG8(0x3074), 0x64 },
	{ CCI_REG8(0x3930), 0x0c },
	{ CCI_REG8(0x3931), 0x01 },
	{ CCI_REG8(0x3a4c), 0x39 },
	{ CCI_REG8(0x3a4d), 0x01 },
	{ CCI_REG8(0x3a50), 0x48 },
	{ CCI_REG8(0x3a51), 0x01 },
	{ CCI_REG8(0x3e10), 0x10 },
	{ CCI_REG8(0x493c), 0x23 },
	{ CCI_REG8(0x4940), 0x23 },
};

static const struct cci_reg_sequence imx585_mode_4k_regs[] = {
	{ CCI_REG8(0x301b), 0x00 },
	{ CCI_REG8(0x3022), 0x02 },
	{ CCI_REG8(0x3023), 0x01 },
	{ CCI_REG8(0x30d5), 0x04 },
};

static const struct cci_reg_sequence imx585_mode_fhd_regs[] = {
	{ CCI_REG8(0x301b), 0x01 },
	{ CCI_REG8(0x3022), 0x02 },
	{ CCI_REG8(0x3023), 0x01 },
	{ CCI_REG8(0x30d5), 0x02 },
};

static const struct imx585_mode imx585_modes[] = {
	{
		.width = 1928,
		.height = 1090,
		.hmax_div = 1,
		.default_vmax = IMX585_VMAX_DEFAULT,
		.reg_list = imx585_mode_fhd_regs,
		.reg_list_length = ARRAY_SIZE(imx585_mode_fhd_regs),
	},
	{
		.width = 3856,
		.height = 2180,
		.hmax_div = 1,
		.default_vmax = IMX585_VMAX_DEFAULT,
		.reg_list = imx585_mode_4k_regs,
		.reg_list_length = ARRAY_SIZE(imx585_mode_4k_regs),
	},
};

/* 50 fps, not 60. At our 720 MHz link frequency the 4-lane line rate is
 * 74.25 MHz / HMAX = 74250000 / 660 = 112500 lines/s, and with VMAX 2250 that is
 * exactly 50.0 fps -- the same number imx585_populate_sensor_mode_props() now
 * derives. 60 fps would need ~1782 Mbps/lane, above the 1440 Mbps the DT selects.
 *
 * Both modes share this because both carry hmax_div = 1, so imx585_get_min_hmax()
 * returns the same HMAX for each. The binned 1928x1090 mode can in principle read
 * out faster; the min-HMAX table does not model that, so it is not claimed here.
 */
static const int imx585_50fps[] = { 50 };

static const struct camera_common_frmfmt imx585_frmfmt[] = {
	{{1928, 1090}, imx585_50fps, 1, 0, 0},
	{{3856, 2180}, imx585_50fps, 1, 0, 1},
};

/* --------------------------------------------------------------------------
 * Driver state
 * --------------------------------------------------------------------------
 */

static const char * const imx585_supply_names[] = {
	"vana",
	"vdig",
	"vddl",
};

#define IMX585_NUM_SUPPLIES ARRAY_SIZE(imx585_supply_names)

struct imx585 {
	struct device *dev;
	struct i2c_client *client;
	struct tegracam_device *tc_dev;
	struct camera_common_data *s_data;
	struct regmap *regmap;

	struct clk *xclk;
	struct regulator_bulk_data supplies[IMX585_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;

	u32 lane_count;
	enum imx585_link_freq link_freq_idx;
	u8 inck_sel;

	u32 hmax;
	u32 vmax;

	bool clear_hdr;

	/* Sub-notifier for the lens-focus actuator, NULL when none is described.
	 * See imx585_register_lens_notifier(). */
	struct v4l2_async_notifier *lens_nf;

	struct mutex lock; /* protects streaming state */
};

static const struct regmap_config imx585_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

/* --------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------
 */

static inline struct imx585 *tegracam_to_imx585(struct tegracam_device *tc_dev)
{
	struct imx585 *priv;

	priv = (struct imx585 *)tegracam_get_privdata(tc_dev);
	if (priv)
		return priv;

	return i2c_get_clientdata(tc_dev->client);
}

static inline u32 imx585_get_min_hmax(const struct imx585 *priv,
				       const struct imx585_mode *mode)
{
	u32 base = imx585_min_hmax_4lane_4k[priv->link_freq_idx];
	u32 scale = (priv->lane_count == 2) ? 2 : 1;
	/* ClearHDR reads the pixel array twice per frame, so the line period
	 * doubles -- as does VMAX, applied in populate_sensor_mode_props(). */
	u32 hdr_scale = priv->clear_hdr ? 2 : 1;

	return (base * scale * hdr_scale) / mode->hmax_div;
}

static int imx585_write_table(struct imx585 *priv,
				 const struct cci_reg_sequence *table,
				 unsigned int count)
{
	return cci_multi_reg_write(priv->regmap, table, count, NULL);
}

static int imx585_power_on(struct camera_common_data *s_data)
{
	struct tegracam_device *tc_dev = to_tegracam_device(s_data);
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	int err;

	err = regulator_bulk_enable(IMX585_NUM_SUPPLIES, priv->supplies);
	if (err)
		return err;

	err = clk_prepare_enable(priv->xclk);
	if (err)
		goto disable_regulators;

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(2000, 5000);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	}

	usleep_range(IMX585_STREAM_DELAY_US,
		     IMX585_STREAM_DELAY_US + IMX585_STREAM_DELAY_RANGE_US);

	return 0;

disable_regulators:
	regulator_bulk_disable(IMX585_NUM_SUPPLIES, priv->supplies);
	return err;
}

static int imx585_power_off(struct camera_common_data *s_data)
{
	struct tegracam_device *tc_dev = to_tegracam_device(s_data);
	struct imx585 *priv = tegracam_to_imx585(tc_dev);

	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 0);

	clk_disable_unprepare(priv->xclk);
	regulator_bulk_disable(IMX585_NUM_SUPPLIES, priv->supplies);
	return 0;
}

static int imx585_power_get(struct tegracam_device *tc_dev)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	u32 i;
	int err;

	if (!priv)
		return -EINVAL;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EINVAL;
	}

	for (i = 0; i < IMX585_NUM_SUPPLIES; i++)
		priv->supplies[i].supply = imx585_supply_names[i];

	err = devm_regulator_bulk_get(dev, IMX585_NUM_SUPPLIES, priv->supplies);
	if (err)
		return err;

	priv->xclk = devm_clk_get(dev, pdata->mclk_name ? pdata->mclk_name : NULL);
	if (IS_ERR(priv->xclk))
		return PTR_ERR(priv->xclk);

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	return 0;
}

static int imx585_power_put(struct tegracam_device *tc_dev)
{
	return 0;
}

static struct camera_common_pdata *imx585_parse_dt(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *pdata;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	of_property_read_string(np, "mclk", &pdata->mclk_name);
	of_property_read_string(np, "avdd-reg", &pdata->regulators.avdd);
	of_property_read_string(np, "dvdd-reg", &pdata->regulators.dvdd);
	of_property_read_string(np, "iovdd-reg", &pdata->regulators.iovdd);

	pdata->reset_gpio = of_get_named_gpio(np, "reset-gpios", 0);

	return pdata;
}

static int imx585_select_link_freq(struct imx585 *priv, u64 link_freq)
{
	enum imx585_link_freq idx;

	for (idx = 0; idx < ARRAY_SIZE(imx585_link_freq_table); idx++) {
		if (imx585_link_freq_table[idx] == link_freq) {
			priv->link_freq_idx = idx;
			return 0;
		}
	}

	return -EINVAL;
}

static int imx585_parse_endpoint(struct imx585 *priv)
{
	struct device *dev = priv->dev;
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	int ret = -EINVAL;


	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return -EINVAL;

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep))
		goto out_put;

	if (ep.bus.mipi_csi2.num_data_lanes != 2 &&
	    ep.bus.mipi_csi2.num_data_lanes != 4)
		goto out_free;

	priv->lane_count = ep.bus.mipi_csi2.num_data_lanes;

	if (!ep.nr_of_link_frequencies)
		goto out_free;

	ret = imx585_select_link_freq(priv, ep.link_frequencies[0]);

out_free:
	v4l2_fwnode_endpoint_free(&ep);
out_put:
	fwnode_handle_put(endpoint);
	return ret;
}

static void imx585_update_timing(struct imx585 *priv,
				 const struct imx585_mode *mode)
{
	priv->hmax = imx585_get_min_hmax(priv, mode);
	priv->vmax = mode->default_vmax;
}

static void imx585_populate_sensor_mode_props(struct imx585 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct sensor_properties *props = &s_data->sensor_props;
	/* RAW12 normally; ClearHDR emits companded 16-bit. */
	const u32 bpp = priv->clear_hdr ? 16 : 12;
	u32 num_modes;
	u32 i;

	if (!props->sensor_modes)
		return;

	num_modes = min(props->num_modes, (u32)ARRAY_SIZE(imx585_modes));

	for (i = 0; i < num_modes; i++) {
		struct sensor_mode_properties *dst = &props->sensor_modes[i];
		const struct imx585_mode *src = &imx585_modes[i];
		u32 hmax = imx585_get_min_hmax(priv, src);
		u64 link_freq = imx585_link_freq_table[priv->link_freq_idx];
		u64 pixel_clock = div_u64(link_freq * 2ULL * priv->lane_count, bpp);
		/* VMAX doubles in ClearHDR, matching the HMAX doubling in
		 * imx585_get_min_hmax(). */
		u64 frame_length = (u64)src->default_vmax *
				   (priv->clear_hdr ? 2 : 1);
		u64 frame_time_us;
		u64 min_exp_us;
		u64 max_exp_us;
		u64 default_exp_us;
		u64 max_fps_q6;
		u64 line_length;

		if (!hmax || !frame_length)
			continue;

		/* HMAX is expressed in IMX585_PIXEL_RATE (74.25 MHz) clock ticks,
		 * NOT in CSI pixel-clock ticks, so the line rate is
		 *
		 *     lines/s = IMX585_PIXEL_RATE / HMAX
		 *
		 * This is how will127534's driver derives it
		 * (pixel_rate = width * IMX585_PIXEL_RATE / min_hmax), and it is
		 * confirmed by Kurokesu's working Jetson DT: their 360 MHz-link
		 * mode pairs pix_clk_hz = 237600000 with line_length = 4224, and
		 * 237600000 / 4224 == 74250000 / 1320 == 56250 lines/s, where 1320
		 * is will's min HMAX at that link frequency.
		 *
		 * Dividing by pixel_clock here instead would inflate every derived
		 * timing by pixel_clock / 74.25 MHz -- a factor of 6.46 at a 720 MHz
		 * link.
		 */
		frame_time_us = div_u64((u64)hmax * frame_length * 1000000ULL,
					IMX585_PIXEL_RATE);
		min_exp_us = div_u64((u64)IMX585_SHR_MIN * hmax * 1000000ULL,
				     IMX585_PIXEL_RATE);
		if (!min_exp_us)
			min_exp_us = 1;
		max_exp_us = max(frame_time_us - min_exp_us, min_exp_us + 1);
		default_exp_us = clamp_t(u64, frame_time_us / 2,
					 min_exp_us, max_exp_us);

		/* framerate_factor is 1000000, so this is micro-fps. At 720 MHz /
		 * 4 lanes this comes out exactly 50000000 (50.0 fps). */
		max_fps_q6 = div_u64(IMX585_PIXEL_RATE * 1000000ULL,
				     (u64)hmax * frame_length);

		/* VI needs the line length in its own pixel-clock domain, so scale
		 * HMAX from the 74.25 MHz domain into it. */
		line_length = div_u64(pixel_clock * hmax, IMX585_PIXEL_RATE);

		dst->signal_properties.num_lanes = priv->lane_count;
		dst->signal_properties.mclk_freq = IMX585_MCLK_FREQ;
		dst->signal_properties.pixel_clock.val = pixel_clock;
		dst->signal_properties.cil_settletime = 0;
		dst->signal_properties.discontinuous_clk = 0;
		dst->signal_properties.dpcm_enable = 0;
		dst->signal_properties.phy_mode = CSI_PHY_MODE_DPHY;

		dst->image_properties.width = src->width;
		dst->image_properties.height = src->height;
		dst->image_properties.line_length = line_length;
		/* ClearHDR is 16-bit, and SBGGR16 is a deliberate compromise: it is
		 * the ONLY 16-bit raw entry in NVIDIA's camera_common colorfmt table
		 * (camera_common.c), which has no SRGGB16. This sensor is RGGB, and
		 * the pre-conversion driver uses MEDIA_BUS_FMT_SRGGB16_1X16, so in
		 * HDR the declared Bayer phase is wrong by one pixel even though the
		 * data is correct. Fixing it properly means patching camera_common
		 * via nvidia-kernel-oot's EXTRA_PATCHES. Irrelevant while clear_hdr
		 * is off, which is the default. See PORTING.md. */
		dst->image_properties.pixel_format = priv->clear_hdr ?
			V4L2_PIX_FMT_SBGGR16 : V4L2_PIX_FMT_SRGGB12;
		/* imx585_common_regs writes 0x303a = 0x03, which disables embedded
		 * data -- the pre-conversion driver carries that write with exactly
		 * that comment, and Kurokesu's working Jetson DT likewise sets
		 * embedded_metadata_height = "0". Promising VI two lines that the
		 * sensor does not send makes it short-count every frame. */
		dst->image_properties.embedded_metadata_height = 0;

		/* Gain is reported in decibels, matching the hardware: IMX585
		 * analog gain is 0.3 dB per step over 0..240 (0..72 dB). With
		 * gain_factor 10 the values here are dB*10, so step 3 == 0.3 dB and
		 * max 720 == 72 dB. Same shape as Kurokesu's working DT. */
		dst->control_properties.gain_factor = 10;
		dst->control_properties.framerate_factor = 1000000;
		dst->control_properties.exposure_factor = 1000000;
		dst->control_properties.inherent_gain = 1;
		dst->control_properties.min_gain_val = 0;
		dst->control_properties.max_gain_val =
			priv->clear_hdr ? (IMX585_ANALOG_GAIN_MAX_HDR * 3)
					: (IMX585_ANALOG_GAIN_MAX * 3);
		dst->control_properties.step_gain_val = 3;
		dst->control_properties.default_gain = 0;
		dst->control_properties.min_hdr_ratio = 1;
		dst->control_properties.max_hdr_ratio = 1;
		dst->control_properties.min_framerate = 1000000;
		dst->control_properties.step_framerate = 1;
		dst->control_properties.max_framerate = min_t(u64, max_fps_q6, U32_MAX);
		dst->control_properties.default_framerate =
			min_t(u64, max_fps_q6, U32_MAX);
		dst->control_properties.min_exp_time.val = min_exp_us;
		dst->control_properties.max_exp_time.val = max_exp_us;
		dst->control_properties.step_exp_time.val = 1;
		dst->control_properties.default_exp_time.val = default_exp_us;
		dst->control_properties.is_interlaced = 0;
		dst->control_properties.interlace_type = 0;
	}
}

static int imx585_mode_init(struct imx585 *priv,
			      const struct imx585_mode *mode)
{
	int err;

	err = imx585_write_table(priv, imx585_common_regs,
				 ARRAY_SIZE(imx585_common_regs));
	if (err)
		return err;

	err = imx585_write_table(priv, mode->reg_list, mode->reg_list_length);
	if (err)
		return err;

	imx585_update_timing(priv, mode);

	err = cci_write(priv->regmap, IMX585_REG_DATARATE_SEL,
			   imx585_link_freq_regval[priv->link_freq_idx], NULL);
	if (err)
		return err;

	err = cci_write(priv->regmap, IMX585_REG_INCK_SEL, priv->inck_sel, NULL);
	if (err)
		return err;

	err = cci_write(priv->regmap, IMX585_REG_LANEMODE,
			   (priv->lane_count == 2) ? 0x01 : 0x03, NULL);
	if (err)
		return err;

	err = cci_write(priv->regmap, IMX585_REG_BIN_MODE,
			   (priv->lane_count == 2) ? 0x01 : 0x00, NULL);
	if (err)
		return err;

	err = cci_write(priv->regmap, IMX585_REG_VMAX, priv->vmax, NULL);
	if (err)
		return err;

	err = cci_write(priv->regmap, IMX585_REG_HMAX, priv->hmax, NULL);
	if (err)
		return err;

	return cci_write(priv->regmap, IMX585_REG_BLACK_LEVEL,
			  IMX585_BLKLEVEL_DEFAULT, NULL);
}

static int imx585_set_mode(struct tegracam_device *tc_dev)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	u32 mode_idx;

	if (s_data->mode < 0 || s_data->mode >= ARRAY_SIZE(imx585_modes))
		return -EINVAL;

	mode_idx = s_data->mode;

	imx585_update_timing(priv, &imx585_modes[mode_idx]);

	return imx585_mode_init(priv, &imx585_modes[mode_idx]);
}

static int imx585_start_streaming(struct tegracam_device *tc_dev)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	const struct imx585_mode *mode;
	int err;

	mutex_lock(&priv->lock);

	mode = &imx585_modes[s_data->mode];

	err = imx585_mode_init(priv, mode);
	if (err)
		goto unlock;

	err = imx585_write_table(priv, priv->clear_hdr ?
				       imx585_clearhdr_regs :
				       imx585_normal_regs,
			priv->clear_hdr ? ARRAY_SIZE(imx585_clearhdr_regs) :
			ARRAY_SIZE(imx585_normal_regs));
	if (err)
		goto unlock;

	err = cci_write(priv->regmap, IMX585_REG_DIGITAL_CLAMP, 0x00, NULL);
	if (err)
		goto unlock;

	/* Release master stop. imx585_common_regs starts with 0x3002 = 0x01 and
	 * nothing else in this driver ever clears it, so without this write the
	 * sensor stays in master stop and never produces a frame no matter what
	 * MODE_SELECT says. Ordering is deliberate and matches the
	 * pre-conversion driver: XMSTA is released *before* leaving standby, not
	 * after the post-MODE_SELECT settling delay.
	 *
	 * Upstream guards this with `if (sync_mode != SYNC_EXTERNAL)`; we have no
	 * external-sync plumbing, so it is unconditional. If XVS/XHS slave mode is
	 * ever wired up, this write must become conditional again -- in slave mode
	 * the master start is what the external pulse provides.
	 */
	err = cci_write(priv->regmap, IMX585_REG_XMSTA, 0x00, NULL);
	if (err)
		goto unlock;

	err = cci_write(priv->regmap, IMX585_REG_MODE_SELECT,
			   IMX585_MODE_STREAMING, NULL);
	if (err)
		goto unlock;

	usleep_range(IMX585_STREAM_DELAY_US,
		     IMX585_STREAM_DELAY_US + IMX585_STREAM_DELAY_RANGE_US);

unlock:
	if (err)
		cci_write(priv->regmap, IMX585_REG_MODE_SELECT,
			  IMX585_MODE_STANDBY, NULL);
	mutex_unlock(&priv->lock);
	return err;
}

static int imx585_stop_streaming(struct tegracam_device *tc_dev)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	int err;

	mutex_lock(&priv->lock);
	err = cci_write(priv->regmap, IMX585_REG_MODE_SELECT,
			   IMX585_MODE_STANDBY, NULL);
	mutex_unlock(&priv->lock);

	return err;
}

static int imx585_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	const u32 gain_max = priv->clear_hdr ? IMX585_ANALOG_GAIN_MAX_HDR
					     : IMX585_ANALOG_GAIN_MAX;
	u32 gain;

	/* val arrives in the units the mode advertises: dB * gain_factor, with
	 * gain_factor 10. The register takes 0.3 dB steps, hence /3. Clamp to the
	 * advertised range first, then to the hardware ceiling -- above it the
	 * sensor applies digital gain, which a linear-raw pipeline does not want
	 * happening silently. */
	val = clamp_t(s64, val, mode->control_properties.min_gain_val,
		      mode->control_properties.max_gain_val);

	gain = div_u64((u64)val, 3);
	if (gain > gain_max)
		gain = gain_max;

	dev_dbg(priv->dev, "%s: %lld (dB*10) -> gain reg %u\n", __func__, val,
		gain);

	return cci_write(priv->regmap, IMX585_REG_ANALOG_GAIN, gain, NULL);
}

static int imx585_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	struct camera_common_data *s_data = tc_dev->s_data;
	const struct sensor_mode_properties *mode =
		&s_data->sensor_props.sensor_modes[s_data->mode_prop_idx];
	u64 pixel_clock = mode->signal_properties.pixel_clock.val;
	u32 line_length = mode->image_properties.line_length;
	u32 factor = mode->control_properties.framerate_factor;
	u32 frame_length;

	if (!val || !pixel_clock || !line_length)
		return -EINVAL;

	frame_length = div_u64(pixel_clock * factor,
				 (u64)line_length * val);
	frame_length = clamp(frame_length,
			      (u32)mode->control_properties.min_framerate,
			      IMX585_VMAX_MAX);

	priv->vmax = frame_length;

	return cci_write(priv->regmap, IMX585_REG_VMAX, priv->vmax, NULL);
}

static int imx585_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);
	u32 shr_min = priv->clear_hdr ? IMX585_SHR_MIN_HDR : IMX585_SHR_MIN;
	u32 shr;

	if (val <= 0)
		return -EINVAL;

	shr = priv->vmax - val;
	shr = clamp(shr, shr_min, IMX585_VMAX_MAX);

	return cci_write(priv->regmap, IMX585_REG_SHR, shr, NULL);
}

static int imx585_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	struct imx585 *priv = tegracam_to_imx585(tc_dev);

	/* Must actually take the hold: tegracam uses this to keep an exposure and
	 * a gain update on the same side of a frame boundary. */
	return cci_write(priv->regmap, IMX585_REG_REGHOLD, val ? 1 : 0, NULL);
}

static const u32 imx585_ctrl_cids[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

static struct tegracam_ctrl_ops imx585_ctrl_ops = {
	.numctrls = ARRAY_SIZE(imx585_ctrl_cids),
	.ctrl_cid_list = imx585_ctrl_cids,
	.set_gain = imx585_set_gain,
	.set_exposure = imx585_set_exposure,
	.set_frame_rate = imx585_set_frame_rate,
	.set_group_hold = imx585_set_group_hold,
};

static struct camera_common_sensor_ops imx585_common_ops = {
	.numfrmfmts = ARRAY_SIZE(imx585_frmfmt),
	.frmfmt_table = imx585_frmfmt,
	.power_on = imx585_power_on,
	.power_off = imx585_power_off,
	.parse_dt = imx585_parse_dt,
	.power_get = imx585_power_get,
	.power_put = imx585_power_put,
	.set_mode = imx585_set_mode,
	.start_streaming = imx585_start_streaming,
	.stop_streaming = imx585_stop_streaming,
};

/*
 * Bind the focus actuator named by the sensor's "lens-focus" phandle.
 *
 * A lens controller is not in the data path: it has zero pads and no of_graph
 * endpoints, and Tegra VI builds its notifier purely by walking endpoints
 * (of_graph_get_next_endpoint -> of_graph_get_remote_port_parent in
 * vi/graph.c). So VI never discovers a lens, never binds it into its
 * v4l2_device, and v4l2_device_register_subdev_nodes() therefore never gives it
 * a /dev/v4l-subdev node, however correctly the lens driver registers itself.
 *
 * The upstream answer is for the *sensor* to own a sub-notifier for its
 * actuator, which is what v4l2_async_register_subdev_sensor() does for drivers
 * that call it -- it parses "lens-focus" (v4l2-fwnode.c) and adds the reference
 * to a notifier whose .sd is the sensor. tegracam registers the subdev itself
 * with a plain v4l2_async_register_subdev(), so that helper is not available to
 * us; this builds the same thing from the exported pieces.
 *
 * Must be called BEFORE tegracam_v4l2subdev_register(), because that is what
 * async-registers the sensor, and v4l2_async_find_subdev_notifier() is consulted
 * at bind time to link this notifier in as a child.
 *
 * Deliberately non-fatal when there is no lens to bind. Once a sub-notifier is
 * registered, VI's root notifier cannot complete until it does --
 * v4l2_async_nf_can_complete() recurses into sub-notifiers -- so a lens that
 * never binds costs the whole camera its /dev/video node. Treating an absent or
 * disabled lens-focus as "no focus control" rather than as an error keeps that
 * failure impossible for the two cases we can detect here.
 */
static int imx585_register_lens_notifier(struct imx585 *priv)
{
	struct device *dev = priv->dev;
	struct v4l2_subdev *sd = &priv->s_data->subdev;
	struct v4l2_async_connection *asc;
	struct fwnode_handle *lens;
	int err;

	lens = fwnode_find_reference(dev_fwnode(dev), "lens-focus", 0);
	if (IS_ERR(lens))
		return 0;

	if (!fwnode_device_is_available(lens)) {
		dev_info(dev, "lens-focus target is disabled, no focus control\n");
		fwnode_handle_put(lens);
		return 0;
	}

	priv->lens_nf = devm_kzalloc(dev, sizeof(*priv->lens_nf), GFP_KERNEL);
	if (!priv->lens_nf) {
		fwnode_handle_put(lens);
		return -ENOMEM;
	}

	v4l2_async_subdev_nf_init(priv->lens_nf, sd);
	asc = v4l2_async_nf_add_fwnode(priv->lens_nf, lens,
				       struct v4l2_async_connection);
	fwnode_handle_put(lens);
	if (IS_ERR(asc)) {
		v4l2_async_nf_cleanup(priv->lens_nf);
		priv->lens_nf = NULL;
		return PTR_ERR(asc);
	}

	err = v4l2_async_nf_register(priv->lens_nf);
	if (err) {
		v4l2_async_nf_cleanup(priv->lens_nf);
		priv->lens_nf = NULL;
		return err;
	}

	dev_dbg(dev, "lens-focus sub-notifier registered\n");
	return 0;
}

static int imx585_board_setup(struct imx585 *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct device *dev = priv->dev;
	u64 id_high, id_low;
	int err;

	err = imx585_power_on(s_data);
	if (err) {
		dev_err(dev, "failed to power on sensor (%d)\n", err);
		return err;
	}

	err = cci_read(priv->regmap, IMX585_REG_MODEL_ID_MSB, &id_high, NULL);
	if (err)
		goto power_off;

	err = cci_read(priv->regmap, IMX585_REG_MODEL_ID_LSB, &id_low, NULL);
	if (err)
		goto power_off;

	dev_dbg(priv->dev, "IMX585 detected id 0x%02llx%02llx\n",
		id_high & 0xff, id_low & 0xff);

	err = imx585_power_off(s_data);
	if (err)
		dev_warn(dev, "sensor power off failed (%d)\n", err);

	return 0;

power_off:
	imx585_power_off(s_data);
	return err;
}

static int imx585_init_inck_sel(struct imx585 *priv)
{
	u32 xclk = clk_get_rate(priv->xclk);
	u32 i;

	for (i = 0; i < ARRAY_SIZE(imx585_inck_table); ++i) {
		if (imx585_inck_table[i].xclk_hz == xclk) {
			priv->inck_sel = imx585_inck_table[i].inck_sel;
			return 0;
		}
	}

	return -EINVAL;
}

static int imx585_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct imx585 *priv;
	int err;

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	priv->dev = dev;
	priv->client = client;

	i2c_set_clientdata(client, priv);

	tc_dev = devm_kzalloc(dev, sizeof(*tc_dev), GFP_KERNEL);
	if (!tc_dev)
		return -ENOMEM;

	priv->tc_dev = tc_dev;
	/* ClearHDR is opt-in from the device tree and off by default.
	 *
	 * It is not a control because tegracam owns the control handler and its
	 * list is TEGRA_CAMERA_CID_*; the pre-conversion driver exposes this as a
	 * V4L2 menu control, which does not transfer. TEGRA_CAMERA_CID_HDR_EN
	 * exists but has no tegracam_ctrl_ops hook, so a DT property is the
	 * honest mechanism for a mode that cannot change while streaming anyway.
	 *
	 * Note ClearHDR is *companded*, i.e. non-linear. This distro exists to get
	 * linear raw out of this sensor, so enabling it is contrary to the usual
	 * goal and is here for completeness rather than because it is wanted.
	 * Untested on hardware. See PORTING.md for what is still missing. */
	priv->clear_hdr = of_property_read_bool(dev->of_node, "sony,clear-hdr");
	if (priv->clear_hdr)
		dev_warn(dev,
			 "ClearHDR enabled: output is companded 16-bit, not linear\n");

	strscpy(tc_dev->name, "imx585", sizeof(tc_dev->name));
	tc_dev->client = client;
	tc_dev->dev = dev;
	tc_dev->dev_regmap_config = &imx585_regmap_config;
	tc_dev->sensor_ops = &imx585_common_ops;
	tc_dev->tcctrl_ops = &imx585_ctrl_ops;

	tc_dev->priv = priv;

	err = tegracam_device_register(tc_dev);
	if (err)
		return err;

	tegracam_set_privdata(tc_dev, priv);
	priv->s_data = tc_dev->s_data;
	priv->regmap = priv->s_data->regmap;

	err = imx585_parse_endpoint(priv);
	if (err) {
		dev_err(dev, "failed to parse endpoint\n");
		goto unregister;
	}

	imx585_populate_sensor_mode_props(priv);

	err = imx585_init_inck_sel(priv);
	if (err) {
		dev_err(dev, "unsupported xclk frequency\n");
		goto unregister;
	}

	err = imx585_board_setup(priv);
	if (err)
		goto unregister;

	/* Before tegracam_v4l2subdev_register(): that async-registers the sensor,
	 * and the sub-notifier has to exist by then to be linked as its child. */
	err = imx585_register_lens_notifier(priv);
	if (err)
		goto unregister;

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err)
		goto unregister_lens;

	dev_info(dev, "Sony IMX585 tegracam driver registered\n");
	return 0;

unregister_lens:
	if (priv->lens_nf) {
		v4l2_async_nf_unregister(priv->lens_nf);
		v4l2_async_nf_cleanup(priv->lens_nf);
		priv->lens_nf = NULL;
	}
unregister:
	tegracam_device_unregister(tc_dev);
	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void imx585_remove(struct i2c_client *client)
#else
static int imx585_remove(struct i2c_client *client)
#endif
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct tegracam_device *tc_dev;
	struct imx585 *priv;

	if (!s_data) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
		return 0;
#else
		return;
#endif
	}

	tc_dev = to_tegracam_device(s_data);
	priv = tegracam_to_imx585(tc_dev);
	tegracam_v4l2subdev_unregister(tc_dev);
	if (priv->lens_nf) {
		v4l2_async_nf_unregister(priv->lens_nf);
		v4l2_async_nf_cleanup(priv->lens_nf);
		priv->lens_nf = NULL;
	}
	tegracam_device_unregister(tc_dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct of_device_id imx585_of_match[] = {
	{ .compatible = "sony,imx585" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx585_of_match);

static const struct i2c_device_id imx585_id[] = {
	{ "imx585", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx585_id);

static struct i2c_driver imx585_i2c_driver = {
	.driver = {
		.name = "imx585",
		.of_match_table = imx585_of_match,
	},
	.probe = imx585_probe,
	.remove = imx585_remove,
	.id_table = imx585_id,
};

module_i2c_driver(imx585_i2c_driver);

MODULE_DESCRIPTION("Tegra tegracam driver for Sony IMX585");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL");
