/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 ** Id: /mgmt/log_ext.c
 */

/*! \file   "log_ext.c"
 *  \brief This file includes log report support.
 *    Detail description.
 */

/*******************************************************************************
 *                         C O M P I L E R   F L A G S
 *******************************************************************************
 */

/*******************************************************************************
 *                    E X T E R N A L   R E F E R E N C E S
 *******************************************************************************
 */

#include "precomp.h"
#include "gl_os.h"
#if CFG_ENABLE_WIFI_DIRECT
#include "gl_p2p_os.h"
#endif
#if (CFG_SUPPORT_CONN_LOG == 1)
#include <linux/can/netlink.h>
#include <net/netlink.h>
#include <net/cfg80211.h>
#endif

/*******************************************************************************
 *                              C O N S T A N T S
 *******************************************************************************
 */
#define RPTMACSTR	"%pM"
#define RPTMAC2STR(a)	a

#if (CFG_EXT_VERSION > 2)
#define DISP_STRING(_str)       _str
#endif

/*******************************************************************************
 *                             D A T A   T Y P E S
 *******************************************************************************
 */


/*******************************************************************************
 *                            P U B L I C   D A T A
 *******************************************************************************
 */

/*******************************************************************************
 *                   F U N C T I O N   D E C L A R A T I O N S
 *******************************************************************************
 */

/*******************************************************************************
 *                           P R I V A T E   D A T A
 *******************************************************************************
 */
enum ENUM_ROAMING_CATAGORY {
	ROAMING_CATEGORY_UNSPECIFIC = 0,
	ROAMING_CATEGORY_LOW_RSSI = 1,
	ROAMING_CATEGORY_HIGH_CU = 2,
	ROAMING_CATEGORY_BEACON_LOST = 3,
	ROAMING_CATEGORY_EMERGENCY = 4,
	ROAMING_CATEGORY_BTM_REQ = 5,
	ROAMING_CATEGORY_IDLE = 6,
	ROAMING_CATEGORY_WTC = 7,
	ROAMING_CATEGORY_INACTIVE_TIMER = 8,
	ROAMING_CATEGORY_SCAN_TIMER = 9,
	ROAMING_CATEGORY_BTCOEX = 10,
};

static uint8_t apucRoamingReasonToLog[ROAMING_REASON_NUM] = {
	ROAMING_CATEGORY_LOW_RSSI,		 /* map to ROAMING_REASON_POOR_RCPI(0) */
	ROAMING_CATEGORY_UNSPECIFIC,	 /* map to ROAMING_REASON_TX_ERR(1) */
	ROAMING_CATEGORY_UNSPECIFIC,	 /* map to ROAMING_REASON_RETRY(2) */
	ROAMING_CATEGORY_IDLE,			 /* map to ROAMING_REASON_IDLE(3) */
	ROAMING_CATEGORY_HIGH_CU,		 /* map to ROAMING_REASON_HIGH_CU(4)*/
	ROAMING_CATEGORY_BTCOEX,		 /* map to ROAMING_REASON_BT_COEX(5) */
	ROAMING_CATEGORY_BEACON_LOST,	 /* map to ROAMING_REASON_BEACON_TIMEOUT(6) */
	ROAMING_CATEGORY_UNSPECIFIC,	 /* map to ROAMING_REASON_INACTIVE(7) */
	ROAMING_CATEGORY_EMERGENCY, 	 /* map to ROAMING_REASON_SAA_FAIL(8) */
	ROAMING_CATEGORY_UNSPECIFIC,	 /* map to ROAMING_REASON_UPPER_LAYER_TRIGGER(9) */
	ROAMING_CATEGORY_BTM_REQ,		 /* map to ROAMING_REASON_BTM(10) */
	ROAMING_CATEGORY_INACTIVE_TIMER, /* map to ROAMING_REASON_INACTIVE_TIMER(11) */
	ROAMING_CATEGORY_SCAN_TIMER,	 /* map to ROAMING_REASON_SCAN_TIMER(12) */
};

static uint8_t *apucExtBandStr[BAND_NUM] = {
	(uint8_t *) DISP_STRING("NULL"),
	(uint8_t *) DISP_STRING("2G"),
	(uint8_t *) DISP_STRING("5G")
#if (CFG_SUPPORT_WIFI_6G == 1)
	,
	(uint8_t *) DISP_STRING("6G")
#endif
};

static char *tx_status_text(uint8_t rTxDoneStatus)
{
	switch (rTxDoneStatus) {
	case TX_RESULT_SUCCESS: return "ACK";
	case TX_RESULT_MPDU_ERROR: return "NO_ACK";
	default: return "TX_FAIL";
	}
}

static char *msdu_tx_status_text(uint32_t u4Stat)
{
	switch (u4Stat) {
	case 0: return "ACK";
	case 1: return "NO_ACK";
	default: return "TX_FAIL";
	}
}

/*
 * EAP Method Types as allocated by IANA:
 * http://www.iana.org/assignments/eap-numbers
 */
enum eap_type {
	EAP_TYPE_NONE = 0,
	EAP_TYPE_IDENTITY = 1 /* RFC 3748 */,
	EAP_TYPE_NOTIFICATION = 2 /* RFC 3748 */,
	EAP_TYPE_NAK = 3 /* Response only, RFC 3748 */,
	EAP_TYPE_MD5 = 4, /* RFC 3748 */
	EAP_TYPE_OTP = 5 /* RFC 3748 */,
	EAP_TYPE_GTC = 6, /* RFC 3748 */
	EAP_TYPE_TLS = 13 /* RFC 2716 */,
	EAP_TYPE_LEAP = 17 /* Cisco proprietary */,
	EAP_TYPE_SIM = 18 /* RFC 4186 */,
	EAP_TYPE_TTLS = 21 /* RFC 5281 */,
	EAP_TYPE_AKA = 23 /* RFC 4187 */,
	EAP_TYPE_PEAP = 25 /* draft-josefsson-pppext-eap-tls-eap-06.txt */,
	EAP_TYPE_MSCHAPV2 = 26 /* draft-kamath-pppext-eap-mschapv2-00.txt */,
	EAP_TYPE_TLV = 33 /* draft-josefsson-pppext-eap-tls-eap-07.txt */,
	EAP_TYPE_TNC = 38 /* TNC IF-T v1.0-r3; note: tentative assignment;
			   * type 38 has previously been allocated for
			   * EAP-HTTP Digest, (funk.com) */,
	EAP_TYPE_FAST = 43 /* RFC 4851 */,
	EAP_TYPE_PAX = 46 /* RFC 4746 */,
	EAP_TYPE_PSK = 47 /* RFC 4764 */,
	EAP_TYPE_SAKE = 48 /* RFC 4763 */,
	EAP_TYPE_IKEV2 = 49 /* RFC 5106 */,
	EAP_TYPE_AKA_PRIME = 50 /* RFC 5448 */,
	EAP_TYPE_GPSK = 51 /* RFC 5433 */,
	EAP_TYPE_PWD = 52 /* RFC 5931 */,
	EAP_TYPE_EKE = 53 /* RFC 6124 */,
	EAP_TYPE_TEAP = 55 /* RFC 7170 */,
	EAP_TYPE_EXPANDED = 254 /* RFC 3748 */
};

enum ENUM_EAP_CODE {
	ENUM_EAP_CODE_NONE,
	ENUM_EAP_CODE_REQ,
	ENUM_EAP_CODE_RESP,
	ENUM_EAP_CODE_SUCCESS,
	ENUM_EAP_CODE_FAIL,

	ENUM_EAP_CODE_NUM
};

/*******************************************************************************
 *                                 M A C R O S
 *******************************************************************************
 */
#ifndef WLAN_AKM_SUITE_FILS_SHA256
#define WLAN_AKM_SUITE_FILS_SHA256	0x000FAC0E
#endif
#ifndef WLAN_AKM_SUITE_FILS_SHA384
#define WLAN_AKM_SUITE_FILS_SHA384	0x000FAC0F
#endif
#ifndef WLAN_AKM_SUITE_FT_FILS_SHA256
#define WLAN_AKM_SUITE_FT_FILS_SHA256	0x000FAC10
#endif
#ifndef WLAN_AKM_SUITE_FT_FILS_SHA384
#define WLAN_AKM_SUITE_FT_FILS_SHA384	0x000FAC11
#endif
#ifndef WLAN_GET_SEQ_SEQ
#define WLAN_GET_SEQ_SEQ(seq) \
	(((seq) & (~(BIT(3) | BIT(2) | BIT(1) | BIT(0)))) >> 4)
#endif
#ifndef WPA_KEY_INFO_KEY_TYPE
#define WPA_KEY_INFO_KEY_TYPE BIT(3) /* 1 = PairwSd7ise, 0 = Group key */
#endif
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#ifndef WPA_KEY_INFO_KEY_INDEX_MASK
#define WPA_KEY_INFO_KEY_INDEX_MASK (BIT(4) | BIT(5))
#endif
#ifndef WPA_KEY_INFO_MIC
#define WPA_KEY_INFO_MIC BIT(8)
#endif
#ifndef WPA_KEY_INFO_ENCR_KEY_DATA
#define WPA_KEY_INFO_ENCR_KEY_DATA BIT(12) /* IEEE 802.11i/RSN only */
#endif
#ifndef AES_BLOCK_SIZE
#define AES_BLOCK_SIZE 16
#endif
#ifndef ieee802_1x_hdr_size
/* struct ieee802_1x_hdr in wpa_supplicant */
#define ieee802_1x_hdr_size 4
#endif
#ifndef wpa_eapol_key_key_info_offset
/* struct wpa_eapol_key in wpa_supplicant */
#define wpa_eapol_key_key_info_offset 1
#endif
#ifndef wpa_eapol_key_fixed_field_size
#define wpa_eapol_key_fixed_field_size 77
#endif

/*******************************************************************************
 *                              F U N C T I O N S
 *******************************************************************************
 */
struct PARAM_CONNECTIVITY_LOG {
	uint8_t id;
	uint8_t len;
	uint8_t msg[];
};

#define MAX_LOG_SIZE 258
#define MAX_LOG_NUM 32
#define MAX_BUF_NUM 32
#define MAX_AUTH_BUF_NUM 8

struct CONNECTIVITY_LOG_INFO {
	uint8_t fgIsUsed;
	uint32_t u4LogSize;
	uint8_t ucBssIndex;
	uint8_t ucLogTime[MAX_LOG_SIZE];
} gConnectivityLog[MAX_LOG_NUM];

struct BUFFERED_LOG_ENTRY gBufferedLog[MAX_BUF_NUM][MAX_BSSID_NUM];
struct BUFFERED_LOG_ENTRY gAuthBufferedLog[MAX_AUTH_BUF_NUM][MAX_BSSID_NUM];

void kalReportWifiLog(struct ADAPTER *prAdapter, uint8_t ucBssIndex,
				uint8_t *log)
{
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	uint32_t size = sizeof(struct PARAM_CONNECTIVITY_LOG);
	u64 ts;
	unsigned long rem_nsec;
	char *log_time = NULL;
	uint32_t log_len = 0;
	uint8_t log_idx;
	uint8_t i;

	wiphy = wlanGetWiphy();
	if (!wiphy)
		return;

	if (!wlanGetNetDev(prAdapter->prGlueInfo,
		ucBssIndex))
		return;
	wdev = wlanGetNetDev(prAdapter->prGlueInfo,
		ucBssIndex)->ieee80211_ptr;

	/*
	 * if (IS_FEATURE_DISABLED(prAdapter->rWifiVar.ucLogEnhancement))
	 *	return;
	 */
	for (i = 0; i < MAX_LOG_NUM; i++) {
		if (gConnectivityLog[i].fgIsUsed == FALSE) {
			log_time = gConnectivityLog[i].ucLogTime;
			log_idx = i;
			gConnectivityLog[i].fgIsUsed = TRUE;
			break;
		}
	}

	if (!log_time) {
		DBGLOG(AIS, INFO, "log_time is NULL\n");
		return;
	}

	ts = local_clock();
	rem_nsec = do_div(ts, 1000000000);
	kalSnprintf(log_time, MAX_LOG_SIZE,
		"[%5lu.%06lu]%s",
		(unsigned long)ts,
		rem_nsec / 1000,
		log);
	log_len = strlen(log_time) + 1;
	size += log_len;

	gConnectivityLog[log_idx].u4LogSize = size;
	gConnectivityLog[log_idx].ucBssIndex = ucBssIndex;

	DBGLOG(AIS, INFO, "%s[size(%d)]\n", log_time, size);
#if (CFG_SUPPORT_CONN_LOG == 1)
	kalReportWiFiLogSet(prAdapter);
#endif
	return;
}

