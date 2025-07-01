/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */


/*! \file   "rlm.h"
 *  \brief
 */

#ifndef _P2P_RLM_H
#define _P2P_RLM_H

/******************************************************************************
 *                         C O M P I L E R   F L A G S
 ******************************************************************************
 */

/******************************************************************************
 *                    E X T E R N A L   R E F E R E N C E S
 ******************************************************************************
 */

/******************************************************************************
 *                              C O N S T A N T S
 ******************************************************************************
 */

#define CHANNEL_SPAN_20 20

/******************************************************************************
 *                                 M A C R O S
 ******************************************************************************
 */

/******************************************************************************
 *                             D A T A   T Y P E S
 ******************************************************************************
 */

/******************************************************************************
 *                            P U B L I C   D A T A
 ******************************************************************************
 */
#if (CFG_SUPPORT_SAP_PUNCTURE == 1)
static const uint16_t PUNCT_VALID_BITMAP_80[] = {
	0xF, 0xE, 0xD, 0xB, 0x7
};
static const uint16_t PUNCT_VALID_BITMAP_160[] = {
	0xFF, 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF,
	0x7F, 0xFC, 0xF3, 0xCF, 0x3F
};
static const uint16_t PUNCT_VALID_BITMAP_320[] = {
	0xFFFF, 0xFFFC, 0xFFF3, 0xFFCF, 0xFF3F, 0xFCFF, 0xF3FF, 0xCFFF,
	0x3FFF, 0xFFF0, 0xFF0F, 0xF0FF, 0x0FFF, 0xFFC0, 0xFF30, 0xFCF0,
	0xF3F0, 0xCFF0, 0x3FF0, 0x0FFC, 0x0FF3, 0x0FCF, 0x0F3F, 0x0CFF,
	0x03FF
};
#endif /* CFG_SUPPORT_SAP_PUNCTURE */

/******************************************************************************
 *                           P R I V A T E   D A T A
 ******************************************************************************
 */

/******************************************************************************
 *                  F U N C T I O N   D E C L A R A T I O N S
 ******************************************************************************
 */
#if CFG_ENABLE_WIFI_DIRECT
#if (CFG_SUPPORT_WIFI_6G == 1)
void rlmUpdate6GOpInfo(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);
#endif

void rlmBssInitForAP(struct ADAPTER *prAdapter, struct BSS_INFO *prBssInfo);

u_int8_t rlmUpdateBwByChListForAP(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);

u_int8_t rlmUpdateParamsForAP(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo, u_int8_t fgUpdateBeacon);

void rlmBssUpdateChannelParams(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);

enum ENUM_CHNL_EXT rlmDecideScoForAP(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);

enum ENUM_CHNL_EXT rlmGetScoForAP(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);

enum ENUM_CHNL_EXT rlmGetScoByChnInfo(struct ADAPTER *prAdapter,
		struct RF_CHANNEL_INFO *prChannelInfo);

uint8_t rlmGetVhtS1ForAP(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo);

void rlmGetChnlInfoForCSA(struct ADAPTER *prAdapter,
	enum ENUM_BAND eBand,
	uint8_t ucCh,
	uint8_t ucBssIdx,
	struct RF_CHANNEL_INFO *prRfChnlInfo);

#if (CFG_SUPPORT_SAP_PUNCTURE == 1)
void rlmPunctUpdateLegacyBw(enum ENUM_BAND eBand, uint16_t u2Bitmap,
			    uint8_t ucPriChannel, uint8_t *pucBw,
			    uint8_t *pucSeg0, uint8_t *pucSeg1,
			    uint8_t *pucOpClass);

u_int8_t rlmValidatePunctBitmap(struct ADAPTER *prAdapter,
				enum ENUM_BAND eBand,
				enum ENUM_MAX_BANDWIDTH_SETTING eBw,
				uint8_t ucPriCh, uint16_t u2PunctBitmap);
#endif /* CFG_SUPPORT_SAP_PUNCTURE */

#endif /* CFG_ENABLE_WIFI_DIRECT */

#endif
