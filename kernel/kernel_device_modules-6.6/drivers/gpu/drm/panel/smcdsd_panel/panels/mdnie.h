/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MDNIE_H__
#define __MDNIE_H__

#include <drm/drm_mipi_dsi.h>

typedef u8 mdnie_t;

enum MDNIE_MODE {
	DYNAMIC,
	STANDARD,
	NATURAL,
	MOVIE,
	AUTO,
	EBOOK,
	MODE_MAX
};

enum SCENARIO {
	UI_MODE,
	VIDEO_NORMAL_MODE,
	CAMERA_MODE = 4,
	NAVI_MODE,
	GALLERY_MODE,
	VT_MODE,
	BROWSER_MODE,
	EBOOK_MODE,
	EMAIL_MODE,
	HMD_8_MODE,
	HMD_16_MODE,
	SCENARIO_MAX,
	DMB_NORMAL_MODE = 20,
	DMB_MODE_MAX
};

enum BYPASS {
	BYPASS_OFF,
	BYPASS_ON,
	BYPASS_MAX
};

enum LIGHT_NOTIFICATION {
	LIGHT_NOTIFICATION_OFF,
	LIGHT_NOTIFICATION_ON,
	LIGHT_NOTIFICATION_MAX
};

enum ACCESSIBILITY {
	ACCESSIBILITY_OFF,
	NEGATIVE,
	COLOR_BLIND,
	SCREEN_CURTAIN,
	GRAYSCALE,
	GRAYSCALE_NEGATIVE,
	COLOR_BLIND_HBM,
	ACCESSIBILITY_MAX
};

enum HBM {
	HBM_OFF,
	HBM_ON,
	HBM_MAX
};

enum COLOR_OFFSET_FUNC {
	COLOR_OFFSET_FUNC_NONE,
	COLOR_OFFSET_FUNC_F1,
	COLOR_OFFSET_FUNC_F2,
	COLOR_OFFSET_FUNC_F3,
	COLOR_OFFSET_FUNC_F4,
	COLOR_OFFSET_FUNC_MAX
};

enum HMD_MODE {
	HMD_MDNIE_OFF,
	HMD_MDNIE_ON,
	HMD_3000K = HMD_MDNIE_ON,
	HMD_4000K,
	HMD_6400K,
	HMD_7500K,
	HMD_MDNIE_MAX
};

enum NIGHT_MODE {
	NIGHT_MODE_OFF,
	NIGHT_MODE_ON,
	NIGHT_MODE_MAX
};

enum HDR {
	HDR_OFF,
	HDR_ON,
	HDR_1 = HDR_ON,
	HDR_2,
	HDR_3,
	HDR_MAX
};

enum COLOR_LENS {
	COLOR_LENS_OFF,
	COLOR_LENS_ON,
	COLOR_LENS_MAX
};

struct mdnie_seq_info {
	struct mipi_dsi_msg dsi_msg;
	char *msg_name;	/* tx_buf */
	int delay;

	unsigned int modes;

	void *reserved;
};

struct mdnie_table {
#define MDNIE_IDX_MAX	12
	char *name;
	struct mdnie_seq_info seq[MDNIE_IDX_MAX + 1];
};

struct mdnie_scr_info {
	u32 index;
	u32 cr;
	u32 wr;
	u32 wg;
	u32 wb;
};

struct mdnie_trans_info {
	u32 index;
	u32 offset;
	u32 enable;
};

struct mdnie_night_info {
	u32 max_w;
	u32 max_h;
};

struct mdnie_color_lens_info {
	u32 max_color;
	u32 max_level;
	u32 max_w;
};

struct mdnie_tune {
	struct mdnie_table	*bypass_table;
	struct mdnie_table	*accessibility_table;
	struct mdnie_table	*light_notification_table;
	struct mdnie_table	*hbm_table;
	struct mdnie_table	*hmd_table;
	struct mdnie_table	(*main_table)[MODE_MAX];
	struct mdnie_table	*dmb_table;
	struct mdnie_table	*night_table;
	struct mdnie_table	*hdr_table;
	struct mdnie_table	*lens_table;

	struct mdnie_scr_info	*scr_info;
	struct mdnie_trans_info	*trans_info;
	struct mdnie_night_info	*night_info;
	struct mdnie_color_lens_info *color_lens_info;
	unsigned char **coordinate_table;
	unsigned char **adjust_ldu_table;
	unsigned char **night_mode_table;
	unsigned char *color_lens_table;
	int (*get_hbm_index)(int lux);
	int (*color_offset[])(int x, int y);
};

struct rgb_info {
	int r;
	int g;
	int b;
};

struct mdnie_ops {
	int (*write)(void *devdata, void *seq, u32 num);
	int (*read)(void *devdata, u8 addr, u8 *buf, u32 size);
};

struct mdnie_info {
	struct device		*dev;
	struct mutex		dev_lock;
	struct mutex		lock;

	unsigned int		enable;

	struct mdnie_tune	*tune;

	enum SCENARIO		scenario;
	enum MDNIE_MODE		mode;
	enum BYPASS		bypass;
	enum HBM		hbm;
	enum HMD_MODE		hmd_mode;
	enum NIGHT_MODE	night_mode;
	enum HDR		hdr;
	enum LIGHT_NOTIFICATION		light_notification;
	enum COLOR_LENS	color_lens;

	unsigned int		tuning;
	unsigned int		accessibility;
	unsigned int		color_correction;
	unsigned int		coordinate[2];

	char			path[50];

	void			*dd_mdnie;

	void			*data;

	struct mdnie_ops	ops;

	struct notifier_block	fb_notif;
#if defined(CONFIG_SMCDSD_DPUI)
	struct notifier_block	dpui_notif;
#endif

	struct rgb_info		wrgb_current;
	struct rgb_info		wrgb_default;
	struct rgb_info		wrgb_balance;
	struct rgb_info		wrgb_ldu;

	unsigned int disable_trans_dimming;
	unsigned int night_mode_level;
	unsigned int color_lens_color;
	unsigned int color_lens_level;
	unsigned int ldu;

	struct mdnie_table table_buffer;
	mdnie_t sequence_buffer[256];
};

extern struct class *mdnie_register(struct device *p, void *devdata,
	int (*fn_w)(void *devdata, void *seq, u32 num),
	int (*fn_r)(void *devdata, u8 cmd, u8 *buf, u32 len),
	unsigned int *coordinate, struct mdnie_tune *tune);
extern int attr_store_for_each(struct class *class, const char *name, const char *buf, size_t size);
extern int mdnie_force_update(struct device *dev, void *data);

#endif /* __MDNIE_H__ */