void __kalReportWifiLog(struct ADAPTER *prAdapter)
{
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct PARAM_CONNECTIVITY_LOG *log_info;
	char *log_time = NULL;
	uint8_t ucBssIndex;
	uint32_t size;
	uint8_t i;

	wiphy = wlanGetWiphy();
	if (!wiphy)
		return;

	for (i = 0; i < MAX_LOG_NUM; i++) {
		if (gConnectivityLog[i].fgIsUsed == TRUE) {
			ucBssIndex = gConnectivityLog[i].ucBssIndex;
			if (!wlanGetNetDev(prAdapter->prGlueInfo,
				ucBssIndex))
				return;
			wdev = wlanGetNetDev(prAdapter->prGlueInfo,
				ucBssIndex)->ieee80211_ptr;

			size = gConnectivityLog[i].u4LogSize;
			log_time = gConnectivityLog[i].ucLogTime;
			if (size <= sizeof(struct PARAM_CONNECTIVITY_LOG)) {
				DBGLOG(AIS, ERROR,
					"size(%d) is too short.\n", size);
				return;
			}

			log_info = kalMemAlloc(MAX_LOG_SIZE, VIR_MEM_TYPE);
			if (!log_info) {
				DBGLOG(AIS, ERROR,
					"alloc mgmt chnl list event fail\n");
				return;
			}

			kalMemZero(log_info, MAX_LOG_SIZE);
			log_info->id = GRID_SWPIS_CONNECTIVITY_LOG;
			if (size < MAX_LOG_SIZE)
				log_info->len = size - 2;
			else {
				log_info->len = 255;
				size = log_info->len + 2;
			}

			kalMemCopy(log_info->msg, log_time, log_info->len);
			log_info->msg[log_info->len] = '\0';

			mtk_cfg80211_vendor_event_generic_response(
				wiphy, wdev, size, (uint8_t *)log_info);

			kalMemFree(log_info, VIR_MEM_TYPE, MAX_LOG_SIZE);
			gConnectivityLog[i].fgIsUsed = FALSE;
		}
	}
}

#if CFG_SUPPORT_WPA3_LOG
void wpa3LogAuthTimeout(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec)
{
#if (CFG_EXT_VERSION > 1)
	struct BSS_INFO *prBssInfo;

	prBssInfo = aisGetAisBssInfo(prAdapter,
		prStaRec->ucBssIndex);

	if (prBssInfo &&
		rsnKeyMgmtSae(prBssInfo->u4RsnSelectedAKMSuite) &&
		prStaRec->ucAuthAlgNum ==
		AUTH_ALGORITHM_NUM_OPEN_SYSTEM) {
		DBGLOG(SAA, INFO, "WPA3 auth open no response!\n");
		prStaRec->u2StatusCode = WPA3_AUTH_OPEN_NO_RESP;
	}
#else
	struct CONNECTION_SETTINGS *prConnSettings;

	prConnSettings = aisGetConnSettings(prAdapter,
		prStaRec->ucBssIndex);

	if (prConnSettings->rRsnInfo.au4AuthKeyMgtSuite[0] ==
		WLAN_AKM_SUITE_SAE &&
		prStaRec->ucAuthAlgNum ==
		AUTH_ALGORITHM_NUM_OPEN_SYSTEM) {
		DBGLOG(SAA, INFO, "WPA3 auth open no response!\n");
		prStaRec->u2StatusCode = WPA3_AUTH_OPEN_NO_RESP;
	}
#endif
}

void wpa3LogAssocTimeout(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec)
{
	struct BSS_INFO *prBssInfo;

	prBssInfo = aisGetAisBssInfo(prAdapter,
		prStaRec->ucBssIndex);

	if (prBssInfo &&
		prBssInfo->u4RsnSelectedAKMSuite
		== WLAN_AKM_SUITE_SAE) {
		DBGLOG(SAA, INFO, "WPA3 assoc no response!\n");
		prStaRec->u2StatusCode = WPA3_ASSOC_NO_RESP;
	}
}

void wpa3LogConnect(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct CONNECTION_SETTINGS *prConnSettings;

	prConnSettings = aisGetConnSettings(
		prAdapter,
		ucBssIndex);

	if (prConnSettings)
		prConnSettings->u2JoinStatus =
			WLAN_STATUS_AUTH_TIMEOUT;
}

void wpa3Log6gPolicyFail(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	enum ENUM_PARAM_AUTH_MODE eAuthMode)
{
	struct BSS_INFO *prBssInfo;

	prBssInfo = aisGetAisBssInfo(prAdapter,
		ucBssIndex);

	if (prBssInfo &&
		(eAuthMode == AUTH_MODE_WPA3_SAE))
		aisGetConnSettings(prAdapter,
		prBssInfo->ucBssIndex)
		->u2JoinStatus = WPA3_6E_REJECT_NO_H2E;
}

void wpa3LogJoinFail(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo)
{
	struct CONNECTION_SETTINGS *prConnSettings;
	prConnSettings = aisGetConnSettings(prAdapter,
		prBssInfo->ucBssIndex);

	if (prBssInfo->u4RsnSelectedAKMSuite ==
		WLAN_AKM_SUITE_SAE) {
		DBGLOG(AIS, INFO,
			"WPA3 auth no response!\n");
		prConnSettings->u2JoinStatus =
			WPA3_AUTH_SAE_NO_RESP;
#if CFG_STAINFO_FEATURE
		if (IS_BSS_INDEX_AIS(prAdapter, prBssInfo->ucBssIndex)) {
			prAdapter->u2ConnRejectStatus
				= AUTH_NO_RESP;
		}
#endif
	}
}

uint16_t wpa3LogJoinFailStatus(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo)
{
	uint16_t u2JoinStatus;

	if (prBssInfo->u4RsnSelectedAKMSuite
			== WLAN_AKM_SUITE_SAE) {
		DBGLOG(INIT, DEBUG, "WPA3: cannot find network\n");
		u2JoinStatus = WPA3_NO_NETWORK_FOUND;
	} else
		u2JoinStatus = WLAN_STATUS_AUTH_TIMEOUT;

	return u2JoinStatus;
}

void wpa3LogExternalAuth(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	uint16_t status)
{
	if ((status == WLAN_STATUS_SUCCESS) &&
		(prStaRec->eAuthAssocState == SAA_STATE_EXTERNAL_AUTH))
		prStaRec->u2StatusCode = STATUS_CODE_SUCCESSFUL;
}

void wpa3LogMgmtTx(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	uint8_t ucBssIndex = prMsduInfo->ucBssIndex;
	struct AIS_FSM_INFO *prAisFsmInfo =
		aisGetAisFsmInfo(prAdapter, ucBssIndex);
	struct CONNECTION_SETTINGS *prConnSettings;


	if (rTxDoneStatus == TX_RESULT_SUCCESS)
		return;

	/* Only report fail case */

	prConnSettings = aisGetConnSettings(prAdapter,
		ucBssIndex);

	if (ucBssIndex == NETWORK_TYPE_AIS
		&& prAisFsmInfo->ucAvailableAuthTypes
		== AUTH_TYPE_SAE) {
		DBGLOG(AIS, INFO,
			"WPA3 auth SAE mgmt TX fail!\n");
		if (rTxDoneStatus == TX_RESULT_MPDU_ERROR) {
			prConnSettings->u2JoinStatus
				= WPA3_AUTH_SAE_NO_ACK;
#if CFG_STAINFO_FEATURE
			prAdapter->u2ConnRejectStatus
				= AUTH_NO_ACK;
#endif
		} else {
			prConnSettings->u2JoinStatus
				= WPA3_AUTH_SAE_SENDING_FAIL;
#if CFG_STAINFO_FEATURE
			prAdapter->u2ConnRejectStatus
				= AUTH_SENDING_FAIL;
#endif
		}
	}
}

void wpa3LogSaaTx(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	struct STA_RECORD *prStaRec;
	struct BSS_INFO *prBssInfo;

	if (rTxDoneStatus == TX_RESULT_SUCCESS)
		return;

	/* Only report fail case */

	prStaRec = cnmGetStaRecByIndex(
		prAdapter,
		prMsduInfo->ucStaRecIndex);
	if (!prStaRec)
		return;

	prBssInfo = aisGetAisBssInfo(prAdapter,
		prStaRec->ucBssIndex);

	if (prBssInfo &&
		prBssInfo->u4RsnSelectedAKMSuite
		== WLAN_AKM_SUITE_SAE
		&& prStaRec->ucAuthAlgNum
		== AUTH_ALGORITHM_NUM_OPEN_SYSTEM) {
		DBGLOG(SAA, INFO,
			"WPA3 auth open TX fail!\n");
		if (rTxDoneStatus
			== TX_RESULT_MPDU_ERROR) {
			prStaRec->u2StatusCode
			= WPA3_AUTH_OPEN_NO_ACK;
		} else {
			prStaRec->u2StatusCode
			= WPA3_AUTH_OPEN_SENDING_FAIL;
		}
	}
}

void wpa3LogSaaStart(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec)
{
	if (prStaRec->ucAuthAlgNum ==
		AUTH_ALGORITHM_NUM_SAE)
		prStaRec->u2StatusCode =
			WPA3_AUTH_SAE_NO_RESP;
}
#endif

#if CFG_SUPPORT_REPORT_LOG
void wnmLogBTMRecvReq(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct ACTION_BTM_REQ_FRAME *prRxFrame,
	uint16_t u2NRLen)
{
	char log[256] = {0};
	uint16_t u2Offset = 0;
	/* uint16_t u2NRLen = prSwRfb->u2PacketLen - u2TmpLen; */
	uint8_t ucNRCount = 0;
	uint8_t *pucIe = &prRxFrame->aucOptInfo[0];
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	IE_FOR_EACH(pucIe, u2NRLen, u2Offset) {
		if (IE_ID(pucIe) != ELEM_ID_NEIGHBOR_REPORT)
			continue;
		ucNRCount++;
	}

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (!fgIsMldAp) {
		kalSprintf(log,
			"[BTM] REQ token=%d mode=%d disassoc=%d validity=%d candidate_list_cnt=%d",
			prRxFrame->ucDialogToken, prRxFrame->ucRequestMode,
			prRxFrame->u2DisassocTimer,
			prRxFrame->ucValidityInterval,
			ucNRCount < WNM_MAX_NEIGHBOR_REPORT ?
				ucNRCount : WNM_MAX_NEIGHBOR_REPORT);
	} else {
		kalSprintf(log,
			"[BTM] REQ band=%s token=%d mode=%d disassoc=%d validity=%d candidate_list_cnt=%d",
			apucExtBandStr[prBssInfo->eBand],
			prRxFrame->ucDialogToken, prRxFrame->ucRequestMode,
			prRxFrame->u2DisassocTimer,
			prRxFrame->ucValidityInterval,
			ucNRCount < WNM_MAX_NEIGHBOR_REPORT ?
				ucNRCount : WNM_MAX_NEIGHBOR_REPORT);
	}
	kalReportWifiLog(prAdapter, ucBssIndex, log);
}

void wnmLogBTMReqCandiReport(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct NEIGHBOR_AP *prNeighborAP,
	uint32_t cnt)
{
	char log[256] = {0};

	if (prNeighborAP->fgFromBtm) {
		kalSprintf(log,
			"[BTM] REQ_CANDI[%d] bssid=" RPTMACSTR
			" preference=%d",
			cnt, RPTMAC2STR(prNeighborAP->aucBssid),
			prNeighborAP->ucPreference);
		cnt++;
		kalReportWifiLog(prAdapter, ucBssIndex, log);
	}
}

