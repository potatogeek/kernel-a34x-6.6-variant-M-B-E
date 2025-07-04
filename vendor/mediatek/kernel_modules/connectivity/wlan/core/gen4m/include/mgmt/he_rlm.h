/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

/*! \file   "rlm.h"
 *  \brief
 */

#ifndef _HE_RLM_H
#define _HE_RLM_H

#if (CFG_SUPPORT_802_11AX == 1)

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

#define HE_PHY_CAP2_INFO_DEFAULT_VAL (HE_PHY_CAP2_FULL_BW_UL_MU_MIMO | \
					HE_PHY_CAP2_PARTIAL_BW_UL_MU_MIMO)
#define HE_PHY_CAP6_INFO_DEFAULT_VAL (HE_PHY_CAP6_PPE_THRESHOLD)

#if (CFG_SUPPORT_WIFI_6G == 1)
#if CFG_SUPPORT_RX_RDG
#define FIELD_HE_6G_CAP_RDR     HE_6G_CAP_INFO_RD_RESPONDER
#else
#define FIELD_HE_6G_CAP_RDR     0
#endif

#if (CFG_SUPPORT_SMALL_PKT == 1)
#define HE_6G_CAP_INFO_DEFAULT_VAL \
	(HE_6G_CAP_INFO_MSS_1_US | \
	HE_6G_CAP_INFO_MAX_AMPDU_LEN_1024K | \
	HE_6G_CAP_INFO_MAX_MPDU_LEN_3K | \
	HE_6G_CAP_INFO_SM_POWER_SAVE | \
	FIELD_HE_6G_CAP_RDR)
#else
#define HE_6G_CAP_INFO_DEFAULT_VAL \
	(HE_6G_CAP_INFO_MSS_NO_RESTRICIT | \
	HE_6G_CAP_INFO_MAX_AMPDU_LEN_1024K | \
	HE_6G_CAP_INFO_MAX_MPDU_LEN_3K | \
	HE_6G_CAP_INFO_SM_POWER_SAVE | \
	FIELD_HE_6G_CAP_RDR)
#endif /* CFG_SUPPORT_SMALL_PKT */

#endif /* CFG_SUPPORT_WIFI_6G */

#define PPE_RU_IDX_SIZE              4
#define PPE_SUBFIELD_BITS_NUM        6

/******************************************************************************
 *                                 M A C R O S
 ******************************************************************************
 */
#define HE_RESET_MAC_CAP(_aucHeMacCapInfo) \
	(memset(_aucHeMacCapInfo, 0, HE_MAC_CAP_BYTE_NUM))

#define HE_RESET_PHY_CAP(_aucHePhyCapInfo) \
{ \
	memset(_aucHePhyCapInfo, 0, HE_PHY_CAP_BYTE_NUM); \
	_aucHePhyCapInfo[2] = (u_int8_t)HE_PHY_CAP2_INFO_DEFAULT_VAL; \
	_aucHePhyCapInfo[6] = (u_int8_t)HE_PHY_CAP6_INFO_DEFAULT_VAL; \
}


/* Definitions for action control of SMPS params */
enum {
	SMPS_ACTION_UPDATE_HT_CAP = 0,
	SMPS_ACTION_UPDATE_HE_CAP = 1,
	SMPS_ACTION_SEND_ACTION_FRAME = 2,
	SMPS_ACTION_MAX
};

/******************************************************************************
 *                             D A T A   T Y P E S
 ******************************************************************************
 */
struct __HE_TWT_INFO_T {
	u_int8_t ucFlowType;
	u_int8_t ucFlowID;
	u_int8_t ucIntervalExp;
	u_int8_t ucProtection;
};

struct __HE_CFG_INFO_T {
	u_int8_t fgHeSupport;              /* HE Support */
	u_int8_t fgHeEnable;               /* HE Enable/Disable Control */
	u_int8_t fgTwtRequesterEnable;     /* HW TWT Requester  Control */
	u_int8_t fgTwtResponderEnable;     /* HW TWT Responder Control */
	u_int8_t fgHtcHe;                  /* +HTC HE Support */
	u_int8_t fgFragment;               /* Fragmentation Support */
	u_int8_t fgMultiTidAgg;            /* Multi-TID Aggregation Support */
	u_int8_t fgAmsduFragment;          /* A-MSDU Fragmentation Support */
	u_int8_t fg32bitBaBitmap;          /* 32-bit BA Bitmap Support */
};