void wnmLogBTMRespReport(
	struct ADAPTER *adapter,
	struct MSDU_INFO *prMsduInfo,
	uint8_t dialogToken,
	uint8_t status,
	uint8_t reason,
	uint8_t delay,
	const uint8_t *bssid)
{
	char log[256] = {0};
	const uint8_t aucZeroMacAddr[] = NULL_MAC_ADDR;
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	prBssInfo = GET_BSS_INFO_BY_INDEX(adapter, prMsduInfo->ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(adapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (!fgIsMldAp) {
		kalSprintf(log,
			"[BTM] RESP token=%d status=%d delay=%d target=" RPTMACSTR,
			dialogToken, status, delay,
			RPTMAC2STR(bssid ? bssid : aucZeroMacAddr));
	} else {
		kalSprintf(log,
			"[BTM] RESP band=%s token=%d status=%d delay=%d target=" RPTMACSTR,
			apucExtBandStr[prBssInfo->eBand],
			dialogToken, status, delay,
			RPTMAC2STR(bssid ? bssid : aucZeroMacAddr));
	}
	kalReportWifiLog(adapter, prMsduInfo->ucBssIndex, log);

}

void wnmLogBTMQueryReport(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct ACTION_BTM_QUERY_FRAME *prTxFrame)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, prStaRec->ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if(!fgIsMldAp) {
		kalSprintf(log,
			"[BTM] QUERY token=%d reason=%d",
			prTxFrame->ucDialogToken, prTxFrame->ucQueryReason);
	} else {
		kalSprintf(log,
			"[BTM] QUERY band=%s token=%d reason=%d",
			apucExtBandStr[prBssInfo->eBand],
			prTxFrame->ucDialogToken, prTxFrame->ucQueryReason);
	}
	kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
}

void wnmBTMReqReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucReasonCode,
	uint8_t ucSubCode)
{
	char aucLog[256] = {0};

	if (!prAdapter)
		return;

	kalSprintf(aucLog,
		"[BTM] WTC reason=%u sub_code=%u duration=0",
		ucReasonCode, ucSubCode);

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void wnmBTMRespReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucReasonCode)
{
	char aucLog[256] = {0};

	if (!prAdapter)
		return;

	kalSprintf(aucLog,
		"[BTM] WTC reason_code=%u",
		ucReasonCode);

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void rrmReqNeighborReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucToken,
	uint8_t *pucSsid,
	uint8_t ucSSIDLen)
{
	char aucLog[256] = {0};
	uint8_t reqSsid[ELEM_MAX_LEN_SSID + 1] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	if (!prAdapter)
		return;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	kalMemCopy(reqSsid, pucSsid,
		kal_min_t(uint8_t, ucSSIDLen, ELEM_MAX_LEN_SSID));

	if (!fgIsMldAp) {
		kalSprintf(aucLog,
			"[NBR_RPT] REQ token=%u ssid=\"%s\"",
			ucToken, reqSsid);
	} else {
		kalSprintf(aucLog,
			"[NBR_RPT] REQ band=%s token=%u ssid=\"%s\"",
			apucExtBandStr[prBssInfo->eBand],
			ucToken, reqSsid);
	}

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void rrmRespNeighborReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucToken)
{
#if CFG_SUPPORT_802_11K
	struct LINK *prNeighborAPLink;
	struct AIS_SPECIFIC_BSS_INFO *prAisSpecBssInfo;
	struct NEIGHBOR_AP *prNeiAP = NULL;
	char aucLog[256] = {0};
	char aucScanChannel[200] = {0};
	uint32_t channels[MAX_CHN_NUM];
	uint32_t num_channels = 0;
	uint32_t freq = 0;
	uint32_t j = 0;
	uint32_t i = 0;
	uint8_t ucIdx = 0, ucPos = 0, ucAvailableLen = 195, ucMaxLen = 195;
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	if (!prAdapter)
		return;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	prAisSpecBssInfo =
		aisGetAisSpecBssInfo(prAdapter, ucBssIndex);
	if (!prAisSpecBssInfo) {
		log_dbg(SCN, INFO, "No prAisSpecBssInfo\n");
		return;
	}

	kalMemZero(channels, sizeof(channels));

	prNeighborAPLink = &prAisSpecBssInfo->rNeighborApList.rUsingLink;
	LINK_FOR_EACH_ENTRY(prNeiAP, prNeighborAPLink,
		rLinkEntry, struct NEIGHBOR_AP) {
		freq = nicChannelNum2Freq(
			prNeiAP->ucChannel,
			prNeiAP->eBand) / 1000;

		if (freq) {
			u_int8_t fgFound = FALSE;

			for (i = 0 ; i < j; i++) {
				if (freq == channels[i]) {
					fgFound = TRUE;
					break;
				}
			}

			if (!fgFound)
				channels[j++] = freq;
		}
	}

	num_channels = j;

	for (ucIdx = 0; (ucIdx <
		num_channels &&
		ucAvailableLen > 0); ucIdx++) {
		ucPos +=
			kalSnprintf(aucScanChannel + ucPos,
			ucMaxLen - ucPos,
			"%u ", channels[ucIdx]);
		ucAvailableLen =
			(ucMaxLen > ucPos) ? (ucMaxLen - ucPos) : 0;
		if (!ucAvailableLen)
			aucScanChannel[199] = '\0';
	}

	if (!fgIsMldAp) {
		kalSprintf(aucLog,
			"[NBR_RPT] RESP token=%u freq[%d]=%s report_number=%d",
			ucToken,
			num_channels,
			strlen(aucScanChannel) ? aucScanChannel : "0",
			prNeighborAPLink->u4NumElem);
	} else {
		kalSprintf(aucLog,
			"[NBR_RPT] RESP band=%s token=%u freq[%d]=%s report_number=%d",
			apucExtBandStr[prBssInfo->eBand],
			ucToken,
			num_channels,
			strlen(aucScanChannel) ? aucScanChannel : "0",
			prNeighborAPLink->u4NumElem);
	}

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
#endif
}

static char *rrmRequestModeToText(uint8_t ucMode)
{
	switch (ucMode) {
	case RM_BCN_REQ_PASSIVE_MODE: return "passive";
	case RM_BCN_REQ_ACTIVE_MODE: return "active";
	case RM_BCN_REQ_TABLE_MODE: return "beacon_table";
	default: return "";
	}
}

void rrmReqBeaconReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucToken,
	uint8_t ucRequestMode,
	uint8_t ucOpClass,
	uint8_t ucChannelNum,
	uint32_t u4Duration,
	uint32_t u4Mode)
{
	char aucLog[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	if (!prAdapter)
		return;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (!fgIsMldAp) {
		kalSprintf(aucLog,
			"[BCN_RPT] REQ token=%u mode=%s operating_class=%u channel=%u duration=%u request_mode=0x%02x",
			ucToken,
			rrmRequestModeToText(ucRequestMode),
			ucOpClass,
			ucChannelNum,
			u4Duration,
			u4Mode);
	} else {
		kalSprintf(aucLog,
			"[BCN_RPT] REQ band=%s token=%u mode=%s operating_class=%u channel=%u duration=%u request_mode=0x%02x",
			apucExtBandStr[prBssInfo->eBand],
			ucToken,
			rrmRequestModeToText(ucRequestMode),
			ucOpClass,
			ucChannelNum,
			u4Duration,
			u4Mode);
	}

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void rrmRespBeaconReportLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t ucToken,
	uint32_t u4ReportNum)
{
	char aucLog[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	if (!prAdapter)
		return;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (!fgIsMldAp) {
		kalSprintf(aucLog,
			"[BCN_RPT] RESP token=%u report_number=%u",
			ucToken,
			u4ReportNum);
	} else {
		kalSprintf(aucLog,
			"[BCN_RPT] RESP band=%s token=%u report_number=%u",
			apucExtBandStr[prBssInfo->eBand],
			ucToken,
			u4ReportNum);
	}

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}
#endif

#if CFG_SUPPORT_SCAN_LOG
#define MAX_BCNINFO_SIZE 4096

void scanUpdateBcn(
	struct ADAPTER *prAdapter,
	struct BSS_DESC *prBss,
	uint8_t ucBssIndex)
{
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct SCAN_INFO *prScanInfo;
	struct PARAM_BCN_INFO *bcnInfo;
	uint32_t size = sizeof(struct PARAM_BCN_INFO);
	uint8_t i;

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	if (prScanInfo && !prScanInfo->fgBcnReport) {
		DBGLOG(AIS, TRACE,
			"[SWIPS] beacon report not started\n");
		return;
	}

	if (ucBssIndex >= KAL_AIS_NUM) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] Dont update within other BSS\n");
		return;
	}

	wiphy = wlanGetWiphy();
	wdev = wlanGetNetDev(
		prAdapter->prGlueInfo, ucBssIndex)->ieee80211_ptr;

	if (size > MAX_BCNINFO_SIZE) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] size is too big\n");
		return;
	}

	bcnInfo = kalMemAlloc(size, VIR_MEM_TYPE);
	if (!bcnInfo) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] beacon report event alloc fail\n");
		return;
	}

	kalMemZero(bcnInfo, size);
	bcnInfo->id = GRID_SWPIS_BCN_INFO;
	bcnInfo->len = size - 2;
	COPY_SSID(bcnInfo->ssid, i,
		  prBss->aucSSID, prBss->ucSSIDLen);
	COPY_MAC_ADDR(bcnInfo->bssid, prBss->aucBSSID);
	bcnInfo->channel = prBss->ucChannelNum;
	bcnInfo->bcnInterval = prBss->u2BeaconInterval;
	bcnInfo->timeStamp[0] = prBss->u8TimeStamp.u.LowPart;
	bcnInfo->timeStamp[1] = prBss->u8TimeStamp.u.HighPart;
	bcnInfo->sysTime = prBss->rUpdateTime;

	DBGLOG(INIT, DEBUG, "[SWIPS] Send[%d:%d] %s:%d (" MACSTR
	       ") with ch:%d interval:%d tsf:%u %u sys:%lu\n",
	       bcnInfo->id, bcnInfo->len, bcnInfo->ssid, i,
	       bcnInfo->bssid, bcnInfo->channel, bcnInfo->bcnInterval,
	       bcnInfo->timeStamp[0], bcnInfo->timeStamp[1], bcnInfo->sysTime);
	DBGLOG_MEM8(INIT, TRACE, bcnInfo, size);

	mtk_cfg80211_vendor_event_generic_response(
		wiphy, wdev, size, (uint8_t *)bcnInfo);

	kalMemFree(bcnInfo, VIR_MEM_TYPE, size);
}

void scanAbortBeaconRecv(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	enum ABORT_REASON abortReason)
{
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct PARAM_BCN_INFO_ABORT *bcnAbort;
	uint32_t size = sizeof(struct PARAM_BCN_INFO_ABORT);
	struct SCAN_INFO *prScanInfo =
		&(prAdapter->rWifiVar.rScanInfo);
	struct CMD_SET_REPORT_BEACON_STRUCT beaconRecv = {0};

	if (prScanInfo->fgBcnReport == FALSE) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] Dont abort, SWIPS already OFF\n");
		return;
	}

	if (ucBssIndex >= KAL_AIS_NUM) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] Dont abort within other BSS\n");
		return;
	}

	wiphy = wlanGetWiphy();
	wdev = wlanGetNetDev(prAdapter->prGlueInfo,
		ucBssIndex)->ieee80211_ptr;

	bcnAbort = kalMemAlloc(size, VIR_MEM_TYPE);
	if (!bcnAbort) {
		DBGLOG(AIS, ERROR,
			"[SWIPS] beacon report abort event alloc fail\n");
		return;
	}

	/* Send abort event to supplicant */
	bcnAbort->id = GRID_SWPIS_BCN_INFO_ABORT;
	bcnAbort->len = size - 2;
	bcnAbort->abort = abortReason;

	DBGLOG(INIT, DEBUG,
		"[SWIPS] Abort beacon report with reason: %d\n",
		bcnAbort->abort);

	mtk_cfg80211_vendor_event_generic_response(
		wiphy, wdev, size, (uint8_t *)bcnAbort);
	kalMemFree(bcnAbort, VIR_MEM_TYPE, size);

	/* Send stop SWIPS to FW */
	prScanInfo->fgBcnReport = FALSE;
	beaconRecv.ucReportBcnEn = FALSE;

	wlanSendSetQueryCmd(prAdapter,
		CMD_ID_SET_REPORT_BEACON,
		TRUE,
		FALSE,
		FALSE,
		nicCmdEventSetCommon,
		nicOidCmdTimeoutCommon,
		sizeof(struct CMD_SET_REPORT_BEACON_STRUCT),
		(uint8_t *) &beaconRecv, NULL, 0);
}

uint32_t
wlanoidSetBeaconRecv(
	struct ADAPTER *prAdapter,
	void *pvSetBuffer,
	uint32_t u4SetBufferLen,
	uint32_t *pu4SetInfoLen)
{
	struct CMD_SET_REPORT_BEACON_STRUCT beaconRecv = {0};
	struct SCAN_INFO *prScanInfo =
		&(prAdapter->rWifiVar.rScanInfo);

	beaconRecv.ucReportBcnEn = *((uint8_t *) pvSetBuffer);
	if (beaconRecv.ucReportBcnEn == prScanInfo->fgBcnReport) {
		DBGLOG(INIT, DEBUG, "[SWIPS] No need to %s again\n",
		       (beaconRecv.ucReportBcnEn == TRUE) ? "START" : "STOP");
		return WLAN_STATUS_SUCCESS;
	}

	DBGLOG(INIT, DEBUG, "[SWIPS] Set: %d\n", beaconRecv.ucReportBcnEn);

	prScanInfo->fgBcnReport = beaconRecv.ucReportBcnEn;

	return wlanSendSetQueryCmd(prAdapter,
		CMD_ID_SET_REPORT_BEACON,
		TRUE,
		FALSE,
		TRUE,
		nicCmdEventSetCommon,
		nicOidCmdTimeoutCommon,
		sizeof(struct CMD_SET_REPORT_BEACON_STRUCT),
		(uint8_t *) &beaconRecv, NULL, 0);
}

#endif

#if (CFG_SUPPORT_ROAMING_LOG == 1)
void roamingFsmLogScanStart(struct ADAPTER *prAdapter,
	uint8_t ucBssIndex, uint8_t fgIsFullScn,
	struct BSS_DESC *prBssDesc)
{
	struct AIS_FSM_INFO *prAisFsmInfo;
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;

	prBssInfo =
		GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(ROAMING, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	prAisFsmInfo = aisGetAisFsmInfo(prAdapter, ucBssIndex);

	if (roamingFsmIsDiscovering(prAdapter, ucBssIndex) &&
		(prAisFsmInfo->eCurrentState ==
		AIS_STATE_LOOKING_FOR)) {
		struct ROAMING_INFO *prRoamInfo;
		uint32_t u4CannelUtilization = 0;
		uint8_t ucIsValidCu = FALSE;
		char aucLog[256] = {0};
		char aucFwTime[32] = {0};

		prRoamInfo = aisGetRoamingInfo(prAdapter, ucBssIndex);
		ucIsValidCu = (prBssDesc && prBssDesc->fgExistBssLoadIE);
		if (ucIsValidCu)
			u4CannelUtilization =
				(prBssDesc->ucChnlUtilization * 100 / 255);

		if (prAisFsmInfo->fgTargetChnlScanIssued == FALSE)
			prRoamInfo->rScanCadence.ucFullScanCount++;

		roamingFsmLogScanInit(prAdapter, ucBssIndex);

		if (prRoamInfo->eReason == ROAMING_REASON_POOR_RCPI ||
		    prRoamInfo->eReason == ROAMING_REASON_HIGH_CU ||
		    prRoamInfo->eReason == ROAMING_REASON_IDLE ||
		    prRoamInfo->eReason == ROAMING_REASON_BT_COEX)
			kalSprintf(aucFwTime, " [fw_time=%u]", prRoamInfo->u4RoamingFwTime*1000);

		if (!fgIsMldAp)
			kalSprintf(aucLog,
				"[ROAM] SCAN_START reason=%d rssi=%d cu=%d full_scan=%d rssi_thres=%d%s",
				apucRoamingReasonToLog[prRoamInfo->eReason],
				RCPI_TO_dBm(prRoamInfo->ucRcpi),
				ucIsValidCu ? u4CannelUtilization : -1,
				fgIsFullScn,
				RCPI_TO_dBm(prRoamInfo->ucThreshold),
				aucFwTime);
		else
			kalSprintf(aucLog,
				"[ROAM] SCAN_START reason=%d rssi=%d cu=%d full_scan=%d rssi_thres=%d band=%s%s",
				apucRoamingReasonToLog[prRoamInfo->eReason],
				RCPI_TO_dBm(prRoamInfo->ucRcpi),
				ucIsValidCu ? u4CannelUtilization : -1,
				fgIsFullScn,
				RCPI_TO_dBm(prRoamInfo->ucThreshold),
				apucExtBandStr[prBssInfo->eBand],
				aucFwTime);

		kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
	}
}

void roamingFsmLogScanDoneImpl(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint16_t u2ApNum)
{
	struct WIFI_VAR *prWifiVar;
	struct SCAN_INFO *prScanInfo;
	char aucLog[256] = {0};

	prWifiVar = &prAdapter->rWifiVar;
	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	kalSprintf(aucLog,
		"[ROAM] SCAN_DONE btcoex=%d ap_count=%d freq[%d]=%s",
#if (CFG_SUPPORT_APS == 1)
		aisGetApsInfo(prAdapter, ucBssIndex)->fgIsGBandCoex,
#else
		0,
#endif
		u2ApNum,
		prWifiVar->u4FreqNum,
		prWifiVar->rScanDoneFreqLog);

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
	prWifiVar->fgIsScanStarted = FALSE;
}

void roamingFsmLogScanDone(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint16_t u2ApNum)
{
	/* LOG: scan start */
	if (roamingFsmIsDiscovering(prAdapter, ucBssIndex)) {
		struct AIS_FSM_INFO *prAisFsmInfo;

		prAisFsmInfo = aisGetAisFsmInfo(prAdapter,
			ucBssIndex);
		if (prAisFsmInfo->eCurrentState ==
			AIS_STATE_LOOKING_FOR)
			roamingFsmLogScanDoneImpl(
				prAdapter,
				ucBssIndex,
				u2ApNum);
	}
}

void roamingFsmLogScanBuffer(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct WIFI_VAR *prWifiVar;
	struct SCAN_INFO *prScanInfo;
	uint8_t ucIdx = 0;;

	prWifiVar = &prAdapter->rWifiVar;
	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

	if (prWifiVar->fgIsScanStarted == FALSE)
		return;

	prWifiVar->u4FreqNum += prScanInfo->ucSparseChannelArrayValidNum;

	for (ucIdx = 0; (ucIdx < prScanInfo->ucSparseChannelArrayValidNum &&
		prWifiVar->ucAvailableLen > 0); ucIdx++) {
		prWifiVar->ucPos += kalSnprintf(prWifiVar->rScanDoneFreqLog + prWifiVar->ucPos,
			prWifiVar->ucMaxLen - prWifiVar->ucPos,
			"%d ", KHZ_TO_MHZ(nicChannelNum2Freq(
				prScanInfo->aucChannelNum[ucIdx],
				prScanInfo->aeChannelBand[ucIdx])));
		prWifiVar->ucAvailableLen = (prWifiVar->ucMaxLen > prWifiVar->ucPos) ?
			(prWifiVar->ucMaxLen - prWifiVar->ucPos) : 0;
		if (!prWifiVar->ucAvailableLen)
			prWifiVar->rScanDoneFreqLog[199] = '\0';
	}
}

void roamingFsmLogScanInit(
	struct ADAPTER * prAdapter,
	uint8_t ucBssIndex)
{
	struct WIFI_VAR *prWifiVar = &prAdapter->rWifiVar;

	prWifiVar->fgIsScanStarted = TRUE;
	prWifiVar->ucPos = 0;
	prWifiVar->ucAvailableLen = 195;
	prWifiVar->ucMaxLen = 195;
	prWifiVar->u4FreqNum = 0;

	kalMemZero(prWifiVar->rScanDoneFreqLog,
		sizeof(prWifiVar->rScanDoneFreqLog));
}

static uint8_t g_ApCnt;
void roamingFsmLogCurr(
	struct ADAPTER *prAdapter,
	struct BSS_DESC *prBssDesc,
	enum ENUM_ROAMING_REASON eRoamReason,
	uint8_t ucBssIndex)
{
	if (prBssDesc && roamingFsmIsDiscovering(prAdapter, ucBssIndex)) {
		roamingFsmLogSocre(prAdapter, "SCORE_CUR_AP", ucBssIndex,
			prBssDesc, prBssDesc->u2Score, 0);

	}

	g_ApCnt = 0;
}

void roamingFsmLogCandi(
	struct ADAPTER *prAdapter,
	struct BSS_DESC *prBssDesc,
	enum ENUM_ROAMING_REASON eRoamReason,
	uint8_t ucBssIndex)
{
	struct AIS_FSM_INFO *aisFsmInfo =
		aisGetAisFsmInfo(prAdapter, ucBssIndex);

	if (roamingFsmIsDiscovering(prAdapter, ucBssIndex) &&
	    !(IS_AIS_CONN_BSSDESC(aisFsmInfo, prBssDesc)) &&

	    apsIsGoodRCPI(prAdapter, prBssDesc, eRoamReason, ucBssIndex)) {
		char log[32] = {0};

		kalSprintf(log, "SCORE_CANDI[%d]", g_ApCnt++);
		roamingFsmLogSocre(prAdapter, log, ucBssIndex, prBssDesc,
			prBssDesc->u2Score, prBssDesc->u4Tput);
	}
}

void roamingFsmLogSocre(struct ADAPTER *prAdapter, uint8_t *prefix,
	uint8_t ucBssIndex, struct BSS_DESC *prBssDesc, uint32_t u4Score,
	uint32_t u4Tput)
{
	char aucLog[256] = {0};
	char aucTput[24] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;

	prBssInfo =
		GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);

	if (!prBssDesc)
		return;

#if (CFG_SUPPORT_802_11BE == 1)
	fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (u4Tput)
		kalSprintf(aucTput, " tp=%dkbps", u4Tput);