struct HE_A_CTRL_OM_T {
	u_int8_t ucRxNss;
	u_int8_t ucTxNsts;
	u_int8_t ucBW;
	u_int8_t fgDisMuUL;
	u_int8_t fgDisMuULData;
};


/******************************************************************************
 *                            P U B L I C   D A T A
 ******************************************************************************
 */
#if (CFG_SUPPORT_802_11AX == 1)
extern uint8_t  g_fgHTSMPSEnabled;
#endif

 /* To indocate if WFA test bed */
extern uint8_t g_IsWfaTestBed;
extern uint8_t g_IsTwtLogo;

/******************************************************************************
 *                           P R I V A T E   D A T A
 ******************************************************************************
 */

/******************************************************************************
 *                  F U N C T I O N   D E C L A R A T I O N S
 ******************************************************************************
 */
u_int32_t heRlmCalculateHeCapIELen(
	struct ADAPTER *prAdapter,
	u_int8_t ucBssIndex,
	struct STA_RECORD *prStaRec);
void heRlmFillHeCapIE(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct MSDU_INFO *prMsduInfo);
u_int32_t heRlmCalculateHeOpIELen(
	struct ADAPTER *prAdapter,
	u_int8_t ucBssIndex,
	struct STA_RECORD *prStaRec);
void heRlmReqGenerateHeCapIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
void heRlmRspGenerateHeCapIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
void heRlmRspGenerateHeOpIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
uint8_t heRlmPeerMaxBwCap(
	uint8_t *pucChannelWidthSet);
void heRlmRecHeCapInfo(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	const uint8_t *pucIE);
void heRlmRecHeOperation(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	const uint8_t *pucIE);
#if (CFG_SUPPORT_UPDATE_HE_BSS_COLOR_FROM_BEACON == 1)
void heRlmRecBssColorChangeAnnouncement(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	const uint8_t *pucIE);
#endif /* CFG_SUPPORT_UPDATE_HE_BSS_COLOR_FROM_BEACON */
u_int8_t heRlmRecHeSRParams(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct SW_RFB *prSwRfb,
	const uint8_t *pucIE,
	uint16_t u2IELength);
void heRlmInitHeHtcACtrlOMAndUPH(
	struct ADAPTER *prAdapter);
void heRlmParseHeHtcACtrlOM(
	uint32_t u4Htc,
	struct HE_A_CTRL_OM_T *prHeActrlOM);
uint32_t heRlmSendHtcNullFrame(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	uint8_t ucUP,
	PFN_TX_DONE_HANDLER pfTxDoneHandler);
uint8_t heRlmMaxBwToHeBw(uint8_t ucMaxBw);

#if (CFG_SUPPORT_802_11AX == 1)
uint32_t heRlmSMPSTxDone(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus);

void heRlmSendSMPSActionFrame(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec);

void heRlmProcessSMPSAction(
	struct ADAPTER *prAdapter,
	struct MSG_HDR *prMsgHdr);
#endif

#if (CFG_SUPPORT_WIFI_6G == 1)
void heRlmRecHe6GCapInfo(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	uint8_t *pucIE);
void heRlmReqGenerateHe6gBandCapIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
void heRlmRspGenerateHeRnrIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
#endif

#if (CFG_SUPPORT_BTWT == 1)
void heRlmRecBTWTparams(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	const uint8_t *pucIE);
#endif

void heRlmRspGenerateBssMaxIdleIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);

void heRlmReqGenerateBssMaxIdleIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);

static void heRlmFillBssMaxIdleIE(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct MSDU_INFO *prMsduInfo);

#if (CFG_SUPPORT_NAN == 1)
uint32_t heRlmFillNANHECapIE(struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo, uint8_t *pOutBuf);

uint32_t heRlmFillNANHeOpIE(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	uint8_t *pOutBuf);
#endif

#if (CFG_SUPPORT_WIFI_6G == 1)
uint32_t heRlmCalculateRegConnectivityIELen(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct STA_RECORD *prStaRec);
void heRlmReqGenerateHeRegConnectivityIE(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo);
#endif

#endif /* CFG_SUPPORT_802_11AX == 1 */
#endif /* !_HE_RLM_H */