	if (!fgIsMldAp)
		kalSprintf(aucLog,
			"[ROAM] %s bssid=" RPTMACSTR
			" freq=%d rssi=%d cu=%d score=%d.%02d%s",
			prefix, RPTMAC2STR(prBssDesc->aucBSSID),
			KHZ_TO_MHZ(nicChannelNum2Freq(prBssDesc->ucChannelNum,
				prBssDesc->eBand)), RCPI_TO_dBm(prBssDesc->ucRCPI),
			(prBssDesc->fgExistBssLoadIE ?
				(prBssDesc->ucChnlUtilization * 100 / 255) : -1),
				u4Score / 100, u4Score % 100, aucTput);
	else
		kalSprintf(aucLog,
			"[ROAM] %s MLD bssid=" RPTMACSTR
			" freq=%d rssi=%d cu=%d score=%d.%02d%s",
			prefix, RPTMAC2STR(prBssDesc->aucBSSID),
			KHZ_TO_MHZ(nicChannelNum2Freq(prBssDesc->ucChannelNum,
				prBssDesc->eBand)), RCPI_TO_dBm(prBssDesc->ucRCPI),
			(prBssDesc->fgExistBssLoadIE ?
				(prBssDesc->ucChnlUtilization * 100 / 255) : -1),
				u4Score / 100, u4Score % 100, aucTput);

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void roamingFsmLogResultImpl(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct BSS_DESC *prSelectedBssDesc)
{
	char aucLog[256] = {0};
	uint8_t fgIsRoam =
		(prSelectedBssDesc && !prSelectedBssDesc->fgIsConnected);
	struct BSS_DESC *prBssDesc = (fgIsRoam ? prSelectedBssDesc :
		aisGetTargetBssDesc(prAdapter, ucBssIndex));

	if (fgIsRoam) {
		kalSprintf(aucLog, "[ROAM] RESULT %s bssid=" RPTMACSTR,
			"ROAM", RPTMAC2STR(prBssDesc->aucBSSID));
	} else
		kalSprintf(aucLog, "[ROAM] RESULT %s", "NO_ROAM");

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void roamingFsmLogResult(
	struct ADAPTER *ad,
	uint8_t bidx,
	struct BSS_DESC *cand)
{
	if (roamingFsmIsDiscovering(ad, bidx))
		roamingFsmLogResultImpl(ad, bidx, cand);
}

void roamingFsmLogCancelImpl(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t *pucReason)
{
	char aucLog[256] = {0};

	kalSprintf(aucLog, "[ROAM] CANCELED [%s]", pucReason);

	kalReportWifiLog(prAdapter, ucBssIndex, aucLog);
}

void roamingFsmLogCancel(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	if (roamingFsmIsDiscovering(prAdapter, ucBssIndex))
		roamingFsmLogCancelImpl(prAdapter, ucBssIndex,
			"STA disconnected");
}

uint8_t roamingFsmIsDiscovering(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct BSS_INFO *prAisBssInfo = NULL;
	struct ROAMING_INFO *prRoamingFsmInfo = NULL;
	uint8_t fgIsDiscovering = FALSE;

	prAisBssInfo = aisGetAisBssInfo(prAdapter,
		ucBssIndex);
	prRoamingFsmInfo = aisGetRoamingInfo(prAdapter,
		ucBssIndex);

	fgIsDiscovering =
		prAisBssInfo->eConnectionState == MEDIA_STATE_CONNECTED ||
		aisFsmIsInProcessPostpone(prAdapter, ucBssIndex);

	return fgIsDiscovering;
}
#endif

#if (CFG_SUPPORT_CONN_LOG == 1)

static uint32_t g_u4LatestDHCPTid;

void kalBufferWifiLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t *log,
	uint8_t ucSn)
{
	struct BUFFERED_LOG_ENTRY *prLogEntry;
	uint8_t i;

	/*
	 * if (IS_FEATURE_DISABLED(prAdapter->rWifiVar.ucLogEnhancement))
	 *	return;
	 */

	if (ucBssIndex >= MAX_BSSID_NUM)
		return;

	for (i = 0; i < MAX_BUF_NUM; i++) {
		prLogEntry = &gBufferedLog[i][ucBssIndex];
		if (prLogEntry->fgBuffered == FALSE) {
			kalMemZero(prLogEntry,
				sizeof(struct BUFFERED_LOG_ENTRY));

			prLogEntry->ucSn = ucSn;
			prLogEntry->fgBuffered = TRUE;

			kalMemCopy(prLogEntry->aucLog,
				log, sizeof(prLogEntry->aucLog));
			break;
		}
	}

	if (i == MAX_BUF_NUM) {
		for (i = 0; i < MAX_BUF_NUM; i++) {
			prLogEntry = &gBufferedLog[i][ucBssIndex];
			prLogEntry->fgBuffered = FALSE;
		}
		DBGLOG(AIS, INFO, "Full of Bufferd Logs(%d)\n", i);
	}

}

void kalRxDeauthBufferWifiLog(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t *log)
{
	struct BUFFERED_LOG_ENTRY *prLogEntry;
	uint8_t i;

	if (ucBssIndex >= MAX_BSSID_NUM)
		return;

	for (i = 0; i < MAX_AUTH_BUF_NUM; i++) {
		prLogEntry = &gAuthBufferedLog[i][ucBssIndex];
		if (prLogEntry->fgBuffered == FALSE) {
			kalMemZero(prLogEntry,
				sizeof(struct BUFFERED_LOG_ENTRY));

			prLogEntry->fgBuffered = TRUE;
			kalMemCopy(prLogEntry->aucLog,
				log, sizeof(prLogEntry->aucLog));
			break;
		}
	}

	if (i == MAX_AUTH_BUF_NUM) {
		for (i = 0; i < MAX_AUTH_BUF_NUM; i++) {
			prLogEntry = &gAuthBufferedLog[i][ucBssIndex];
			prLogEntry->fgBuffered = FALSE;
		}
		DBGLOG(AIS, INFO, "Full of AuthBufferd Logs(%d)\n", i);
	}

}

static char *eap_type_text(uint8_t type)
{
	switch (type) {
	case EAP_TYPE_IDENTITY: return "Identity";
	case EAP_TYPE_NOTIFICATION: return "Notification";
	case EAP_TYPE_NAK: return "Nak";
	case EAP_TYPE_TLS: return "TLS";
	case EAP_TYPE_TTLS: return "TTLS";
	case EAP_TYPE_PEAP: return "PEAP";
	case EAP_TYPE_SIM: return "SIM";
	case EAP_TYPE_GTC: return "GTC";
	case EAP_TYPE_MD5: return "MD5";
	case EAP_TYPE_OTP: return "OTP";
	case EAP_TYPE_FAST: return "FAST";
	case EAP_TYPE_SAKE: return "SAKE";
	case EAP_TYPE_PSK: return "PSK";
	default: return "Unknown";
	}
}

static char *connect_fail_reason(uint8_t type)
{
	switch (type) {
	case CONN_FAIL_UNKNOWN: return "Unknown";
	case CONN_FAIL_DISALLOWED_LIST: return "disallowed list";
	case CONN_FAIL_FWK_BLACLIST: return "FWK blacklist";
	case CONN_FAIL_RSN_MISMATCH: return "RSN mismatch";
	case CONN_FAIL_BLACLIST_LIMIT: return "blacklist limit";
	default: return "Unknown";
	}
}

static int wpa_mic_len(uint32_t akmp)
{
	switch (akmp) {
	case WLAN_AKM_SUITE_8021X_SUITE_B_192:
		return 24;
	case WLAN_AKM_SUITE_FILS_SHA256:
	case WLAN_AKM_SUITE_FILS_SHA384:
	case WLAN_AKM_SUITE_FT_FILS_SHA256:
	case WLAN_AKM_SUITE_FT_FILS_SHA384:
		return 0;
	default:
		return 16;
	}
}

void connLogEapKey(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint8_t eventType,
	uint8_t *pucEapol,
	uint8_t ucSn)
{
	enum EVENT_TYPE {
		EVENT_RX,
		EVENT_TX,
	};
	char log[256] = {0};
	uint16_t u2KeyDataLen = 0;
	uint8_t mic_len = 16;
	uint8_t key_data_len_offset; /* fixed field len + mic len*/
	uint8_t isPairwise = FALSE;
	uint16_t u2KeyInfo = 0;
	uint8_t m = 0;
	uint32_t u4RsnSelectedAKMSuite = 0;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif
#if (CFG_EXT_VERSION == 1)
	struct CONNECTION_SETTINGS *prConnSettings;

	prConnSettings =
		aisGetConnSettings(prAdapter, ucBssIndex);
	if (prConnSettings) {
		mic_len = wpa_mic_len(
		prConnSettings->rRsnInfo.au4AuthKeyMgtSuite[0]);
		u4RsnSelectedAKMSuite =
			prConnSettings->rRsnInfo.au4AuthKeyMgtSuite[0];
	}
#else
	struct BSS_INFO *prBssInfo = NULL;

	prBssInfo =
		GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (prBssInfo) {
		mic_len = wpa_mic_len(
		prBssInfo->u4RsnSelectedAKMSuite);
		u4RsnSelectedAKMSuite =
			prBssInfo->u4RsnSelectedAKMSuite;
	}
#endif

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	WLAN_GET_FIELD_BE16(&pucEapol[
		ieee802_1x_hdr_size
		+ wpa_eapol_key_key_info_offset],
		&u2KeyInfo);
	key_data_len_offset =
		ieee802_1x_hdr_size
		+ wpa_eapol_key_fixed_field_size
		+ mic_len;
	WLAN_GET_FIELD_BE16(&pucEapol[key_data_len_offset],
		&u2KeyDataLen);

#ifdef DBG_CONN_LOG_EAP
	DBGLOG(AIS, INFO,
		"akm=%x mic_len=%d key_data_len_offset=%d",
		u4RsnSelectedAKMSuite,
		mic_len, key_data_len_offset);
#endif

	switch (eventType) {
	case EVENT_RX:
		if (u2KeyInfo & WPA_KEY_INFO_KEY_TYPE) {
			if (u2KeyInfo
				& WPA_KEY_INFO_KEY_INDEX_MASK)
				DBGLOG(RX, WARN,
					"WPA: ignore EAPOL-key (pairwise) with non-zero key index\n");

			if (u2KeyInfo &
				(WPA_KEY_INFO_MIC
				| WPA_KEY_INFO_ENCR_KEY_DATA)) {
				m = 3;
				isPairwise = TRUE;
			} else {
				m = 1;
				isPairwise = TRUE;
			}
#ifdef DBG_CONN_LOG_EAP
			DBGLOG(RX, DEBUG,
				"<RX> EAPOL: key, M%d, KeyInfo 0x%04x KeyDataLen %d\n",
				m, u2KeyInfo, u2KeyDataLen);
#endif
		} else {
			if ((mic_len &&
					(u2KeyInfo
				& WPA_KEY_INFO_MIC)) ||
				(!mic_len &&
					(u2KeyInfo
				& WPA_KEY_INFO_ENCR_KEY_DATA)
				)) {
				m = 1;
				isPairwise = FALSE;
			} else {
				isPairwise = FALSE;
#ifdef DBG_CONN_LOG_EAP
				DBGLOG(RX, WARN,
					"WPA: EAPOL-Key (Group) without Mic/Encr bit\n");
#endif
			}
#ifdef DBG_CONN_LOG_EAP
			DBGLOG(RX, DEBUG,
				"<RX> EAPOL: group key, M%d, KeyInfo 0x%04x KeyDataLen %d\n",
				m, u2KeyInfo, u2KeyDataLen);
#endif
		}

		if (isPairwise) {
			if (!fgIsMldAp)
				kalSprintf(log, "[EAPOL] 4WAY M%d rx", m);
			else
				kalSprintf(log,
					"[EAPOL] 4WAY M%d band=%s rx", m,
					apucExtBandStr[prBssInfo->eBand]);
		} else {
			if (!fgIsMldAp)
				kalSprintf(log, "[EAPOL] GTK M%d rx", m);
			else
				kalSprintf(log,
					"[EAPOL] GTK M%d band=%s rx", m,
					apucExtBandStr[prBssInfo->eBand]);
		}
		kalReportWifiLog(prAdapter, ucBssIndex, log);
		break;
	case EVENT_TX:
		if ((u2KeyInfo & WPA_KEY_INFO_KEY_TYPE))
			isPairwise = TRUE;
		else
			isPairwise = FALSE;

		if ((u2KeyInfo & 0x1100) == 0x0000 ||
			(u2KeyInfo & 0x0008) == 0x0000)
			m = 1;
		else if ((u2KeyInfo & 0xfff0) == 0x0100)
			m = 2;
		else if ((u2KeyInfo & 0xfff0) == 0x13c0)
			m = 3;
		else if ((u2KeyInfo & 0xfff0) == 0x0300)
			m = 4;

#ifdef DBG_CONN_LOG_EAP
		DBGLOG(RX, DEBUG,
			"<TX> EAPOL: key, M%d, KeyInfo 0x%04x KeyDataLen %d isPairwise=%d\n",
			m, u2KeyInfo, u2KeyDataLen, isPairwise);
#endif

		if (isPairwise) {
			if (!fgIsMldAp)
				kalSprintf(log,
					"[EAPOL] 4WAY M%d", m);
			else
				kalSprintf(log,
					"[EAPOL] 4WAY M%d band=%s", m,
					apucExtBandStr[prBssInfo->eBand]);
		} else {
			if (!fgIsMldAp)
				kalSprintf(log,
					"[EAPOL] GTK M2");
			else
				kalSprintf(log,
					"[EAPOL] GTK M2 band=%s",
					apucExtBandStr[prBssInfo->eBand]);
		}
		kalBufferWifiLog(prAdapter, ucBssIndex, log,
			ucSn);
		break;
	}
}

void connLogDhcpRx(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint32_t u4Xid,
	uint32_t u4Opt)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	switch (u4Opt & 0xffffff00) {
	case 0x35010100:
#if 0 /* Customer request */
		kalSprintf(log, "[DHCP] DISCOVER");
		kalReportWifiLog(prAdapter,
			ucBssIndex, log);
#endif
		break;
	case 0x35010200:
		if (g_u4LatestDHCPTid == u4Xid) {
			if (!fgIsMldAp)
				kalSprintf(log, "[DHCP] OFFER rx");
			else
				kalSprintf(log, "[DHCP] OFFER band=%s rx",
					apucExtBandStr[prBssInfo->eBand]);
			kalReportWifiLog(prAdapter,
				ucBssIndex, log);
		}
		break;
	case 0x35010300:
#if 0 /* Customer request */
		kalSprintf(log, "[DHCP] REQUEST");
		kalReportWifiLog(prAdapter,
			ucBssIndex, log);
#endif
		break;
	case 0x35010500:
		if (g_u4LatestDHCPTid == u4Xid) {
			if (!fgIsMldAp)
				kalSprintf(log, "[DHCP] ACK rx");
			else
				kalSprintf(log, "[DHCP] ACK band=%s rx",
					apucExtBandStr[prBssInfo->eBand]);
			kalReportWifiLog(prAdapter,
				ucBssIndex, log);
		}
		break;
	case 0x35010600:
		if (g_u4LatestDHCPTid == u4Xid) {
			if (!fgIsMldAp)
				kalSprintf(log, "[DHCP] NAK rx");
			else
				kalSprintf(log, "[DHCP] NAK band=%s rx",
					apucExtBandStr[prBssInfo->eBand]);
			kalReportWifiLog(prAdapter,
				ucBssIndex, log);
		}
		break;
	}
}

void connLogDhcpTx(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint32_t u4Xid,
	uint32_t u4Opt,
	uint8_t ucSn)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	/* client */
	switch (u4Opt & 0xffffff00) {
	case 0x35010100:
		if (!fgIsMldAp)
			kalSprintf(log, "[DHCP] DISCOVER");
		else
			kalSprintf(log, "[DHCP] DISCOVER band=%s",
				apucExtBandStr[prBssInfo->eBand]);
		kalBufferWifiLog(prAdapter,
			ucBssIndex, log, ucSn);
		g_u4LatestDHCPTid = u4Xid;
		break;
	case 0x35010200:
		if (!fgIsMldAp)
			kalSprintf(log, "[DHCP] OFFER");
		else
			kalSprintf(log, "[DHCP] OFFER band=%s",
				apucExtBandStr[prBssInfo->eBand]);
		kalBufferWifiLog(prAdapter,
			ucBssIndex, log, ucSn);
		break;
	case 0x35010300:
		if (!fgIsMldAp)
			kalSprintf(log, "[DHCP] REQUEST");
		else
			kalSprintf(log, "[DHCP] REQUEST band=%s",
				apucExtBandStr[prBssInfo->eBand]);
		kalBufferWifiLog(prAdapter,
			ucBssIndex, log, ucSn);
		g_u4LatestDHCPTid = u4Xid;
		break;
	case 0x35010500:
		if (!fgIsMldAp)
			kalSprintf(log, "[DHCP] ACK");
		else
			kalSprintf(log, "[DHCP] ACK band=%s",
				apucExtBandStr[prBssInfo->eBand]);
		kalBufferWifiLog(prAdapter,
			ucBssIndex, log, ucSn);
		break;
	case 0x35010600:
		if (!fgIsMldAp)
			kalSprintf(log, "[DHCP] NAK");
		else
			kalSprintf(log, "[DHCP] NAK band=%s",
				apucExtBandStr[prBssInfo->eBand]);
		kalBufferWifiLog(prAdapter,
			ucBssIndex, log, ucSn);
		break;
	}
}

void connLogEapTx(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint16_t u2EapLen,
	uint8_t ucEapType,
	uint8_t ucEapCode,
	uint8_t ucSn)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	/* 1:REQ, 2:RESP, 3:SUCCESS, 4:FAIL, 5:INIT, 6:Finish */
	uint8_t *apucEapCode[ENUM_EAP_CODE_NUM] = {
		(uint8_t *) DISP_STRING("UNKNOWN"),
		(uint8_t *) DISP_STRING("REQ"),
		(uint8_t *) DISP_STRING("RESP"),
		(uint8_t *) DISP_STRING("SUCC"),
		(uint8_t *) DISP_STRING("FAIL")
	};

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (ucEapCode == 1 || ucEapCode == 2) {
		if (kalStrnCmp(
			eap_type_text(ucEapType),
			"Unknown", 7) != 0) {
			if (!fgIsMldAp)
				kalSprintf(log,
					"[EAP] %s type=%s len=%d",
				apucEapCode[ucEapCode],
				eap_type_text(ucEapType),
				u2EapLen);
			else
				kalSprintf(log,
					"[EAP] %s band=%s type=%s len=%d",
				apucEapCode[ucEapCode],
				apucExtBandStr[prBssInfo->eBand],
				eap_type_text(ucEapType),
				u2EapLen);
			kalBufferWifiLog(prAdapter,
				ucBssIndex, log,
				ucSn);
		}
	} else if (ucEapCode == 3 ||
				ucEapCode == 4) {
			kalSprintf(log,
				"[EAP] %s",
				apucEapCode[ucEapCode]);
			kalBufferWifiLog(prAdapter,
				ucBssIndex, log,
				ucSn);
	} else {
		//EapCode = 5 or 6, no need to log
	}
}

void connLogEapRx(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	uint16_t u2EapLen,
	uint8_t ucEapType,
	uint8_t ucEapCode)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;
#endif

	/* 1:REQ, 2:RESP, 3:SUCCESS, 4:FAIL, 5:INIT, 6:Finish */
	uint8_t *apucEapCode[ENUM_EAP_CODE_NUM] = {
		(uint8_t *) DISP_STRING("UNKNOWN"),
		(uint8_t *) DISP_STRING("REQ"),
		(uint8_t *) DISP_STRING("RESP"),
		(uint8_t *) DISP_STRING("SUCC"),
		(uint8_t *) DISP_STRING("FAIL")
	};

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(WNM, ERROR, "Invalid BSS_INFO\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE == 1)
	prBssDesc = scanSearchBssDescByBssid(prAdapter, prBssInfo->aucBSSID);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (ucEapCode == 1 || ucEapCode == 2) {
		if (kalStrnCmp(
			eap_type_text(ucEapType),
			"Unknown", 7) != 0) {
			if (!fgIsMldAp)
				kalSprintf(log,
					"[EAP] %s type=%s len=%d rx",
				apucEapCode[ucEapCode],
				eap_type_text(ucEapType),
				u2EapLen);
			else
				kalSprintf(log,
					"[EAP] %s band=%s type=%s len=%d rx",
				apucEapCode[ucEapCode],
				apucExtBandStr[prBssInfo->eBand],
				eap_type_text(ucEapType),
				u2EapLen);
			kalReportWifiLog(prAdapter,
				ucBssIndex, log);
		}
	} else if (ucEapCode == 3 ||
			ucEapCode == 4) {
		kalSprintf(log,
			"[EAP] %s",
			apucEapCode[ucEapCode]);
		kalReportWifiLog(prAdapter,
			ucBssIndex, log);
	} else {
		//EapCode = 5 or 6, no need to log
	}
}

void connLogPkt(
	struct ADAPTER *prAdapter,
	struct MSDU_TOKEN_ENTRY *prTokenEntry, uint32_t u4Stat)
{
	uint8_t i;

	/* report EAPOL & DHCP packet only */
	if (prTokenEntry->ucPktType == ENUM_PKT_1X ||
		prTokenEntry->ucPktType == ENUM_PKT_DHCP) {
		char log[256] = {0};
		char buf[64] = {0};
		struct BUFFERED_LOG_ENTRY *prLogEntry;

		for (i = 0; i < MAX_BUF_NUM; i++) {
			/* Check if any buffered log */
			prLogEntry = &gBufferedLog[i][prTokenEntry->ucBssIndex];

			if (prLogEntry->fgBuffered && prLogEntry->ucSn ==
				prTokenEntry->ucTxSeqNum) {
				kalSprintf(buf, "%s", prLogEntry->aucLog);
				kalSprintf(log,
				"%s tx_status=%s [sn=%d]", buf,
					msdu_tx_status_text(u4Stat),
					prTokenEntry->ucTxSeqNum);

				kalReportWifiLog(prAdapter,
					prTokenEntry->ucBssIndex, log);
				prLogEntry->fgBuffered = FALSE;
			}
		}
	}
}

static uint8_t g_IsStaInfoPrinted = FALSE;
void connLogStaInfo(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct BSS_DESC_SET *set)
{
	char log[256] = {0};
	uint8_t *prStaMacAddr;
	struct BSS_INFO *prBssInfo = NULL;
#if (CFG_SUPPORT_802_11BE_MLO == 1)
	uint8_t fgIsMldAp = FALSE;
	struct MLD_BSS_INFO *prMldBssInfo = NULL;
	struct BSS_DESC *prBssDesc = NULL;
#endif

	if (g_IsStaInfoPrinted == TRUE) {
		return;
	}

	prBssInfo = aisGetAisBssInfo(prAdapter, ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(INIT, ERROR, "prBssInfo is NULL\n");
		return;
	}

#if (CFG_SUPPORT_802_11BE_MLO == 1)
	prBssDesc = set->prMainBssDesc;
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;

	if(fgIsMldAp) {
		prMldBssInfo = mldBssGetByBss(prAdapter, prBssInfo);
		if (prMldBssInfo) {
			uint8_t aucMacAddr[MAC_ADDR_LEN];

			kalSprintf(log, "[CONN] STA_INFO mld_mac=" RPTMACSTR,
				RPTMAC2STR(prMldBssInfo->aucOwnMldAddr));
			if ((prBssDesc->eBand == BAND_5G) || (prBssDesc->eBand == BAND_6G)) {
				nicApplyLinkAddress(prAdapter, prBssInfo->aucOwnMacAddr, aucMacAddr, 1);
				kalSprintf(log + strlen(log), " 2G=" RPTMACSTR,
					aucMacAddr);
				kalSprintf(log + strlen(log), " 5G=" RPTMACSTR,
					prBssInfo->aucOwnMacAddr);
				kalSprintf(log + strlen(log), " 6G=" RPTMACSTR,
					prBssInfo->aucOwnMacAddr);
			}
			else {
				nicApplyLinkAddress(prAdapter, prBssInfo->aucOwnMacAddr, aucMacAddr, 1);
				kalSprintf(log + strlen(log), " 2G=" RPTMACSTR,
					prBssInfo->aucOwnMacAddr);
				kalSprintf(log + strlen(log), " 5G=" RPTMACSTR,
					aucMacAddr);
				kalSprintf(log + strlen(log), " 6G=" RPTMACSTR,
					aucMacAddr);
			}
		}
	} else
#endif
	{
		prStaMacAddr = prBssInfo->aucOwnMacAddr;
		kalSprintf(log, "[CONN] STA_INFO mac=" RPTMACSTR,
			RPTMAC2STR(prStaMacAddr));
	}

	kalReportWifiLog(prAdapter, ucBssIndex, log);
	g_IsStaInfoPrinted = TRUE;
	return;
}

void gResetStaInfoPrinted(void)
{
	g_IsStaInfoPrinted = FALSE;
}

void connLogConnect(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	struct cfg80211_connect_params *sme)
{
	char log[256] = {0};
	uint8_t aucSSID[ELEM_MAX_LEN_SSID+1];
	struct RSN_INFO rRsnInfo;
#if CFG_SUPPORT_802_11W
	uint8_t ucMfpCap;
#endif

	kalMemZero(aucSSID, ELEM_MAX_LEN_SSID+1);
	COPY_SSID(aucSSID, sme->ssid_len,
		sme->ssid, sme->ssid_len);

	kalSprintf(log, "[CONN] CONNECTING ssid=\"%s\"", aucSSID);
	if (sme->bssid)
		kalSprintf(log + strlen(log), " bssid=" RPTMACSTR,
			RPTMAC2STR(sme->bssid));
	if (sme->bssid_hint)
		kalSprintf(log + strlen(log), " bssid_hint=" RPTMACSTR,
			RPTMAC2STR(sme->bssid_hint));
	if (sme->channel && sme->channel->center_freq)
		kalSprintf(log + strlen(log), " freq=%d",
			sme->channel->center_freq);
	if (sme->channel_hint && sme->channel_hint->center_freq)
		kalSprintf(log + strlen(log), " freq_hint=%d",
			sme->channel_hint->center_freq);

	kalSprintf(log + strlen(log),
		" pairwise=0x%x group=0x%x akm=0x%x auth_type=%x",
		sme->crypto.ciphers_pairwise[0],
		sme->crypto.cipher_group,
		sme->crypto.akm_suites[0],
		sme->auth_type);

	if (sme->ie && sme->ie_len > 0) {
		uint8_t *prDesiredIE = NULL;
		uint8_t *pucIEStart = (uint8_t *)sme->ie;
		if (wextSrchDesiredWPAIE(pucIEStart, sme->ie_len, ELEM_ID_RSN,
					 (uint8_t **) &prDesiredIE)) {
			if (rsnParseRsnIE(prAdapter, prDesiredIE, &rRsnInfo)) {
#if CFG_SUPPORT_802_11W
				if (rRsnInfo.u2RsnCap & ELEM_WPA_CAP_MFPC) {
					ucMfpCap = RSN_AUTH_MFP_OPTIONAL;
					if (rRsnInfo.u2RsnCap &
					    ELEM_WPA_CAP_MFPR)
						ucMfpCap =
							RSN_AUTH_MFP_REQUIRED;
				} else
					ucMfpCap = RSN_AUTH_MFP_DISABLED;
#endif
			}
		}
	}

#if CFG_SUPPORT_802_11W
	switch (sme->mfp) {
	case NL80211_MFP_NO:
		if (ucMfpCap == RSN_AUTH_MFP_OPTIONAL)
			kalSprintf(log + strlen(log),
				" group_mgmt=0x%x",
				be2cpu32(rRsnInfo.u4GroupMgmtCipherSuite));
		break;
	case NL80211_MFP_REQUIRED:
		kalSprintf(log + strlen(log),
			" group_mgmt=0x%x",
			be2cpu32(rRsnInfo.u4GroupMgmtCipherSuite));
		break;
	default:
		break;
	}
#endif
	kalSprintf(log + strlen(log),
		" btcoex=%d",
		(aisGetAisBssInfo(prAdapter, ucBssIndex)->eCoexMode == COEX_TDD_MODE));
	kalReportWifiLog(prAdapter, ucBssIndex, log);
}

void connLogConnectFail(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex,
	enum ENUM_CONN_FAIL_REASON reason)
{
	char log[256] = {0};
	struct CONNECTION_SETTINGS *prConnSettings = NULL;

	prConnSettings = aisGetConnSettings(prAdapter, ucBssIndex);
	if (!prConnSettings) {
		DBGLOG(INIT, ERROR, "prConnSettings is NULL");
		return;
	}

	kalSprintf(log, "[CONN] CONNECTING FAIL bssid=" RPTMACSTR,
		RPTMAC2STR(prConnSettings->aucBSSIDHint));
	kalSprintf(log + strlen(log),
		" freq=%d", prConnSettings->u4FreqInMHz);
	kalSprintf(log + strlen(log),
		" [reason=%d %s]", reason, connect_fail_reason(reason));
	kalReportWifiLog(prAdapter, ucBssIndex, log);
}

void connLogDisconnect(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct BSS_DESC *prTargetBssDesc = NULL;
	struct BSS_INFO *prAisBssInfo = NULL;
	struct AIS_FSM_INFO *prAisFsmInfo = NULL;

	prAisBssInfo = aisGetAisBssInfo(prAdapter, ucBssIndex);
	prAisFsmInfo = aisGetAisFsmInfo(prAdapter, ucBssIndex);

	prTargetBssDesc = aisGetTargetBssDesc(prAdapter,
		prAisBssInfo->ucBssIndex);
	if (prTargetBssDesc && prAisBssInfo->eConnectionState
		== MEDIA_STATE_DISCONNECTED &&
		prAisFsmInfo->ucReasonOfDisconnect ==
		DISCONNECT_REASON_CODE_RADIO_LOST) {
		char log[256] = {0};

		kalSprintf(log,
			"[CONN] DISCONN bssid=" RPTMACSTR
			" rssi=%d reason=%d",
			RPTMAC2STR(prTargetBssDesc->aucBSSID),
			RCPI_TO_dBm(prTargetBssDesc->ucRCPI), 0);
		kalReportWifiLog(prAdapter,
			prAisBssInfo->ucBssIndex, log);
	}
}

void connLogMgmtTx(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	struct AIS_FSM_INFO *prAisFsmInfo;
	uint8_t ucBssIndex = 0;
	char log[256] = {0};
	struct STA_RECORD *prStaRec;
	struct WLAN_AUTH_FRAME *prAuthFrame;
	uint16_t u2RxTransactionSeqNum;
	uint16_t u2TxAuthAlgNum;
	struct BSS_DESC *prTargetBssDesc;
	uint16_t u2AuthStatusCode;

	prStaRec =
		cnmGetStaRecByIndex(prAdapter,
			prMsduInfo->ucStaRecIndex);
	prTargetBssDesc =
		aisGetTargetBssDesc(prAdapter,
			ucBssIndex);
	prAisFsmInfo =
		aisGetAisFsmInfo(prAdapter,
			ucBssIndex);

	if (ucBssIndex == NETWORK_TYPE_AIS
		&& prTargetBssDesc
		&& prStaRec) {

		prAuthFrame = (struct WLAN_AUTH_FRAME *)
			(prMsduInfo->prPacket);
		u2TxAuthAlgNum = prAuthFrame->u2AuthAlgNum;

		if (u2TxAuthAlgNum == AUTH_ALGORITHM_NUM_SAE) {
			u2RxTransactionSeqNum =
				prAuthFrame->u2AuthTransSeqNo;
			u2AuthStatusCode =
				prAuthFrame->u2StatusCode;
			kalSprintf(log,
			"[CONN] AUTH REQ bssid=" RPTMACSTR
			" rssi=%d auth_algo=%d type=%d sn=%d status=%d tx_status=%s",
			RPTMAC2STR(prStaRec->aucMacAddr),
			RCPI_TO_dBm(
			prTargetBssDesc->ucRCPI),
			prStaRec->ucAuthAlgNum,
			u2RxTransactionSeqNum,
			prMsduInfo->ucTxSeqNum,
			u2AuthStatusCode,
			tx_status_text(rTxDoneStatus));
			kalReportWifiLog(prAdapter,
				prMsduInfo->ucBssIndex, log);
		}
	}
}

void connLogAssocResp(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct WLAN_ASSOC_RSP_FRAME *prAssocRspFrame,
	uint16_t u2RxStatusCode)
{
	uint8_t fgIsMldAp = FALSE;
#if (CFG_SUPPORT_802_11BE == 1)
	struct BSS_DESC *prBssDesc = NULL;

	prBssDesc = scanSearchBssDescByBssid(prAdapter,
		prStaRec->aucMacAddr);
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (IS_STA_IN_AIS(prAdapter, prStaRec) &&
		prStaRec->eAuthAssocState == SAA_STATE_WAIT_ASSOC2) {
		char log[256] = {0};
		uint16_t u2RxAssocId;

		u2RxAssocId = prAssocRspFrame->u2AssocId;
		if ((u2RxAssocId & BIT(6)) && (u2RxAssocId & BIT(7))
			&& !(u2RxAssocId & BITS(8, 15))) {
			u2RxAssocId = u2RxAssocId & ~BITS(6, 7);
		} else {
			u2RxAssocId = u2RxAssocId & ~AID_MSB;
		}
		if (prStaRec->fgIsReAssoc)
			kalSprintf(log, "[CONN] REASSOC");
		else
			kalSprintf(log, "[CONN] ASSOC");

		if (!fgIsMldAp) {
			kalSprintf(log + strlen(log),
				" RESP bssid=" RPTMACSTR
				" sn=%d status=%d assoc_id=%d",
				RPTMAC2STR(prStaRec->aucMacAddr),
				(int)WLAN_GET_SEQ_SEQ(prAssocRspFrame->u2SeqCtrl),
				u2RxStatusCode, u2RxAssocId);
			kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
		}
#if (CFG_SUPPORT_802_11BE_MLO == 1)
		else {
			kalSprintf(log + strlen(log),
				" RESP bssid=" RPTMACSTR
				" sn=%d status=%d assoc_id=%d mld_mac=" RPTMACSTR,
				RPTMAC2STR(prStaRec->aucMacAddr),
				(int)WLAN_GET_SEQ_SEQ(prAssocRspFrame->u2SeqCtrl),
				u2RxStatusCode, u2RxAssocId,
				RPTMAC2STR(prStaRec->aucMldAddr));
			kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
		}
#endif
	}
}

void connLogAuthReq(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	struct STA_RECORD *prStaRec;
	char log[256] = {0};
	struct BSS_DESC *prBssDesc = NULL;

	prStaRec = cnmGetStaRecByIndex(prAdapter,
		prMsduInfo->ucStaRecIndex);

	if (!prStaRec)
		return;

	prBssDesc = scanSearchBssDescByBssid(prAdapter,
		prStaRec->aucMacAddr);

	if (IS_STA_IN_AIS(prAdapter, prStaRec) && prBssDesc) {
		struct WLAN_AUTH_FRAME *prAuthFrame;
		uint16_t u2AuthStatusCode;

		prAuthFrame = (struct WLAN_AUTH_FRAME *)
			prMsduInfo->prPacket;
		u2AuthStatusCode = prAuthFrame->u2StatusCode;
		kalSprintf(log,
			"[CONN] AUTH REQ bssid=" RPTMACSTR
			" rssi=%d auth_algo=%d sn=%d status=%d tx_status=%s",
			RPTMAC2STR(prStaRec->aucMacAddr),
			RCPI_TO_dBm(prBssDesc->ucRCPI),
			prStaRec->ucAuthAlgNum,
			prMsduInfo->u2HwSeqNum,
			u2AuthStatusCode,
			tx_status_text(rTxDoneStatus));
		kalReportWifiLog(prAdapter,
			prMsduInfo->ucBssIndex, log);
	}
}

void connLogAssocReq(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	struct STA_RECORD *prStaRec;
	char log[256] = {0};
	char buf[64] = {0};
	struct BSS_DESC *prBssDesc = NULL;
	uint8_t fgIsMldAp = FALSE;
	struct BSS_DESC_SET *set;
	enum ENUM_BAND eBand;
	uint8_t i;

	prStaRec = cnmGetStaRecByIndex(prAdapter,
		prMsduInfo->ucStaRecIndex);

	if (!prStaRec)
		return;

	prBssDesc = scanSearchBssDescByBssid(prAdapter,
		prStaRec->aucMacAddr);
#if (CFG_SUPPORT_802_11BE == 1)
	if (prBssDesc)
		fgIsMldAp = prBssDesc->fgIsEHTPresent;
#endif
	if (IS_STA_IN_AIS(prAdapter, prStaRec) && prBssDesc) {
		if (prStaRec->fgIsReAssoc)
			kalSprintf(log, "[CONN] REASSOC");
		else
			kalSprintf(log, "[CONN] ASSOC");

		if (!fgIsMldAp)
			kalSprintf(log + strlen(log),
			" REQ bssid=" RPTMACSTR
			" rssi=%d sn=%d tx_status=%s",
				RPTMAC2STR(prStaRec->aucMacAddr),
				RCPI_TO_dBm(prBssDesc->ucRCPI),
				prMsduInfo->u2HwSeqNum,
				tx_status_text(rTxDoneStatus));
		else {
			set = aisGetSearchResult(prAdapter, prMsduInfo->ucBssIndex);
			if (set) {
				for (i = MLD_LINK_MAX; i > 0; i--) {
					if (set->aprBssDesc[i-1]) {
						eBand = set->aprBssDesc[i-1]->eBand;
						kalSprintf(buf + strlen(buf), "%s+",
							apucExtBandStr[eBand]);
					}
				}
				if (strlen(buf) > 1) buf[strlen(buf) - 1] = '\0';
			}
			kalSprintf(log + strlen(log),
			" REQ bssid=" RPTMACSTR
			" rssi=%d sn=%d band=%s tx_status=%s",
				RPTMAC2STR(prStaRec->aucMacAddr),
				RCPI_TO_dBm(prBssDesc->ucRCPI),
				prMsduInfo->u2HwSeqNum,
				buf,
				tx_status_text(rTxDoneStatus));
		}

		kalReportWifiLog(prAdapter,
			prMsduInfo->ucBssIndex, log);
	}
}

void connLogAuthResp(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct WLAN_AUTH_FRAME *prAuthFrame,
	uint16_t u2RxStatusCode)
{
	if (IS_STA_IN_AIS(prAdapter, prStaRec)) {
		char log[256] = {0};

		if (prStaRec->eAuthAssocState == SAA_STATE_WAIT_AUTH2
		|| prStaRec->eAuthAssocState == SAA_STATE_WAIT_AUTH4
		|| prStaRec->eAuthAssocState == SAA_STATE_SEND_AUTH1
		|| prStaRec->eAuthAssocState == SAA_STATE_SEND_AUTH3) {
			kalSprintf(log,
				"[CONN] AUTH RESP bssid=" RPTMACSTR
				" auth_algo=%d sn=%d status=%d",
			RPTMAC2STR(prStaRec->aucMacAddr),
			prStaRec->ucAuthAlgNum,
			(int)WLAN_GET_SEQ_SEQ(prAuthFrame->u2SeqCtrl),
			u2RxStatusCode);
			kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
		} else if (prStaRec->eAuthAssocState ==
			SAA_STATE_EXTERNAL_AUTH) {
			kalSprintf(log,
				"[CONN] AUTH RESP bssid=" RPTMACSTR
				" auth_algo=%d type=%d sn=%d status=%d",
			RPTMAC2STR(prStaRec->aucMacAddr),
			prStaRec->ucAuthAlgNum,
			prAuthFrame->u2AuthTransSeqNo,
			(int)WLAN_GET_SEQ_SEQ(prAuthFrame->u2SeqCtrl),
			u2RxStatusCode);
			kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
		}
	}
}

void connLogDeauth(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	uint8_t ucTxSeqNum,
	uint16_t u2ReasonCode)
{
	struct BSS_DESC *prBssDesc = NULL;
	char log[256] = {0};

	if (prStaRec && IS_STA_IN_AIS(prAdapter, prStaRec)) {
		prBssDesc = scanSearchBssDescByBssid(prAdapter,
			prStaRec->aucMacAddr);

		if (prBssDesc) {
			kalSprintf(log,
				"[CONN] DEAUTH TX bssid=" RPTMACSTR
				" rssi=%d sn=%d reason=%d",
			RPTMAC2STR(prStaRec->aucMacAddr),
			RCPI_TO_dBm(prBssDesc->ucRCPI),
			ucTxSeqNum,
			u2ReasonCode);

			kalReportWifiLog(prAdapter,
				prStaRec->ucBssIndex, log);
		}
	}
}

void connLogRxDeauth(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct WLAN_DEAUTH_FRAME *prDeauthFrame,
	struct BSS_DESC *prBssDesc)
{
	char log[256] = {0};

	if (prBssDesc) {
		kalSprintf(log,
		"[CONN] DEAUTH RX bssid=" RPTMACSTR
		" rssi=%d sn=%d reason=%d",
		RPTMAC2STR(prDeauthFrame->aucSrcAddr),
		RCPI_TO_dBm(prBssDesc->ucRCPI),
		(int)WLAN_GET_SEQ_SEQ(
			prDeauthFrame->u2SeqCtrl),
		prDeauthFrame->u2ReasonCode);
		kalReportWifiLog(prAdapter,
			prStaRec->ucBssIndex, log);
	}

}

void connLogRxDeauthBuffer(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct WLAN_DEAUTH_FRAME *prDeauthFrame,
	struct BSS_DESC *prBssDesc)
{
	char log[256] = {0};

	if (prBssDesc) {
		kalSprintf(log,
		"[CONN] DEAUTH RX bssid=" RPTMACSTR
		" rssi=%d sn=%d reason=%d",
		RPTMAC2STR(prDeauthFrame->aucSrcAddr),
		RCPI_TO_dBm(prBssDesc->ucRCPI),
		(int)WLAN_GET_SEQ_SEQ(
			prDeauthFrame->u2SeqCtrl),
		prDeauthFrame->u2ReasonCode);
		kalRxDeauthBufferWifiLog(prAdapter,
			prStaRec->ucBssIndex, log);
	}

}

void connLogRxDeauthPrint(
	struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	uint8_t i;
	struct BUFFERED_LOG_ENTRY *prLogEntry;

	for (i = 0; i < MAX_AUTH_BUF_NUM; i++) {
		/* Check if any buffered log */
		prLogEntry = &gAuthBufferedLog[i][ucBssIndex];
		if (prLogEntry->fgBuffered) {
			kalReportWifiLog(prAdapter,
				ucBssIndex, prLogEntry->aucLog);
			prLogEntry->fgBuffered = FALSE;
		}
	}
}

void connLogRxDeassoc(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct WLAN_DISASSOC_FRAME *prDisassocFrame,
	struct BSS_DESC *prBssDesc)
{
	char log[256] = {0};

	if (prBssDesc) {
		kalSprintf(log,
		"[CONN] DISASSOC RX bssid=" MACSTR
		" rssi=%d sn=%d reason=%d",
		RPTMAC2STR(prDisassocFrame->aucSrcAddr),
		RCPI_TO_dBm(prBssDesc->ucRCPI),
		(int)WLAN_GET_SEQ_SEQ(
			prDisassocFrame->u2SeqCtrl),
		prDisassocFrame->u2ReasonCode);

		kalReportWifiLog(prAdapter,
			prStaRec->ucBssIndex, log);
	}
}

#endif

#if (CFG_SUPPORT_MLD_LOG == 1) && (CFG_SUPPORT_802_11BE_MLO == 1)
static char *mlo_link_reason(uint8_t reason)
{
	switch (reason) {
	case MLO_LINK_STATE_CHANGE_REASON_DEFAULT: return "default";
	case MLO_LINK_STATE_CHANGE_REASON_TEST: return "test";
	case MLO_LINK_STATE_CHANGE_REASON_T2LM: return "T2LM";
	case MLO_LINK_STATE_CHANGE_REASON_EMLSR: return "EMLSR";
	case MLO_LINK_STATE_CHANGE_REASON_LINK_RECOMMEND: return "link recommed";
	case MLO_LINK_STATE_CHANGE_REASON_RCPI: return "rssi";
	case MLO_LINK_STATE_CHANGE_REASON_COEX: return "coex";
	case MLO_LINK_STATE_CHANGE_REASON_CONCURRENT: return "concurrent";
	case MLO_LINK_STATE_CHANGE_REASON_TPUT_HIGH: return "tput high";
	case MLO_LINK_STATE_CHANGE_REASON_TPUT_LOW: return "tput low";
	default: return "unknown";
	}
}

static char *mld_tx_status_text(uint8_t rTxDoneStatus)
{
	switch (rTxDoneStatus) {
	case TX_RESULT_SUCCESS: return "ACK";
	case TX_RESULT_MPDU_ERROR: return "NO_ACK";
	default: return "TX_FAIL";
	}
}

void mldLogSetup(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	uint8_t ucStatus)
{
	char log[256] = {0};
	struct BSS_INFO *prBssInfo;
	struct BSS_DESC *prBssDesc;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, prStaRec->ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(INIT, ERROR, "prBssInfo is NULL\n");
		return;
	}

	prBssDesc = scanSearchBssDescByBssid(prAdapter,
		prStaRec->aucMacAddr);
	if(!prBssDesc) {
		DBGLOG(ML, INFO, "prBssDesc is NULL");
		return;
	}

	kalSprintf(log, "[MLD] SETUP band=%s freq=%d status=%d bssid="
		RPTMACSTR " link_id=%d",
		apucExtBandStr[prBssInfo->eBand],
		KHZ_TO_MHZ(nicChannelNum2Freq(prBssDesc->ucChannelNum,
			prBssDesc->eBand)),
		ucStatus,
		RPTMAC2STR(prBssDesc->aucBSSID),
		prBssInfo->ucLinkId);

	kalReportWifiLog(prAdapter, prBssInfo->ucBssIndex, log);
}

void mldLogT2LMStatus(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec)
{
	char log[256] = {0};
	char dl_tidlog[32] = {0};
	char ul_tidlog[32] = {0};
	struct BSS_INFO *prBssInfo;
	uint8_t ucBitmap;
	uint8_t i;

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, prStaRec->ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(INIT, ERROR, "prBssInfo is NULL\n");
		return;
	}

	kalSprintf(log, "[MLD] T2LM STATUS band=%s",
		apucExtBandStr[prBssInfo->eBand]);

	if (prStaRec->ucDLTidBitmap == 0)
		kalSprintf(log + strlen(log), " tid_dl=NONE");
	else if (prStaRec->ucDLTidBitmap == 0xff)
		kalSprintf(log + strlen(log), " tid_dl=ALL");
	else {
		for (i = 0; i < MAX_NUM_T2LM_TIDS; i++) {
			ucBitmap = (prStaRec->ucDLTidBitmap & BIT(i));
			if (ucBitmap) kalSprintf(dl_tidlog + strlen(dl_tidlog), "%d,", i);
		}
		if (strlen(dl_tidlog) > 1) dl_tidlog[strlen(dl_tidlog) - 1] = '\0';
		kalSprintf(log + strlen(log), " tid_dl=%s", dl_tidlog);
	}

	if (prStaRec->ucULTidBitmap == 0)
		kalSprintf(log + strlen(log), " tid_ul=NONE");
	else if (prStaRec->ucULTidBitmap == 0xff)
		kalSprintf(log + strlen(log), " tid_ul=ALL");
	else {
		for (i = 0; i < MAX_NUM_T2LM_TIDS; i++) {
			ucBitmap = (prStaRec->ucULTidBitmap & BIT(i));
			if (ucBitmap) kalSprintf(ul_tidlog + strlen(ul_tidlog), "%d,", i);
		}
		if (strlen(ul_tidlog) > 1) ul_tidlog[strlen(ul_tidlog) - 1] = '\0';
		kalSprintf(log + strlen(log), " tid_ul=%s", ul_tidlog);
	}

	kalReportWifiLog(prAdapter, prBssInfo->ucBssIndex, log);
}

void mldLogT2LMReq(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct STA_RECORD *prStaRec,
	uint8_t ucToken,
	uint8_t ucCnt,
	uint8_t rTxDoneStatus)
{
	char log[256] = {0};

	kalSprintf(log, "[MLD] T2LM REQ band=%s token=%d cnt=%d tx_status=%s",
		apucExtBandStr[prBssInfo->eBand], ucToken, ucCnt,
		mld_tx_status_text(rTxDoneStatus));
	kalReportWifiLog(prAdapter, prBssInfo->ucBssIndex, log);
}

void mldLogT2LMResp(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct STA_RECORD *prStaRec,
	uint8_t ucToken,
	uint8_t ucStatus)
{
	char log[256] = {0};

	kalSprintf(log, "[MLD] T2LM RESP band=%s token=%d tx_status=%d",
		apucExtBandStr[prBssInfo->eBand], ucToken, ucStatus);
	kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
}

void mldLogT2LMTeardown(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo,
	struct STA_RECORD *prStaRec,
	uint8_t rTxDoneStatus)
{
	char log[256] = {0};

	kalSprintf(log, "[MLD] T2LM TEARDOWN band=%s tx_status=%s",
		apucExtBandStr[prBssInfo->eBand],
		mld_tx_status_text(rTxDoneStatus));
	kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
}

void mldLogLink(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	struct MLD_STA_RECORD *prMldStaRec,
	uint8_t ucLinkState,
	uint8_t reason)
{
	char log[256] = {0};
	char abuf[64] = {0};
	char ibuf[64] = {0};
	struct BSS_INFO *prBssInfo = NULL;
	struct BSS_INFO *prCurrBssInfo = NULL;
	struct STA_RECORD *prCurrStarec;
	struct LINK *prStarecList;
	uint32_t fgIsActive;
	unsigned long long u8ActiveStaBitmap;

	prBssInfo = aisGetAisBssInfo(prAdapter, prStaRec->ucBssIndex);
	if (!prBssInfo) {
		DBGLOG(INIT, ERROR, "prBssInfo is NULL\n");
		return;
	}

	u8ActiveStaBitmap = prMldStaRec->u8ActiveStaBitmap;
	prStarecList = &prMldStaRec->rStarecList;
	LINK_FOR_EACH_ENTRY(prCurrStarec, prStarecList, rLinkEntryMld,
	    struct STA_RECORD) {
		fgIsActive = (u8ActiveStaBitmap & BIT(prCurrStarec->ucIndex));
		prCurrBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter,
			prCurrStarec->ucBssIndex);
		if (prCurrBssInfo) {
			if (fgIsActive)
				kalSprintf(abuf + strlen(abuf), "%s,",
					apucExtBandStr[prCurrBssInfo->eBand]);
			else
				kalSprintf(ibuf + strlen(ibuf), "%s,",
					apucExtBandStr[prCurrBssInfo->eBand]);
		}
	}
	if (strlen(abuf) > 1) abuf[strlen(abuf) - 1] = '\0';
	if (strlen(ibuf) > 1) ibuf[strlen(ibuf) - 1] = '\0';

	kalSprintf(log, "[MLD] LINK");
	if (strlen(abuf) > 0)
		kalSprintf(log + strlen(log), " active=%s", abuf);
	if (strlen(ibuf) > 0)
		kalSprintf(log + strlen(log), " inactive=%s", ibuf);
	kalSprintf(log + strlen(log), " reason=\"%s\"",
		mlo_link_reason(reason));

	kalReportWifiLog(prAdapter, prStaRec->ucBssIndex, log);
}
#endif

