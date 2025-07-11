// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include "precomp.h"

static void wmmTxTspecFrame(struct ADAPTER *prAdapter, uint8_t ucTid,
	enum TSPEC_OP_CODE eOpCode,
	struct PARAM_QOS_TSPEC *prTsParam,
	uint8_t ucBssIndex);
static void wmmSyncAcParamWithFw(struct ADAPTER *prAdapter, uint8_t ucAc,
	uint16_t u2MediumTime, uint32_t u4PhyRate, uint8_t ucBssIndex);

static void wmmGetTsmRptTimeout(struct ADAPTER *prAdapter,
				uintptr_t ulParam);

static void wmmQueryTsmResult(struct ADAPTER *prAdapter, uintptr_t ulParam);
static void wmmRemoveTSM(struct ADAPTER *prAdapter,
			 struct ACTIVE_RM_TSM_REQ *prActiveTsm,
			 u_int8_t fgNeedStop,
			 uint8_t ucBssIndex);
static struct ACTIVE_RM_TSM_REQ *wmmGetActiveTsmReq(struct ADAPTER *prAdapter,
						    uint8_t ucTid,
						    u_int8_t fgTriggered,
						    u_int8_t fgAllocIfNotExist,
						    uint8_t ucBssIndex);
static uint32_t wmmRunEventActionTxDone(struct ADAPTER *prAdapter,
					struct MSDU_INFO *prMsduInfo,
					enum ENUM_TX_RESULT_CODE rTxDoneStatus);
static void wmmMayDoTsReplacement(struct ADAPTER *prAdapter,
	uint8_t ucNewTid, uint8_t ucBssIndex);

#if 0
static void DumpData(PUINT8 prAddr, UINT8 uLen, char *tag);
#endif

#if CFG_SUPPORT_SOFT_ACM
static uint16_t wmmAcmTxTimeCal(uint16_t u2SecExtra, uint16_t u2EthBodyLen,
				uint16_t u2DataRate, uint16_t u2BasicRate,
				uint8_t ucFlags);

static uint16_t wmmAcmTxTimeHtCal(uint16_t u2SecExtra, uint16_t u2EthBodyLen,
				  uint8_t ucMcsId, uint8_t ucFlags);

static void wmmAcmDequeueTimeOut(struct ADAPTER *prAdapter,
				 uintptr_t ulParamPtr);

#define FLAG_S_PREAMBLE BIT(0)
#define FLAG_CTS_SELF BIT(1)
#define FLAG_RTS_CTS BIT(2)
#define FLAG_G_MODE BIT(3)
#define FLAG_SHORT_GI BIT(4)
#define FLAG_40M_BW BIT(5)
#define FLAG_GF_HT BIT(6)
#define FLAG_ONLY_DATA BIT(7)

#define TIME_LONG_PREAMBLE 192
#define TIME_SHORT_PREAMBLE 96
#define TIME_SIFSG 0x10
#define TIME_SIFS 0x0A
#define FRM_LENGTH_BLOCK_ACK 30
#define TIME_SIFSGx2 0x20 /* support Clause 18 STA exists */
#define TIME_SIFSx2 0x14
#define FRM_LENGTH_RTS 0x14
#define FRM_LENGTH_ACK 0x0E
/* aggregation related */
#define FRM_LENGTH_AGG_AMSDU_HDR 17
#define FRM_LENGTH_AGG_RAILNK_HDR 14

static inline uint8_t LMR_PREAMBL_TIME(uint8_t __fgIsGmode,
	uint8_t __fgIsSpreamble)
{
	uint8_t ucTime = 0;

	if (__fgIsGmode)
		ucTime = 20;
	else
		ucTime = __fgIsSpreamble ? TIME_SHORT_PREAMBLE
		: TIME_LONG_PREAMBLE;

	return ucTime;
}
#endif

uint8_t const aucUp2ACIMap[8] = {ACI_BE, ACI_BK, ACI_BK, ACI_BE,
				 ACI_VI, ACI_VI, ACI_VO, ACI_VO};

void wmmInit(struct ADAPTER *prAdapter, uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo = aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prTspecInfo = &prWmmInfo->arTsInfo[0];
	uint8_t ucTid = 0;

	for (ucTid = 0; ucTid < WMM_TSPEC_ID_NUM;
		ucTid++, prTspecInfo++) {
		prTspecInfo->ucTid = ucTid;
		cnmTimerInitTimer(prAdapter,
		&prTspecInfo->rAddTsTimer,
		(PFN_MGMT_TIMEOUT_FUNC)
		wmmSetupTspecTimeOut,
		(uintptr_t)prTspecInfo);
	}
#if CFG_SUPPORT_SOFT_ACM
	cnmTimerInitTimer(prAdapter, &prWmmInfo->rAcmDeqTimer,
			wmmAcmDequeueTimeOut, ucBssIndex);
	kalMemZero(&prWmmInfo->arAcmCtrl[0],
			sizeof(prWmmInfo->arAcmCtrl));
#endif
	LINK_INITIALIZE(&prWmmInfo->rActiveTsmReq);
	prWmmInfo->rTriggeredTsmRptTime = 0;

	DBGLOG(WMM, TRACE, "wmm init done\n");
}

void wmmUnInit(struct ADAPTER *prAdapter, uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo = aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prTspecInfo = &prWmmInfo->arTsInfo[0];
	uint8_t ucTid = 0;

	for (ucTid = 0; ucTid < WMM_TSPEC_ID_NUM;
		ucTid++, prTspecInfo++)
		cnmTimerStopTimer(prAdapter,
			&prTspecInfo->rAddTsTimer);
#if CFG_SUPPORT_SOFT_ACM
	cnmTimerStopTimer(prAdapter, &prWmmInfo->rAcmDeqTimer);
#endif
	wmmRemoveAllTsmMeasurement(prAdapter, FALSE, ucBssIndex);

	DBGLOG(WMM, TRACE, "wmm uninit done\n");
}

void wmmFillTsinfo(struct PARAM_QOS_TSINFO *prTsInfo, uint8_t *pucTsInfo)
{
	uint32_t u4TsInfoValue = 0;
	/*	|    0         |1-4  | 5-6 |	7-8          | 9           |
	 *10  | 11-13 | 14-23  |
	 **	Traffic Type|TSID| Dir  |Access Policy|Reserved | PSB|	UP
	 *|reserved|
	 */

	u4TsInfoValue = prTsInfo->ucTrafficType & 0x1;
	u4TsInfoValue |= (prTsInfo->ucTid & 0xf) << 1;
	u4TsInfoValue |= (prTsInfo->ucDirection & 0x3) << 5;
	u4TsInfoValue |= (prTsInfo->ucAccessPolicy & 0x3) << 7;
	u4TsInfoValue |= (prTsInfo->ucApsd & 0x1) << 10;
	u4TsInfoValue |= (prTsInfo->ucuserPriority) << 11;
	u4TsInfoValue |= BIT(7); /* Fixed bit in spec */

	pucTsInfo[0] = u4TsInfoValue & 0xFF;
	pucTsInfo[1] = (u4TsInfoValue >> 8) & 0xff;
	pucTsInfo[2] = (u4TsInfoValue >> 16) & 0xff;
}

void wmmComposeTspecIE(struct ADAPTER *prAdapter,
		       struct MSDU_INFO *prMsduInfo,
		       struct PARAM_QOS_TSPEC *prParamQosTspec)
{
	struct IE_WMM_TSPEC *prIeWmmTspec = NULL;
	uint8_t *pucTemp = NULL;
	uint8_t aucWfaOui[] = VENDOR_OUI_WFA;

	prIeWmmTspec = (struct IE_WMM_TSPEC *)((uint8_t *)prMsduInfo->prPacket +
					       prMsduInfo->u2FrameLength);
	pucTemp = prIeWmmTspec->aucTspecBodyPart;

	/*fill WMM head*/
	prIeWmmTspec->ucId = ELEM_ID_VENDOR;
	prIeWmmTspec->ucLength = ELEM_MAX_LEN_WMM_TSPEC;
	kalMemCopy(prIeWmmTspec->aucOui, aucWfaOui, sizeof(aucWfaOui));
	prIeWmmTspec->ucOuiType = VENDOR_OUI_TYPE_WMM;
	prIeWmmTspec->ucOuiSubtype = VENDOR_OUI_SUBTYPE_WMM_TSPEC;
	prIeWmmTspec->ucVersion = VERSION_WMM;

	/*fill tsinfo*/
	wmmFillTsinfo(&prParamQosTspec->rTsInfo, prIeWmmTspec->aucTsInfo);
	/*1.2 BODY*/
	/*nominal size*/
	/* DumpData(prParamQosTspec, sizeof(struct PARAM_QOS_TSPEC),
	 ** "QosTspc");
	 */
	WLAN_SET_FIELD_16(pucTemp, prParamQosTspec->u2NominalMSDUSize);
	pucTemp += 2;
	WLAN_SET_FIELD_16(pucTemp, prParamQosTspec->u2MaxMSDUsize);
	pucTemp += 2;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MinSvcIntv);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MaxSvcIntv);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4InactIntv);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4SpsIntv);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4SvcStartTime);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MinDataRate);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MeanDataRate);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4PeakDataRate);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MaxBurstSize);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4DelayBound);
	pucTemp += 4;
	WLAN_SET_FIELD_32(pucTemp, prParamQosTspec->u4MinPHYRate);
	pucTemp += 4;
	WLAN_SET_FIELD_16(pucTemp, prParamQosTspec->u2Sba);
	pucTemp += 2;
	WLAN_SET_FIELD_16(pucTemp, prParamQosTspec->u2MediumTime);
	/*DumpData(prIeWmmTspec->aucTsInfo, 55, "tspec ie");*/

	prMsduInfo->u2FrameLength += IE_SIZE(prIeWmmTspec);
}

static uint8_t wmmNewDlgToken(void)
{
	static uint8_t sWmmDlgToken;

	return sWmmDlgToken++;
}

/* follow WMM spec, send add/del tspec request frame */
static void wmmTxTspecFrame(struct ADAPTER *prAdapter, uint8_t ucTid,
	enum TSPEC_OP_CODE eOpCode,
	struct PARAM_QOS_TSPEC *prTsParam,
	uint8_t ucBssIndex)
{
	struct BSS_INFO *prBssInfo =
		aisGetAisBssInfo(prAdapter, ucBssIndex);
	uint16_t u2PayLoadLen = WLAN_MAC_HEADER_LEN + 4; /*exclude TSPEC IE*/
	struct STA_RECORD *prStaRec =
		aisGetTargetStaRec(prAdapter, ucBssIndex);
	struct MSDU_INFO *prMsduInfo = NULL;
	struct WMM_ACTION_TSPEC_FRAME *prActionFrame = NULL;
	uint16_t u2FrameCtrl = MAC_FRAME_ACTION;

	if (!prStaRec || !prTsParam || !prBssInfo) {
		DBGLOG(WMM, ERROR, "prStaRec NULL %d, prTsParam NULL %d\n",
		       !prStaRec, !prTsParam);
		return;
	}
	/*build ADDTS for TID*/
	/*1 compose Action frame Fix field*/
	DBGLOG(WMM, DEBUG, "Tspec Action to AP=" MACSTR "\n",
	       MAC2STR(prStaRec->aucMacAddr));

	prMsduInfo = cnmMgtPktAlloc(prAdapter, ACTION_ADDTS_REQ_FRAME_LEN);
	if (!prMsduInfo) {
		DBGLOG(WMM, ERROR, "Failed to allocate msdu info\n");
		return;
	}
	TX_SET_MMPDU(prAdapter, prMsduInfo, prStaRec->ucBssIndex,
		     prStaRec->ucIndex, WLAN_MAC_MGMT_HEADER_LEN, u2PayLoadLen,
		     wmmRunEventActionTxDone, MSDU_RATE_MODE_AUTO);

	kalMemZero(prMsduInfo->prPacket, ACTION_ADDTS_REQ_FRAME_LEN);

	prActionFrame = (struct WMM_ACTION_TSPEC_FRAME *)prMsduInfo->prPacket;

	/*********frame header**********************/
	WLAN_SET_FIELD_16(&prActionFrame->u2FrameCtrl, u2FrameCtrl);
	COPY_MAC_ADDR(prActionFrame->aucDestAddr, prStaRec->aucMacAddr);
	COPY_MAC_ADDR(prActionFrame->aucSrcAddr, prBssInfo->aucOwnMacAddr);
	COPY_MAC_ADDR(prActionFrame->aucBSSID, prStaRec->aucMacAddr);
	prActionFrame->u2SeqCtrl = 0;

	/********Frame body*************/
	prActionFrame->ucCategory =
		CATEGORY_WME_MGT_NOTIFICATION; /*CATEGORY_QOS_ACTION;*/
	if (eOpCode == TX_ADDTS_REQ) {
		prActionFrame->ucAction = ACTION_ADDTS_REQ;
		prActionFrame->ucDlgToken = (prTsParam->ucDialogToken == 0)
						    ? wmmNewDlgToken()
						    : prTsParam->ucDialogToken;
	} else if (eOpCode == TX_DELTS_REQ) {
		prActionFrame->ucAction = ACTION_DELTS;
		prActionFrame->ucDlgToken =
			0; /* dialog token should be always 0 in delts frame */
	}

	/* this field only meanful in ADD TS response, otherwise set to 0 */
	prActionFrame->ucStatusCode = 0;

	/*DumpData((PUINT_8)prMsduInfo->prPacket,u2PayLoadLen, "ADDTS-FF");*/

	/********Information Element *************/
	wmmComposeTspecIE(prAdapter, prMsduInfo, prTsParam);

	/******** Insert into Msdu Queue *************/
	nicTxEnqueueMsdu(prAdapter, prMsduInfo);
#if 0
	DumpData(((uint8_t *)prMsduInfo->prPacket) + u2PayLoadLen,
		prMsduInfo->u2FrameLength - u2PayLoadLen, "TSPEC-IE");
#endif
}

void wmmSetupTspecTimeOut(struct ADAPTER *prAdapter, uintptr_t ulParam)
{
	struct TSPEC_INFO *prTsInfo = (struct TSPEC_INFO *)ulParam;

	if (!prTsInfo) {
		DBGLOG(WMM, DEBUG, "Wrong TS info\n");
		return;
	}

	switch (prTsInfo->eState) {
	case QOS_TS_ACTIVE:
		DBGLOG(WMM, DEBUG, "Update TS TIMEOUT for TID %d\n",
			prTsInfo->ucTid);
		break;
	case QOS_TS_SETUPING:
		DBGLOG(WMM, DEBUG, "ADD TS TIMEOUT for TID %d\n",
			prTsInfo->ucTid);
		prTsInfo->eState = QOS_TS_INACTIVE;
		break;
	default:
		DBGLOG(WMM, DEBUG,
		       "Shouldn't start this timer when Ts %d in state %d\n",
		       prTsInfo->ucTid, prTsInfo->eState);
		break;
	}
}

uint8_t wmmCalculateUapsdSetting(struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct PM_PROFILE_SETUP_INFO *prPmProf = NULL;
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prCurTs;
	uint8_t ucTid = 0;
	uint8_t ucFinalSetting = 0;
	struct BSS_INFO *prAisBssInfo;

	prAisBssInfo =
		aisGetAisBssInfo(prAdapter,
		ucBssIndex);

	if (!prAisBssInfo || !prWmmInfo) {
		DBGLOG(WMM, DEBUG, "prWmmInfo is null %d\n", ucBssIndex);
		return 0;
	}

	prCurTs = &prWmmInfo->arTsInfo[0];
	prPmProf = &prAisBssInfo->rPmProfSetupInfo;
	ucFinalSetting =
		(prPmProf->ucBmpDeliveryAC << 4) | prPmProf->ucBmpTriggerAC;
	for (ucTid = 0; ucTid < WMM_TSPEC_ID_NUM; ucTid++, prCurTs++) {
		uint8_t ucPsd = 0;

		if (prCurTs->eState != QOS_TS_ACTIVE)
			continue;
		switch (prCurTs->eDir) {
		case UPLINK_TS:
			ucPsd = BIT(prCurTs->eAC);
			break;
		case DOWNLINK_TS:
			ucPsd = BIT(prCurTs->eAC + 4);
			break;
		case BI_DIR_TS:
			ucPsd = BIT(prCurTs->eAC) | BIT(prCurTs->eAC + 4);
			break;
		}
		if (prCurTs->fgUapsd)
			ucFinalSetting |= ucPsd;
		else
			ucFinalSetting &= ~ucPsd;
	}
	return ucFinalSetting;
}

void wmmSyncPsParamWithFw(struct ADAPTER *prAdapter, uint8_t ucAc,
	uint8_t ucBssIndex)
{
	struct CMD_SET_WMM_PS_TEST_STRUCT rSetWmmPsTestParam;
	struct BSS_INFO *prAisBssInfo = NULL;

	prAisBssInfo = aisGetAisBssInfo(prAdapter, ucBssIndex);
	if (prAisBssInfo == NULL)
		return;

	kalMemZero(&rSetWmmPsTestParam, sizeof(rSetWmmPsTestParam));
	rSetWmmPsTestParam.ucBssIndex = prAisBssInfo->ucBssIndex;
	rSetWmmPsTestParam.bmfgApsdEnAc =
		wmmCalculateUapsdSetting(prAdapter, prAisBssInfo->ucBssIndex);
	wlanSendSetQueryCmd(prAdapter, CMD_ID_SET_WMM_PS_TEST_PARMS, TRUE,
			    FALSE, FALSE, NULL, NULL,
			    sizeof(struct CMD_SET_WMM_PS_TEST_STRUCT),
			    (uint8_t *)&rSetWmmPsTestParam, NULL, 0);

	DBGLOG(WMM, DEBUG, "Ac=%d, Uapsd 0x%02x\n",
	       ucAc, rSetWmmPsTestParam.bmfgApsdEnAc);
}

void wmmReSyncPsParamWithFw(struct ADAPTER *prAdapter, uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo = aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prCurTs = NULL;
	uint8_t ucTid = 0;

	for (ucTid = 0; ucTid < WMM_TSPEC_ID_NUM; ucTid++) {
		prCurTs = &prWmmInfo->arTsInfo[ucTid];
		if (prCurTs->eState == QOS_TS_ACTIVE)
			wmmSyncPsParamWithFw(
				prAdapter, prCurTs->eAC, ucBssIndex);
	}
}

void wmmSyncAcParamWithFw(struct ADAPTER *prAdapter, uint8_t ucAc,
	uint16_t u2MediumTime, uint32_t u4PhyRate,
	uint8_t ucBssIndex)
{
#if CFG_SUPPORT_SOFT_ACM
	struct SOFT_ACM_CTRL *prAcmCtrl = NULL;
#endif
	struct CMD_UPDATE_AC_PARAMS rCmdUpdateAcParam;
	struct BSS_INFO *prAisBssInfo;
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);

	prAisBssInfo =
		aisGetAisBssInfo(prAdapter,
		ucBssIndex);
	ASSERT(prAisBssInfo);
#if CFG_SUPPORT_SOFT_ACM
	prAcmCtrl = &prWmmInfo->arAcmCtrl[ucAc];
/* admitted time is in unit 32-us */
#if 0 /* UT/IT code */
	if (u2MediumTime)
		u2MediumTime = 153;
#endif
	prAcmCtrl->u8AdmittedTime = u2MediumTime * 32;
	prAcmCtrl->u8IntervalEndSec = 0;
#endif
	kalMemZero(&rCmdUpdateAcParam, sizeof(rCmdUpdateAcParam));
	rCmdUpdateAcParam.ucAcIndex = ucAc;
	rCmdUpdateAcParam.ucBssIdx = prAisBssInfo->ucBssIndex;
	rCmdUpdateAcParam.u2MediumTime = u2MediumTime;
	rCmdUpdateAcParam.u4PhyRate = u4PhyRate;
	wlanSendSetQueryCmd(prAdapter, CMD_ID_UPDATE_AC_PARMS, TRUE, FALSE,
		FALSE, NULL, NULL, sizeof(struct CMD_UPDATE_AC_PARAMS),
		(uint8_t *)&rCmdUpdateAcParam, NULL, 0);

	DBGLOG(WMM, DEBUG, "Ac=%d, MediumTime=%d PhyRate=%u\n",
	       ucAc, u2MediumTime, u4PhyRate);

	wmmSyncPsParamWithFw(prAdapter, ucAc, ucBssIndex);
}

/* Return: AC List in bit map if this ac has active tspec */
uint8_t wmmHasActiveTspec(struct WMM_INFO *prWmmInfo)
{
	uint8_t ucTid = 0;
	uint8_t ucACList = 0;

	if (!prWmmInfo) {
		DBGLOG(WMM, DEBUG, "prWmmInfo is null\n");
		return 0;
	}

	/* if any tspec is active, it means */
	for (; ucTid < WMM_TSPEC_ID_NUM; ucTid++)
		if (prWmmInfo->arTsInfo[ucTid].eState == QOS_TS_ACTIVE)
			ucACList |= 1 << prWmmInfo->arTsInfo[ucTid].eAC;
	return ucACList;
}

void wmmRunEventTSOperate(struct ADAPTER *prAdapter,
			  struct MSG_HDR *prMsgHdr)
{
	struct MSG_TS_OPERATE *prMsgTsOperate =
		(struct MSG_TS_OPERATE *)prMsgHdr;
	uint8_t ucBssIndex = prMsgTsOperate->ucBssIdx;

	wmmTspecSteps(prAdapter, prMsgTsOperate->ucTid, prMsgTsOperate->eOpCode,
		(void *)&prMsgTsOperate->rTspecParam,
		ucBssIndex);
	cnmMemFree(prAdapter, prMsgHdr);
}

void wmmTspecSteps(struct ADAPTER *prAdapter, uint8_t ucTid,
	enum TSPEC_OP_CODE eOpCode, void *prStepParams,
	uint8_t ucBssIndex)
{
	struct AIS_FSM_INFO *prAisFsmInfo =
		aisGetAisFsmInfo(prAdapter, ucBssIndex);
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prCurTs = NULL;
	struct BSS_INFO *prAisBssInfo =
		aisGetAisBssInfo(prAdapter, ucBssIndex);

	ASSERT(prAisBssInfo);
	if (prAisBssInfo->eConnectionState !=
		    MEDIA_STATE_CONNECTED ||
	    prAisFsmInfo->eCurrentState == AIS_STATE_DISCONNECTING) {
		DBGLOG(WMM, DEBUG,
		       "ignore OP code %d when medium disconnected\n", eOpCode);
		return;
	}

	if (ucTid >= WMM_TSPEC_ID_NUM) {
		DBGLOG(WMM, DEBUG, "Invalid TID %d\n", ucTid);
		return;
	}

	prCurTs = &prWmmInfo->arTsInfo[ucTid];
	DBGLOG(WMM, TRACE, "TID %d, State %d, Oper %d\n", ucTid,
	       prCurTs->eState, eOpCode);

	switch (prCurTs->eState) {
	case QOS_TS_INACTIVE: {
		struct PARAM_QOS_TSPEC *prQosTspec =
			(struct PARAM_QOS_TSPEC *)prStepParams;

		if (eOpCode != TX_ADDTS_REQ)
			break;
		if (!prQosTspec) {
			DBGLOG(WMM, DEBUG, "Lack of Tspec Param\n");
			break;
		}
		/*Send ADDTS req Frame*/
		wmmTxTspecFrame(prAdapter, ucTid, TX_ADDTS_REQ, prQosTspec,
			ucBssIndex);

		/*start ADDTS timer*/
		cnmTimerStartTimer(prAdapter, &prCurTs->rAddTsTimer, 1000);
		prCurTs->eState = QOS_TS_SETUPING;
		prCurTs->eAC = aucUp2ACIMap[prQosTspec->rTsInfo.ucuserPriority];
		prCurTs->ucToken = prQosTspec->ucDialogToken;
		break;
	}
	case QOS_TS_SETUPING: {
		struct WMM_ADDTS_RSP_STEP_PARAM *prParam =
			(struct WMM_ADDTS_RSP_STEP_PARAM *)prStepParams;

		if (eOpCode == TX_DELTS_REQ || eOpCode == RX_DELTS_REQ ||
		    eOpCode == DISC_DELTS_REQ) {
			cnmTimerStopTimer(prAdapter, &prCurTs->rAddTsTimer);
			prCurTs->eState = QOS_TS_INACTIVE;
			DBGLOG(WMM, DEBUG, "Del Ts %d in setuping state\n",
			       ucTid);
			break;
		} else if (eOpCode != RX_ADDTS_RSP ||
			   prParam->ucDlgToken !=
				   prWmmInfo->arTsInfo[ucTid].ucToken)
			break;

		cnmTimerStopTimer(prAdapter, &prCurTs->rAddTsTimer);
		if (prParam->ucStatusCode == WMM_TS_STATUS_ADMISSION_ACCEPTED) {
			struct ACTIVE_RM_TSM_REQ *prActiveTsmReq = NULL;

			prCurTs->eState = QOS_TS_ACTIVE;
			prCurTs->eDir = prParam->eDir;
			prCurTs->fgUapsd = !!prParam->ucApsd;
			prCurTs->u2MediumTime = prParam->u2MediumTime;
			prCurTs->u4PhyRate = prParam->u4PhyRate;
			wmmSyncAcParamWithFw(prAdapter, prCurTs->eAC,
					     prParam->u2MediumTime,
					     prParam->u4PhyRate,
					     ucBssIndex);
			wmmMayDoTsReplacement(prAdapter, ucTid, ucBssIndex);
			/* start pending TSM if it was requested before admitted
			 */
			prActiveTsmReq = wmmGetActiveTsmReq(prAdapter, ucTid,
				TRUE, FALSE, ucBssIndex);
			if (prActiveTsmReq)
				wmmStartTsmMeasurement(
					prAdapter, (uintptr_t)prActiveTsmReq
							   ->prTsmReq,
				ucBssIndex);
			prActiveTsmReq = wmmGetActiveTsmReq(prAdapter, ucTid,
				FALSE, FALSE, ucBssIndex);
			if (prActiveTsmReq)
				wmmStartTsmMeasurement(
					prAdapter, (uintptr_t)prActiveTsmReq
							   ->prTsmReq,
				ucBssIndex);

			/* nicTxChangeDataPortByAc(
			 ** prAisBssInfo->prStaRecOfAP,
			 ** prCurTs->eAC, TRUE);
			 */
		} else {
			prCurTs->eState = QOS_TS_INACTIVE;
			DBGLOG(WMM, ERROR, "ADD TS is rejected, status=%d\n",
			       prParam->ucStatusCode);
		}
		break;
	}
	case QOS_TS_ACTIVE: {
		struct ACTIVE_RM_TSM_REQ *prActiveTsm = NULL;

		switch (eOpCode) {
		case TX_DELTS_REQ:
		case RX_DELTS_REQ:
		case DISC_DELTS_REQ:
			prActiveTsm = wmmGetActiveTsmReq(prAdapter, ucTid, TRUE,
				FALSE, ucBssIndex);
			if (prActiveTsm)
				wmmRemoveTSM(prAdapter, prActiveTsm, TRUE,
					ucBssIndex);
			prActiveTsm = wmmGetActiveTsmReq(prAdapter, ucTid,
				FALSE, FALSE, ucBssIndex);
			if (prActiveTsm)
				wmmRemoveTSM(prAdapter, prActiveTsm, TRUE,
					ucBssIndex);
			prCurTs->eState = QOS_TS_INACTIVE;
#if CFG_SUPPORT_SOFT_ACM
			/* Need to change tx queue, due to we do soft ACM */
			qmHandleDelTspec(prAdapter,
				aisGetTargetStaRec(prAdapter, ucBssIndex),
				prCurTs->eAC);
#endif
			wmmSyncAcParamWithFw(prAdapter, prCurTs->eAC, 0, 0,
				ucBssIndex);
			wmmDumpActiveTspecs(prAdapter, NULL, 0,
				ucBssIndex);
			if (eOpCode == TX_DELTS_REQ)
				wmmTxTspecFrame(
					prAdapter, ucTid, TX_DELTS_REQ,
					(struct PARAM_QOS_TSPEC *)prStepParams,
					ucBssIndex);
			break;
		case TX_ADDTS_REQ:
			/*Send ADDTS req Frame*/
			wmmTxTspecFrame(prAdapter, ucTid, TX_ADDTS_REQ,
					(struct PARAM_QOS_TSPEC *)prStepParams,
					ucBssIndex);
			prCurTs->eAC =
				aucUp2ACIMap[((struct PARAM_QOS_TSPEC *)
						      prStepParams)
						     ->rTsInfo.ucuserPriority];
			prCurTs->ucToken =
				((struct PARAM_QOS_TSPEC *)prStepParams)
					->ucDialogToken;
			/*start ADDTS timer*/
			cnmTimerStartTimer(prAdapter, &prCurTs->rAddTsTimer,
					   1000);
			break;
		/* for case: TS of tid N has existed, then setup TS with this
		 ** tid again.
		 */
		case RX_ADDTS_RSP: {
			struct WMM_ADDTS_RSP_STEP_PARAM *prParam =
				(struct WMM_ADDTS_RSP_STEP_PARAM *)prStepParams;

			if (prParam->ucStatusCode !=
			    WMM_TS_STATUS_ADMISSION_ACCEPTED) {
				DBGLOG(WMM, DEBUG,
				       "Update TS %d request was rejected by BSS\n",
				       ucTid);
				break;
			}
			prCurTs->eDir = prParam->eDir;
			prCurTs->fgUapsd = !!prParam->ucApsd;
			prCurTs->u2MediumTime = prParam->u2MediumTime;
			prCurTs->u4PhyRate = prParam->u4PhyRate;
			wmmSyncAcParamWithFw(prAdapter, prCurTs->eAC,
					     prParam->u2MediumTime,
					     prParam->u4PhyRate,
					     ucBssIndex);
			wmmMayDoTsReplacement(prAdapter, ucTid, ucBssIndex);
			break;
		}
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
}

static uint32_t wmmRunEventActionTxDone(struct ADAPTER *prAdapter,
					struct MSDU_INFO *prMsduInfo,
					enum ENUM_TX_RESULT_CODE rTxDoneStatus)
{
	DBGLOG(WMM, DEBUG, "Status %d\n", rTxDoneStatus);
	return WLAN_STATUS_SUCCESS;
}

void DumpData(uint8_t *prAddr, uint8_t uLen, char *tag)
{
	uint16_t k = 0;
	char buf[16 * 3 + 1];
	uint16_t loop = 0;
	uint8_t *p = prAddr;
	static char const charmap[16] = {'0', '1', '2', '3', '4', '5',
					 '6', '7', '8', '9', 'A', 'B',
					 'C', 'D', 'E', 'F'};

	uLen = (uLen > 128) ? 128 : uLen;
	loop = uLen / 16;
	if (tag)
		DBGLOG(WMM, DEBUG, "++++++++ dump data \"%s\" p=%p len=%d\n",
		       tag, prAddr, uLen);
	else
		DBGLOG(WMM, DEBUG, "++++++ dump data p=%p, len=%d\n", prAddr,
		       uLen);

	while (loop) {
		for (k = 0; k < 16; k++) {
			buf[k * 3] = charmap[((*(p + k) & 0xF0) >> 4)];
			buf[k * 3 + 1] = charmap[(*(p + k) & 0x0F)];
			buf[k * 3 + 2] = ' ';
		}
		buf[16 * 3] = 0;
		DBGLOG(WMM, DEBUG, "%s\n", buf);
		loop--;
		p += 16;
	}
	uLen = uLen % 16;
	k = 0;
	while (uLen) {
		buf[k * 3] = charmap[((*(p + k) & 0xF0) >> 4)];
		buf[k * 3 + 1] = charmap[(*(p + k) & 0x0F)];
		buf[k * 3 + 2] = ' ';
		k++;
		uLen--;
	}
	buf[k * 3] = 0;
	DBGLOG(WMM, DEBUG, "%s\n", buf);
	DBGLOG(WMM, DEBUG, "====== end dump data\n");
}

/* TSM related */
static void wmmQueryTsmResult(struct ADAPTER *prAdapter,
	uintptr_t ulParam)
{
	uint8_t ucBssIndex =
		((struct ACTIVE_RM_TSM_REQ *)ulParam)->ucBssIdx;
	struct RM_TSM_REQ *prTsmReq =
		((struct ACTIVE_RM_TSM_REQ *)ulParam)->prTsmReq;
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct CMD_GET_TSM_STATISTICS rGetTsmStatistics = {0};

	DBGLOG(WMM, DEBUG, "[%d] Query TSM statistics, tid = %d\n",
		ucBssIndex,
		prTsmReq->ucTID);
	DBGLOG(WMM, DEBUG, "%p , aci %d, duration %d\n", prTsmReq,
		prTsmReq->ucACI, prTsmReq->u2Duration);

	rGetTsmStatistics.ucBssIdx = ucBssIndex;
	rGetTsmStatistics.ucAcIndex = prTsmReq->ucACI;
	rGetTsmStatistics.ucTid = prTsmReq->ucTID;
	COPY_MAC_ADDR(rGetTsmStatistics.aucPeerAddr, prTsmReq->aucPeerAddr);

	wlanSendSetQueryCmd(prAdapter, CMD_ID_GET_TSM_STATISTICS, FALSE, TRUE,
			    FALSE, wmmComposeTsmRpt, NULL,
			    sizeof(struct CMD_GET_TSM_STATISTICS),
			    (uint8_t *)&rGetTsmStatistics, NULL, 0);
	cnmTimerInitTimer(prAdapter, &prWmmInfo->rTsmTimer,
		wmmGetTsmRptTimeout,
		ulParam);
	cnmTimerStartTimer(prAdapter, &prWmmInfo->rTsmTimer, 2000);

}

static struct ACTIVE_RM_TSM_REQ *wmmGetActiveTsmReq(struct ADAPTER *prAdapter,
						    uint8_t ucTid,
						    u_int8_t fgTriggered,
						    u_int8_t fgAllocIfNotExist,
						    uint8_t ucBssIndex)
{
	struct WMM_INFO *prWMMInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct ACTIVE_RM_TSM_REQ *prActiveReq = NULL;
	u_int8_t fgFound = FALSE;

	LINK_FOR_EACH_ENTRY(prActiveReq, &prWMMInfo->rActiveTsmReq, rLinkEntry,
			    struct ACTIVE_RM_TSM_REQ)
	{
		if ((!!prActiveReq->prTsmReq->u2Duration) == fgTriggered &&
		    ucTid == prActiveReq->prTsmReq->ucTID) {
			fgFound = TRUE;
			break;
		}
	}
	if (!fgFound && fgAllocIfNotExist) {
		fgFound = TRUE;
		prActiveReq = cnmMemAlloc(prAdapter, RAM_TYPE_BUF,
					  sizeof(struct ACTIVE_RM_TSM_REQ));
		if (!prActiveReq)
			return NULL;
		LINK_INSERT_TAIL(&prWMMInfo->rActiveTsmReq,
				 &prActiveReq->rLinkEntry);
	}
	return fgFound ? prActiveReq : NULL;
}

static void wmmRemoveTSM(struct ADAPTER *prAdapter,
			 struct ACTIVE_RM_TSM_REQ *prActiveTsm,
			 u_int8_t fgNeedStop,
			 uint8_t ucBssIndex)
{
	struct WMM_INFO *prWMMInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct LINK *prActiveTsmLink = &prWMMInfo->rActiveTsmReq;

	LINK_REMOVE_KNOWN_ENTRY(prActiveTsmLink, prActiveTsm);
	if (fgNeedStop) {
		struct CMD_SET_TSM_STATISTICS_REQUEST rTsmStatistics = {0};
		struct STA_RECORD *prStaRec = NULL;
		struct BSS_INFO *prAisBssInfo;

		prAisBssInfo =
			aisGetAisBssInfo(prAdapter,
			ucBssIndex);
		if (!prAisBssInfo) {
			DBGLOG(WMM, ERROR, "prAisBssInfo is NULL\n");
			return;
		}
		prStaRec = prAisBssInfo->prStaRecOfAP;
		nicTxChangeDataPortByAc(prAdapter,
					prStaRec,
					prActiveTsm->prTsmReq->ucACI,
					FALSE);
		rTsmStatistics.ucBssIdx = prAisBssInfo->ucBssIndex;
		rTsmStatistics.ucEnabled = FALSE;
		rTsmStatistics.ucAcIndex = prActiveTsm->prTsmReq->ucACI;
		rTsmStatistics.ucTid = prActiveTsm->prTsmReq->ucTID;
		COPY_MAC_ADDR(rTsmStatistics.aucPeerAddr,
			      prActiveTsm->prTsmReq->aucPeerAddr);
		wlanSendSetQueryCmd(prAdapter,
			CMD_ID_SET_TSM_STATISTICS_REQUEST, TRUE,
			FALSE, FALSE, NULL, NULL,
			sizeof(struct CMD_SET_TSM_STATISTICS_REQUEST),
			(uint8_t *)&rTsmStatistics, NULL, 0);
	}
	cnmMemFree(prAdapter, prActiveTsm->prTsmReq);
	cnmMemFree(prAdapter, prActiveTsm);
}

void wmmStartTsmMeasurement(struct ADAPTER *prAdapter, uintptr_t ulParam,
	uint8_t ucBssIndex)
{
	struct WMM_INFO *prWMMInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct CMD_SET_TSM_STATISTICS_REQUEST rTsmStatistics;
	struct RM_TSM_REQ *prTsmReq = (struct RM_TSM_REQ *)ulParam;
	uint8_t ucTid = prTsmReq->ucTID;
	struct ACTIVE_RM_TSM_REQ *prActiveTsmReq = NULL;
	struct STA_RECORD *prStaRec = NULL;
	struct TSPEC_INFO *prCurTs = NULL;
	struct BSS_INFO *prAisBssInfo;

	prAisBssInfo =
		aisGetAisBssInfo(prAdapter, ucBssIndex);

	ASSERT(prAisBssInfo);
	if (!prTsmReq->u2Duration &&
	    !(prTsmReq->rTriggerCond.ucCondition & TSM_TRIGGER_CONDITION_ALL)) {
		DBGLOG(WMM, WARN, "Duration is %d, Trigger Condition %d\n",
		       prTsmReq->u2Duration,
		       prTsmReq->rTriggerCond.ucCondition);
		cnmMemFree(prAdapter, prTsmReq);
		rrmScheduleNextRm(prAdapter,
			ucBssIndex);
		return;
	}
	prStaRec = prAisBssInfo->prStaRecOfAP;
	if (!prStaRec) {
		DBGLOG(WMM, DEBUG, "No station record found for "MACSTR"\n",
			MAC2STR(prTsmReq->aucPeerAddr));
		cnmMemFree(prAdapter, prTsmReq);
		rrmScheduleNextRm(prAdapter,
			ucBssIndex);
		return;
	}
	/* if there's a active tspec, then TID means TS ID */
	prCurTs = &prWMMInfo->arTsInfo[ucTid];
	if (prCurTs->eState == QOS_TS_ACTIVE)
		prTsmReq->ucACI = prCurTs->eAC;
	else { /* otherwise TID means TC ID */
		uint8_t ucTsAcs = wmmHasActiveTspec(prWMMInfo);

		prTsmReq->ucACI = aucUp2ACIMap[ucTid];
		/* if current TID is not admitted, don't start measurement, only
		 ** save this requirement
		 */
		if (prStaRec->afgAcmRequired[prTsmReq->ucACI] &&
		    !(ucTsAcs & BIT(prTsmReq->ucACI))) {
			DBGLOG(WMM, DEBUG,
			       "ACM is set for UP %d, but No tspec is setup\n",
			       ucTid);
			rrmScheduleNextRm(prAdapter,
				ucBssIndex);
			return;
		}
	}

	kalMemZero(&rTsmStatistics, sizeof(rTsmStatistics));
	if (prTsmReq->u2Duration) {
		/* If a non-AP QoS STa receives a Transmit Stream/Category
		 *Measurement Request for a TC, or
		 ** TS that is already being measured using a triggered transmit
		 *stream/category measurement,
		 ** the triggered traffic stream measurement shall be suspended
		 *for the duration of the requested
		 ** traffic stream measurement. When triggered measurement
		 *resumes, the traffic stream metrics
		 ** shall be reset.  See end part of 802.11k 11.10.8.8
		 **/
		LINK_FOR_EACH_ENTRY(prActiveTsmReq, &prWMMInfo->rActiveTsmReq,
				    rLinkEntry, struct ACTIVE_RM_TSM_REQ)
		{
			if (prActiveTsmReq->prTsmReq->u2Duration ||
			    prActiveTsmReq->prTsmReq->ucACI != prTsmReq->ucACI)
				continue;
			nicTxChangeDataPortByAc(
				prAdapter,
				prStaRec,
				prTsmReq->ucACI,
						FALSE);
			rTsmStatistics.ucBssIdx =
				ucBssIndex;
			rTsmStatistics.ucEnabled = FALSE;
			rTsmStatistics.ucAcIndex = prTsmReq->ucACI;
			rTsmStatistics.ucTid = prActiveTsmReq->prTsmReq->ucTID;
			COPY_MAC_ADDR(rTsmStatistics.aucPeerAddr,
				      prActiveTsmReq->prTsmReq->aucPeerAddr);
			wlanSendSetQueryCmd(
				prAdapter, CMD_ID_SET_TSM_STATISTICS_REQUEST,
				TRUE, FALSE, FALSE, NULL, NULL,
				sizeof(struct CMD_SET_TSM_STATISTICS_REQUEST),
				(uint8_t *)&rTsmStatistics, NULL, 0);
		}
		prActiveTsmReq = wmmGetActiveTsmReq(
			prAdapter, ucTid, !!prTsmReq->u2Duration, TRUE,
			ucBssIndex);
		if (prActiveTsmReq) {
			/* if exist normal tsm on the same ts, replace it */
			if (prActiveTsmReq->prTsmReq)
				cnmMemFree(prAdapter, prActiveTsmReq->prTsmReq);
			DBGLOG(WMM, DEBUG, "%p tid %d, aci %d, duration %d\n",
				prTsmReq, prTsmReq->ucTID, prTsmReq->ucACI,
				prTsmReq->u2Duration);
			prActiveTsmReq->ucBssIdx = ucBssIndex;
			cnmTimerInitTimer(prAdapter, &prWMMInfo->rTsmTimer,
				wmmQueryTsmResult,
				(uintptr_t)prActiveTsmReq);
			cnmTimerStartTimer(prAdapter, &prWMMInfo->rTsmTimer,
					   TU_TO_MSEC(prTsmReq->u2Duration));
		}
	} else {
		prActiveTsmReq = wmmGetActiveTsmReq(
			prAdapter, ucTid, !prTsmReq->u2Duration, TRUE,
			ucBssIndex);
		if (prActiveTsmReq) {
			/* if exist triggered tsm on the same ts, replace it */
			if (prActiveTsmReq->prTsmReq) {
				cnmTimerStopTimer(prAdapter,
						  &prActiveTsmReq->rTsmTimer);
				cnmMemFree(prAdapter, prActiveTsmReq->prTsmReq);
			}
			rTsmStatistics.ucTriggerCondition =
				prTsmReq->rTriggerCond.ucCondition;
			rTsmStatistics.ucMeasureCount =
				prTsmReq->rTriggerCond.ucMeasureCount;
			rTsmStatistics.ucTriggerTimeout =
				prTsmReq->rTriggerCond.ucTriggerTimeout;
			rTsmStatistics.ucAvgErrThreshold =
				prTsmReq->rTriggerCond.ucAvgErrThreshold;
			rTsmStatistics.ucConsecutiveErrThreshold =
				prTsmReq->rTriggerCond.ucConsecutiveErr;
			rTsmStatistics.ucDelayThreshold =
				prTsmReq->rTriggerCond.ucDelayThreshold;
			rTsmStatistics.ucBin0Range = prTsmReq->ucB0Range;
		}
	}
	nicTxChangeDataPortByAc(prAdapter, prStaRec, prTsmReq->ucACI, TRUE);
	if (prActiveTsmReq)
		prActiveTsmReq->prTsmReq = prTsmReq;
	rTsmStatistics.ucBssIdx = ucBssIndex;
	rTsmStatistics.ucAcIndex = prTsmReq->ucACI;
	rTsmStatistics.ucTid = prTsmReq->ucTID;
	rTsmStatistics.ucEnabled = TRUE;
	COPY_MAC_ADDR(rTsmStatistics.aucPeerAddr, prTsmReq->aucPeerAddr);
	DBGLOG(WMM, DEBUG, "enabled=%d, tid=%d\n", rTsmStatistics.ucEnabled,
	       ucTid);
	wlanSendSetQueryCmd(prAdapter, CMD_ID_SET_TSM_STATISTICS_REQUEST, TRUE,
			    FALSE, FALSE, NULL, NULL,
			    sizeof(struct CMD_SET_TSM_STATISTICS_REQUEST),
			    (uint8_t *)&rTsmStatistics, NULL, 0);
}

void wmmRemoveAllTsmMeasurement(struct ADAPTER *prAdapter,
	u_int8_t fgOnlyTriggered, uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct LINK *prActiveTsmLink =
		&prWmmInfo->rActiveTsmReq;
	struct ACTIVE_RM_TSM_REQ *prActiveTsm = NULL;
	struct ACTIVE_RM_TSM_REQ *prHead = LINK_PEEK_HEAD(
		prActiveTsmLink, struct ACTIVE_RM_TSM_REQ, rLinkEntry);
	u_int8_t fgFinished = FALSE;

	if (!fgOnlyTriggered)
		cnmTimerStopTimer(prAdapter,
				  &prWmmInfo->rTsmTimer);
	do {
		prActiveTsm = LINK_PEEK_TAIL(
			prActiveTsmLink, struct ACTIVE_RM_TSM_REQ, rLinkEntry);
		if (!prActiveTsm)
			break;
		if (prActiveTsm == prHead)
			fgFinished = TRUE;
		if (fgOnlyTriggered && prActiveTsm->prTsmReq->u2Duration)
			continue;
		wmmRemoveTSM(prAdapter, prActiveTsm, TRUE, ucBssIndex);
	} while (!fgFinished);
	prWmmInfo->rTriggeredTsmRptTime = 0;
}

u_int8_t wmmParseQosAction(struct ADAPTER *prAdapter,
			   struct SW_RFB *prSwRfb)
{
	struct WLAN_ACTION_FRAME *prWlanActionFrame = NULL;
	uint8_t *pucIE = NULL;
	struct PARAM_QOS_TSPEC rTspec = {0};
	uint16_t u2Offset = 0;
	uint16_t u2IEsBufLen = 0;
	uint8_t ucTid = WMM_TSPEC_ID_NUM;
	struct WMM_ADDTS_RSP_STEP_PARAM rStepParam;
	u_int8_t ret = FALSE;
	uint8_t ucBssIndex = secGetBssIdxByRfb(prAdapter,
		prSwRfb);

	prWlanActionFrame = (struct WLAN_ACTION_FRAME *)prSwRfb->pvHeader;

	DBGLOG(WMM, DEBUG, "[%d] Action=%d\n",
		ucBssIndex,
		prWlanActionFrame->ucAction);
	switch (prWlanActionFrame->ucAction) {
	case ACTION_ADDTS_RSP: {
		kalMemZero(&rStepParam, sizeof(rStepParam));
		if (prWlanActionFrame->ucCategory ==
		    CATEGORY_WME_MGT_NOTIFICATION) {
			struct WMM_ACTION_TSPEC_FRAME *prAddTsRsp =
				(struct WMM_ACTION_TSPEC_FRAME *)
					prWlanActionFrame;

			rStepParam.ucDlgToken = prAddTsRsp->ucDlgToken;
			rStepParam.ucStatusCode = prAddTsRsp->ucStatusCode;
			pucIE = (uint8_t *)prAddTsRsp->aucInfoElem;
		} else if (prWlanActionFrame->ucCategory ==
			   CATEGORY_QOS_ACTION) {
			struct ACTION_ADDTS_RSP_FRAME *prAddTsRsp =
				(struct ACTION_ADDTS_RSP_FRAME *)
					prWlanActionFrame;

			rStepParam.ucDlgToken = prAddTsRsp->ucDialogToken;
			rStepParam.ucStatusCode = prAddTsRsp->ucStatusCode;
			pucIE = (uint8_t *)prAddTsRsp->aucInfoElem;
		} else {
			DBGLOG(WMM, DEBUG,
			       "Not supported category %d for action %d\n",
			       prWlanActionFrame->ucCategory,
			       prWlanActionFrame->ucAction);
			break;
		}

		/* underflow check */
		if (prSwRfb->u2PacketLen <
			(uint16_t)(OFFSET_OF(struct ACTION_ADDTS_RSP_FRAME,
					     aucInfoElem)))
			break;


		/*for each IE*/
		u2IEsBufLen = prSwRfb->u2PacketLen -
			(uint16_t)(OFFSET_OF(struct ACTION_ADDTS_RSP_FRAME,
					     aucInfoElem));

		IE_FOR_EACH(pucIE, u2IEsBufLen, u2Offset)
		{
			switch (IE_ID(pucIE)) {
			case ELEM_ID_TSPEC:
			case ELEM_ID_VENDOR:
				if (wmmParseTspecIE(prAdapter, pucIE,
						    &rTspec)) {
					rStepParam.u2MediumTime =
						rTspec.u2MediumTime;
					ucTid = rTspec.rTsInfo.ucTid;
					rStepParam.eDir =
						rTspec.rTsInfo.ucDirection;
					rStepParam.u4PhyRate =
						rTspec.u4MinPHYRate;
					rStepParam.ucApsd =
						rTspec.rTsInfo.ucApsd;
				} else {
					DBGLOG(WMM, DEBUG,
					       "can't parse Tspec IE?!\n");
				}
				break;
			default:
				break;
			}
		}
		wmmTspecSteps(prAdapter, ucTid, RX_ADDTS_RSP, &rStepParam,
			ucBssIndex);
		ret = TRUE;
		break;
	}
	case ACTION_DELTS: {
		if (prWlanActionFrame->ucCategory ==
		    CATEGORY_WME_MGT_NOTIFICATION) {
			/* wmm Tspec */
			struct WMM_ACTION_TSPEC_FRAME *prDelTs =
				(struct WMM_ACTION_TSPEC_FRAME *)
					prWlanActionFrame;

			u2IEsBufLen = prSwRfb->u2PacketLen -
				      (uint16_t)OFFSET_OF(
					      struct WMM_ACTION_TSPEC_FRAME,
					      aucInfoElem);
			u2Offset = 0;
			pucIE = prDelTs->aucInfoElem;
			IE_FOR_EACH(pucIE, u2IEsBufLen, u2Offset)
			{
				if (!wmmParseTspecIE(prAdapter, pucIE, &rTspec))
					continue;
				ucTid = rTspec.rTsInfo.ucTid;
				break;
			}
		} else if (prWlanActionFrame->ucCategory ==
			   CATEGORY_QOS_ACTION) {
			/* IEEE 802.11 Tspec */
			struct ACTION_DELTS_FRAME *prDelTs =
				(struct ACTION_DELTS_FRAME *)prWlanActionFrame;

			ucTid = WMM_TSINFO_TSID(prDelTs->aucTsInfo[0]);
		}

		wmmTspecSteps(prAdapter, ucTid, RX_DELTS_REQ, NULL,
			ucBssIndex);
		ret = TRUE;
		break;
	}
	default:
		break;
	}
	return ret;
}

u_int8_t wmmParseTspecIE(struct ADAPTER *prAdapter, uint8_t *pucIE,
			 struct PARAM_QOS_TSPEC *prTspec)
{
	uint32_t u4TsInfoValue = 0;
	uint8_t *pucTemp = NULL;

	if (IE_ID(pucIE) == ELEM_ID_TSPEC) {
		DBGLOG(WMM, DEBUG, "found 802.11 Tspec Information Element\n");
		/* todo: implement 802.11 Tspec here, assign value to
		 ** u4TsInfoValue and pucTemp
		 */
		u4TsInfoValue = 0;
		pucTemp = NULL;
		return FALSE; /* we didn't support IEEE 802.11 Tspec now */
	}
	{
		struct IE_WMM_TSPEC *prIeWmmTspec =
			(struct IE_WMM_TSPEC *)pucIE;
		uint8_t aucWfaOui[] = VENDOR_OUI_WFA;

		/* WMM TSPEC length */
		if (prIeWmmTspec->ucLength < ELEM_MAX_LEN_WMM_TSPEC) {
			DBGLOG(WMM, DEBUG, "Abnormal IE length\n");
			return FALSE;
		}

		if (prIeWmmTspec->ucId != ELEM_ID_VENDOR ||
		    kalMemCmp(prIeWmmTspec->aucOui, aucWfaOui,
			      sizeof(aucWfaOui)) ||
		    prIeWmmTspec->ucOuiType != VENDOR_OUI_TYPE_WMM ||
		    prIeWmmTspec->ucOuiSubtype !=
			    VENDOR_OUI_SUBTYPE_WMM_TSPEC) {
			return FALSE;
		}
		u4TsInfoValue |= prIeWmmTspec->aucTsInfo[0];
		u4TsInfoValue |= (prIeWmmTspec->aucTsInfo[1] << 8);
		u4TsInfoValue |= (prIeWmmTspec->aucTsInfo[2] << 16);
		pucTemp = prIeWmmTspec->aucTspecBodyPart;
	}

	prTspec->rTsInfo.ucTrafficType = WMM_TSINFO_TRAFFIC_TYPE(u4TsInfoValue);
	prTspec->rTsInfo.ucTid = WMM_TSINFO_TSID(u4TsInfoValue);
	prTspec->rTsInfo.ucDirection = WMM_TSINFO_DIR(u4TsInfoValue);
	prTspec->rTsInfo.ucAccessPolicy = WMM_TSINFO_AC(u4TsInfoValue);
	prTspec->rTsInfo.ucApsd = WMM_TSINFO_PSB(u4TsInfoValue);
	prTspec->rTsInfo.ucuserPriority = WMM_TSINFO_UP(u4TsInfoValue);

	/* nominal size*/
	WLAN_GET_FIELD_16(pucTemp, &prTspec->u2NominalMSDUSize);
	pucTemp += 2;
	WLAN_GET_FIELD_16(pucTemp, &prTspec->u2MaxMSDUsize);
	pucTemp += 2;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MinSvcIntv);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MaxSvcIntv);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4InactIntv);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4SpsIntv);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4SvcStartTime);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MinDataRate);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MeanDataRate);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4PeakDataRate);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MaxBurstSize);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4DelayBound);
	pucTemp += 4;
	WLAN_GET_FIELD_32(pucTemp, &prTspec->u4MinPHYRate);
	pucTemp += 4;
	WLAN_GET_FIELD_16(pucTemp, &prTspec->u2Sba);
	pucTemp += 2;
	WLAN_GET_FIELD_16(pucTemp, &prTspec->u2MediumTime);
	pucTemp += 2;
	ASSERT((pucTemp == (IE_SIZE(pucIE) + pucIE)));
	DBGLOG(WMM, DEBUG, "TsId=%d, TrafficType=%d, PSB=%d, MediumTime=%d\n",
	       prTspec->rTsInfo.ucTid, prTspec->rTsInfo.ucTrafficType,
	       prTspec->rTsInfo.ucApsd, prTspec->u2MediumTime);
	return TRUE;
}

static void wmmGetTsmRptTimeout(struct ADAPTER *prAdapter,
	uintptr_t ulParam)
{
	uint8_t ucBssIndex =
		((struct ACTIVE_RM_TSM_REQ *)ulParam)->ucBssIdx;
	DBGLOG(WMM, ERROR,
		"[%d] timeout to get Tsm Rpt from firmware\n", ucBssIndex);
	wlanReleasePendingCmdById(prAdapter, CMD_ID_GET_TSM_STATISTICS);
	wmmRemoveTSM(prAdapter, (struct ACTIVE_RM_TSM_REQ *)ulParam, TRUE,
		ucBssIndex);
	/* schedule next measurement after a duration based TSM done */
	rrmStartNextMeasurement(prAdapter, FALSE, ucBssIndex);
}

void wmmComposeTsmRpt(struct ADAPTER *prAdapter, struct CMD_INFO *prCmdInfo,
		      uint8_t *pucEventBuf)
{
	struct CMD_GET_TSM_STATISTICS *prTsmStatistic =
		(struct CMD_GET_TSM_STATISTICS *)pucEventBuf;
	uint8_t ucBssIndex = prTsmStatistic->ucBssIdx;
	struct RADIO_MEASUREMENT_REPORT_PARAMS *prRmRep =
		aisGetRmReportParam(prAdapter, ucBssIndex);
	struct IE_MEASUREMENT_REPORT *prTsmRpt = NULL;
	struct RM_TSM_REPORT *prTsmRptField = NULL;
	uint16_t u2IeSize =
		OFFSET_OF(struct IE_MEASUREMENT_REPORT, aucReportFields) +
		sizeof(*prTsmRptField);
	struct ACTIVE_RM_TSM_REQ *prCurrentTsmReq = NULL;
	struct WMM_INFO *prWMMInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct BSS_INFO *prAisBssInfo =
		aisGetAisBssInfo(prAdapter, ucBssIndex);

	if (!prAisBssInfo) {
		DBGLOG(WMM, ERROR, "prAisBssInfo is NULL\n");
		return;
	}
	prCurrentTsmReq =
		wmmGetActiveTsmReq(prAdapter, prTsmStatistic->ucTid,
				   !prTsmStatistic->ucReportReason, FALSE,
				   ucBssIndex);
	/* prCmdInfo is not NULL or report reason is 0 means it is a command
	 ** reply, so we need to stop the timer
	 */
	if (prCmdInfo || !prTsmStatistic->ucReportReason)
		cnmTimerStopTimer(prAdapter, &prWMMInfo->rTsmTimer);
	if (!prCurrentTsmReq) {
		DBGLOG(WMM, ERROR, "unexpected Tsm statistic event, tid %d\n",
		       prTsmStatistic->ucTid);
		/* schedule next measurement after a duration based TSM done */
		rrmScheduleNextRm(prAdapter,
			ucBssIndex);
		return;
	}

	/* Put the report IE into report frame */
	if (u2IeSize + prRmRep->u2ReportFrameLen > RM_REPORT_FRAME_MAX_LENGTH)
		rrmTxRadioMeasurementReport(prAdapter, ucBssIndex);

	DBGLOG(WMM, DEBUG, "tid %d, aci %d\n", prCurrentTsmReq->prTsmReq->ucTID,
	       prCurrentTsmReq->prTsmReq->ucACI);
	prTsmRpt =
		(struct IE_MEASUREMENT_REPORT *)(prRmRep->pucReportFrameBuff +
						 prRmRep->u2ReportFrameLen);
	prTsmRpt->ucId = ELEM_ID_MEASUREMENT_REPORT;
	prTsmRpt->ucToken = prCurrentTsmReq->prTsmReq->ucToken;
	prTsmRpt->ucMeasurementType = ELEM_RM_TYPE_TSM_REPORT;
	prTsmRpt->ucReportMode = 0;
	prTsmRpt->ucLength = u2IeSize - 2;
	prTsmRptField = (struct RM_TSM_REPORT *)&prTsmRpt->aucReportFields[0];
	prTsmRptField->u8ActualStartTime = prTsmStatistic->u8StartTime;
	prTsmRptField->u2Duration = prCurrentTsmReq->prTsmReq->u2Duration;
	COPY_MAC_ADDR(prTsmRptField->aucPeerAddress,
		      prTsmStatistic->aucPeerAddr);
	/* TID filed: bit0~bit3 reserved, bit4~bit7: real tid */
	prTsmRptField->ucTID = (prCurrentTsmReq->prTsmReq->ucTID & 0xf) << 4;
	prTsmRptField->ucReason = prTsmStatistic->ucReportReason;
	prTsmRptField->u4TransmittedMsduCnt = prTsmStatistic->u4PktTxDoneOK;
	prTsmRptField->u4DiscardedMsduCnt = prTsmStatistic->u4PktDiscard;
	prTsmRptField->u4FailedMsduCnt = prTsmStatistic->u4PktFail;
	prTsmRptField->u4MultiRetryCnt = prTsmStatistic->u4PktRetryTxDoneOK;
	prTsmRptField->u4CfPollLostCnt = prTsmStatistic->u4PktQosCfPollLost;
	prTsmRptField->u4AvgQueDelay = prTsmStatistic->u4AvgPktQueueDelay;
	prTsmRptField->u4AvgDelay = prTsmStatistic->u4AvgPktTxDelay;
	prTsmRptField->ucBin0Range = prCurrentTsmReq->prTsmReq->ucB0Range;
	kalMemCopy(&prTsmRptField->u4Bin[0], &prTsmStatistic->au4PktCntBin[0],
		   sizeof(prTsmStatistic->au4PktCntBin));
	prRmRep->u2ReportFrameLen += u2IeSize;
	/* For normal TSM, only once measurement */
	if (prCurrentTsmReq->prTsmReq->u2Duration) {
		struct RM_TSM_REQ *prTsmReq = NULL;
		struct CMD_SET_TSM_STATISTICS_REQUEST rTsmStatistics;

		wmmRemoveTSM(prAdapter, prCurrentTsmReq, FALSE, ucBssIndex);
		/* Resume all triggered tsm whose TC is same with this normal
		 ** tsm
		 */
		LINK_FOR_EACH_ENTRY(prCurrentTsmReq, &prWMMInfo->rActiveTsmReq,
				    rLinkEntry, struct ACTIVE_RM_TSM_REQ)
		{
			prTsmReq = prCurrentTsmReq->prTsmReq;
			if (prTsmReq->u2Duration ||
			    prTsmReq->ucACI != prTsmStatistic->ucAcIndex)
				continue;

			nicTxChangeDataPortByAc(
				prAdapter,
				prAisBssInfo->prStaRecOfAP,
				prTsmReq->ucACI, TRUE);
			kalMemZero(&rTsmStatistics, sizeof(rTsmStatistics));
			rTsmStatistics.ucBssIdx =
				ucBssIndex;
			rTsmStatistics.ucEnabled = TRUE;
			rTsmStatistics.ucAcIndex = prTsmReq->ucACI;
			rTsmStatistics.ucTid = prTsmReq->ucTID;
			COPY_MAC_ADDR(rTsmStatistics.aucPeerAddr,
				      prTsmReq->aucPeerAddr);
			rTsmStatistics.ucTriggerCondition =
				prTsmReq->rTriggerCond.ucCondition;
			rTsmStatistics.ucMeasureCount =
				prTsmReq->rTriggerCond.ucMeasureCount;
			rTsmStatistics.ucTriggerTimeout =
				prTsmReq->rTriggerCond.ucTriggerTimeout;
			rTsmStatistics.ucAvgErrThreshold =
				prTsmReq->rTriggerCond.ucAvgErrThreshold;
			rTsmStatistics.ucConsecutiveErrThreshold =
				prTsmReq->rTriggerCond.ucConsecutiveErr;
			rTsmStatistics.ucDelayThreshold =
				prTsmReq->rTriggerCond.ucDelayThreshold;
			rTsmStatistics.ucBin0Range = prTsmReq->ucB0Range;
			wlanSendSetQueryCmd(
				prAdapter, CMD_ID_SET_TSM_STATISTICS_REQUEST,
				TRUE, FALSE, FALSE, NULL, NULL,
				sizeof(rTsmStatistics),
				(uint8_t *)&rTsmStatistics, NULL, 0);
		}
		/* schedule next measurement after a duration based TSM done */
		rrmScheduleNextRm(prAdapter, ucBssIndex);
	} else {
		/* Triggered TSM, we should send TSM report to peer if the first
		 ** report time to now more than 10 second
		 */
		OS_SYSTIME rCurrent = kalGetTimeTick();

		if (prWMMInfo->rTriggeredTsmRptTime == 0)
			prWMMInfo->rTriggeredTsmRptTime = rCurrent;
		else if (CHECK_FOR_TIMEOUT(rCurrent,
					   prWMMInfo->rTriggeredTsmRptTime,
					   10000)) {
			rrmTxRadioMeasurementReport(prAdapter, ucBssIndex);
			prWMMInfo->rTriggeredTsmRptTime = 0;
		}
	}
}

void wmmNotifyDisconnected(struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	uint8_t ucTid = 0;

	for (; ucTid < WMM_TSPEC_ID_NUM; ucTid++)
		wmmTspecSteps(prAdapter, ucTid, DISC_DELTS_REQ, NULL,
			ucBssIndex);
	wmmRemoveAllTsmMeasurement(prAdapter, FALSE,
		ucBssIndex);
#if CFG_SUPPORT_SOFT_ACM
	kalMemZero(&prWmmInfo->arAcmCtrl[0],
		   sizeof(prWmmInfo->arAcmCtrl));
#endif
}

u_int8_t wmmTsmIsOngoing(struct ADAPTER *prAdapter,
	uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);

	return !LINK_IS_EMPTY(&prWmmInfo->rActiveTsmReq);
}

/* This function implements TS replacement rule
 ** Replace case base on same AC:
 ** 1. old: Uni-dir; New: Bi-dir or same dir with old
 ** 2. old: Bi-dir; New: Bi-dir or Uni-dir
 ** 3. old: two diff Uni-dir; New: Bi-dir
 ** for detail, see WMM spec V1.2.0, section 3.5
 */
static void wmmMayDoTsReplacement(struct ADAPTER *prAdapter,
	uint8_t ucNewTid, uint8_t ucBssIndex)
{
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prTspec = &prWmmInfo->arTsInfo[0];
	uint8_t ucTid = 0;

	for (; ucTid < WMM_TSPEC_ID_NUM; ucTid++) {
		if (ucTid == ucNewTid)
			continue;
		if (prTspec[ucTid].eState != QOS_TS_ACTIVE ||
		    prTspec[ucTid].eAC != prTspec[ucNewTid].eAC)
			continue;
		if (prTspec[ucNewTid].eDir != prTspec[ucTid].eDir &&
		    prTspec[ucNewTid].eDir < BI_DIR_TS &&
		    prTspec[ucTid].eDir < BI_DIR_TS)
			continue;
		prTspec[ucTid].eAC = ACI_NUM;
		prTspec[ucTid].eState = QOS_TS_INACTIVE;
	}
	wmmDumpActiveTspecs(prAdapter, NULL, 0, ucBssIndex);
}

uint32_t wmmDumpActiveTspecs(struct ADAPTER *prAdapter, uint8_t *pucBuffer,
	uint16_t u2BufferLen, uint8_t ucBssIndex)
{
	uint8_t ucTid = 0;
	int32_t i4BytesWritten = 0;
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	struct TSPEC_INFO *prTspec = &prWmmInfo->arTsInfo[0];

	for (; ucTid < WMM_TSPEC_ID_NUM; ucTid++, prTspec++) {
		if (prTspec->eState != QOS_TS_ACTIVE)
			continue;
		if (u2BufferLen > 0 && pucBuffer) {
			i4BytesWritten += kalSnprintf(
				pucBuffer + i4BytesWritten, u2BufferLen,
				"Tid %d, AC %d, Dir %d, Uapsd %d, MediumTime %d, PhyRate %u\n",
				ucTid, prTspec->eAC, prTspec->eDir,
				prTspec->fgUapsd, prTspec->u2MediumTime,
				prTspec->u4PhyRate);
			if (i4BytesWritten <= 0)
				break;
			u2BufferLen -= (uint16_t)i4BytesWritten;
		} else
			DBGLOG(WMM, DEBUG,
			       "Tid %d, AC %d, Dir %d, Uapsd %d, MediumTime %d, PhyRate %u\n",
			       ucTid, prTspec->eAC, prTspec->eDir,
			       prTspec->fgUapsd, prTspec->u2MediumTime,
			       prTspec->u4PhyRate);
	}
	if (u2BufferLen > 0 && pucBuffer) {
		struct STA_RECORD *prStaRec =
			aisGetStaRecOfAP(prAdapter, ucBssIndex);

		if (prStaRec) {
			i4BytesWritten += kalSnprintf(
				pucBuffer + i4BytesWritten, u2BufferLen,
				"\nACM status for AP "MACSTR
				":\nBE %d; BK %d; VI %d; VO %d\n",
				MAC2STR(prStaRec->aucMacAddr),
				prStaRec->afgAcmRequired[ACI_BE],
				prStaRec->afgAcmRequired[ACI_BK],
				prStaRec->afgAcmRequired[ACI_VI],
				prStaRec->afgAcmRequired[ACI_VO]);
		} else
			i4BytesWritten += kalSnprintf(
				pucBuffer + i4BytesWritten, u2BufferLen, "%s\n",
				"Didn't connect to a AP");
	}
	return (uint32_t)i4BytesWritten;
}

#if CFG_SUPPORT_SOFT_ACM
/* u2PktLen: Ethernet payload length, exclude eth header.
 ** Return value: estimated tx time in unit us.
 */
uint32_t wmmCalculatePktUsedTime(struct BSS_INFO *prBssInfo,
				 struct STA_RECORD *prStaRec, uint16_t u2PktLen)
{
	uint8_t ucSecurityPadding = 0;
	int8_t i = 0;
	uint32_t u4TxTime = 0;
	uint8_t ucFlags = 0;

	ASSERT(prBssInfo);
	ASSERT(prStaRec);
	switch (prBssInfo->u4RsnSelectedPairwiseCipher) {
	case RSN_CIPHER_SUITE_CCMP:
		ucSecurityPadding = 16;
		break;
	case RSN_CIPHER_SUITE_TKIP:
		ucSecurityPadding = 20;
		break;
	case RSN_CIPHER_SUITE_WEP104:
	case RSN_CIPHER_SUITE_WEP40:
		ucSecurityPadding = 8;
		break;
	default:
		ucSecurityPadding = 0;
		break;
	}
	/* ToDo: 802.11AC? WMM-AC didn't support 802.11AC now */
	if (prStaRec->ucDesiredPhyTypeSet & PHY_TYPE_SET_802_11N) {
		if (prBssInfo->fg40mBwAllowed) {
			if (prStaRec->u2HtCapInfo & HT_CAP_INFO_SHORT_GI_40M)
				ucFlags |= FLAG_SHORT_GI;
			ucFlags |= FLAG_40M_BW;
		} else if (prStaRec->u2HtCapInfo & HT_CAP_INFO_SHORT_GI_20M)
			ucFlags |= FLAG_SHORT_GI;

		u4TxTime = wmmAcmTxTimeHtCal(ucSecurityPadding, u2PktLen, 7,
					     ucFlags);
		DBGLOG(WMM, DEBUG,
		       "MCS 7, Tx %d bytes, SecExtra %d bytes, Flags %02x, Time %u us\n",
		       u2PktLen, ucSecurityPadding, ucFlags, u4TxTime);
	} else {
		uint16_t u2DataRate = RATE_54M;

		if (prStaRec->ucDesiredPhyTypeSet & PHY_TYPE_802_11G) {
			u2DataRate = RATE_54M;
			for (i = 13; i >= 4; i--) {
				if ((prStaRec->u2BSSBasicRateSet & BIT(i)) &&
				    aucDataRate[i] <= u2DataRate)
					break;
			}
			ucFlags |= FLAG_G_MODE;
		} else if (prStaRec->ucDesiredPhyTypeSet &
			   (PHY_TYPE_802_11B | PHY_CONFIG_802_11A)) {
			u2DataRate = RATE_11M;
			for (i = 3; i >= 0; i--) {
				if ((prStaRec->u2BSSBasicRateSet & BIT(i)) &&
				    aucDataRate[i] <= u2DataRate)
					break;
			}
		}

		if (i >= 0) {
			if (prBssInfo->fgUseShortPreamble)
				ucFlags |= FLAG_S_PREAMBLE;
			u4TxTime = wmmAcmTxTimeCal(ucSecurityPadding, u2PktLen,
					u2DataRate, aucDataRate[i], ucFlags);
			DBGLOG(WMM, DEBUG,
			       "DataRate %d, BasicRate %d, Tx %d bytes, SecExtra %d bytes, Flags %02x, Time %u us\n",
			       u2DataRate, aucDataRate[i], u2PktLen,
			       ucSecurityPadding, ucFlags, u4TxTime);
		} else {
			u4TxTime = 0;
			DBGLOG(WMM, ERROR, "i is %d !!", i);
		}
	}
	return u4TxTime;
}

/* 1. u4PktTxTime is 0, this function give a fast check if remain medium time is
 *enough to Deq
 ** 2. u4PktTxTime is not 0, if remain medium time is greater than u4PktTxTime,
 *statistic deq number
 **     and remain time. Otherwise, start a timer to schedule next dequeue
 *interval
 ** return value:
 ** TRUE: Can dequeue
 ** FALSE: No time to dequeue
 */
u_int8_t wmmAcmCanDequeue(struct ADAPTER *prAdapter, uint8_t ucAc,
	uint32_t u4PktTxTime, uint8_t ucBssIndex)
{
	struct SOFT_ACM_CTRL *prAcmCtrl = NULL;
	struct WMM_INFO *prWmmInfo =
		aisGetWMMInfo(prAdapter, ucBssIndex);
	uint64_t u8CurTime = 0;

	if (!prWmmInfo) {
		DBGLOG(WMM, DEBUG, "prWmmInfo is null %d\n", ucBssIndex);
		return FALSE;
	}

	prAcmCtrl = &prWmmInfo->arAcmCtrl[ucAc];
	if (!prAcmCtrl->u8AdmittedTime)
		return FALSE;

	u8CurTime = kal_div64_u64(kalGetBootTime(), USEC_PER_SEC);

	if (!TIME_BEFORE(u8CurTime, prAcmCtrl->u8IntervalEndSec)) {
		u8CurTime++;
		DBGLOG(WMM, DEBUG,
		       "AC %d, Admitted %lu, LastEnd %lu, NextEnd %lu, LastUsed %lu, LastDeq %d\n",
		       ucAc, prAcmCtrl->u8AdmittedTime,
		       prAcmCtrl->u8IntervalEndSec, u8CurTime,
		       prAcmCtrl->u8AdmittedTime - prAcmCtrl->u8RemainTime,
		       prAcmCtrl->u2DeqNum);
		prAcmCtrl->u8IntervalEndSec = u8CurTime;
		prAcmCtrl->u8RemainTime = prAcmCtrl->u8AdmittedTime;
		prAcmCtrl->u2DeqNum = 0;
		/* Stop the next dequeue timer due to we will dequeue right now.
		 */
		if (timerPendingTimer(&prWmmInfo->rAcmDeqTimer))
			cnmTimerStopTimer(prAdapter, &prWmmInfo->rAcmDeqTimer);
	}
	if (!u4PktTxTime) {
		DBGLOG(WMM, TRACE, "AC %d, can dq %d\n", ucAc,
		       (prAcmCtrl->u8RemainTime > 0));
		return (prAcmCtrl->u8RemainTime > 0);
	}
	/* If QM request to dequeue, and have enough medium time,  then dequeue
	 */
	if (prAcmCtrl->u8RemainTime >= u4PktTxTime) {
		prAcmCtrl->u2DeqNum++;
		prAcmCtrl->u8RemainTime -= u4PktTxTime;
		DBGLOG(WMM, DEBUG, "AC %d, Remain %lu, DeqNum %d\n", ucAc,
		       prAcmCtrl->u8RemainTime, prAcmCtrl->u2DeqNum);
		if (prAcmCtrl->u8RemainTime > 0)
			return TRUE;
	}
	/* If not enough medium time to dequeue next packet, should start a
	 * timer to schedue next dequeue
	 * We didn't consider the case u8RemainTime is enough to dequeue
	 * packets except the head of the
	 * station tx queue, because it is too complex to implement dequeue
	 * routine.
	 * We should reset u8RemainTime to 0, used to skip next dequeue request
	 * if still in this deq interval.
	 * the dequeue interval is 1 second according to WMM-AC specification.
	 */
	prAcmCtrl->u8RemainTime = 0;
	/* Start a timer to schedule next dequeue interval, since application
	 * may stop sending data to driver,
	 * but driver still pending some data to dequeue
	 */
	if (!timerPendingTimer(&prWmmInfo->rAcmDeqTimer)) {
		uint64_t u8EndMsec = prAcmCtrl->u8IntervalEndSec * 1000;

		u8CurTime = kal_div64_u64(kalGetBootTime(),
					USEC_PER_MSEC);

		/* It is impossible that u4EndMsec is less than u8CurTime */
		u8EndMsec = u8EndMsec - u8CurTime +
			    20; /* the timeout duration at least 2 jiffies */
		cnmTimerStartTimer(prAdapter, &prWmmInfo->rAcmDeqTimer,
				   (uint32_t)u8EndMsec);
		DBGLOG(WMM, DEBUG,
		       "AC %d, will start next deq interval after %lu ms\n",
		       ucAc, u8EndMsec);
	}
	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
 * \brief For TX direct, check whether can TX now, or pending until timeout
 *
 * \param[in] prAdapter
 * \param[in] prBssInfo
 * \param[in] prStaRec
 * \param[in] ucAc: AC category
 * \param[in] u2PktLen: packet length
 *
 * \return TRUE for TX, FALSE for pending
 */
/*----------------------------------------------------------------------------*/
u_int8_t wmmAcmCanTx(struct ADAPTER *prAdapter,
		struct BSS_INFO *prBssInfo, struct STA_RECORD *prStaRec,
		uint8_t ucAc, uint16_t u2PktLen)
{
	if (!prAdapter || !prBssInfo || !prStaRec)
		return TRUE;

	if (unlikely(prStaRec->afgAcmRequired[ucAc])) {
		uint32_t u4PktTxTime = 0;

		DBGLOG(WMM, TRACE, "AC %d Pending Pkts %u\n",
				ucAc, u2PktLen);

		u4PktTxTime = wmmCalculatePktUsedTime(
				prBssInfo, prStaRec,
				u2PktLen - ETH_HLEN);
		return wmmAcmCanDequeue(prAdapter, ucAc,
					u4PktTxTime,
					prBssInfo->ucBssIndex);
	} else {
		return TRUE;
	}
}


static uint16_t wmmAcmTxTimePLCPCal(uint16_t u2Length, uint16_t u2Rate,
				    uint8_t FlgIsGmode)
{
	uint16_t u2PLCP;

	if (FlgIsGmode) {
		u2Rate <<= 1;
		/* EX1: BodyLen = 30B and rate = 54Mbps,
		 * 1. additional 22 bit in PSDU
		 * PLCP = 30*8 + 22 = 262 bits
		 * 2. round_up{X / 4} * 4 means OFDM symbol is in unit of 4
		 * usec
		 * PLCP = (262/54) = 4.8xxx us
		 * 4.8xxx / 4 = 1.2xxx
		 * 3. PLCP = round up(1.2xxx) * 4 = 2 * 4 = 8 us
		 * EX2: BodyLen = 14B and rate = 6Mbps,
		 * 1. additional 22 bit in PSDU
		 * PLCP = 14*8 + 22 = 134 bits
		 * 2. round_up{X / 4} * 4 means OFDM symbol is in unit of 4
		 * usec
		 * PLCP = (134/6) = 22.3xxx us
		 * 22.3xxx / 4 = 5.583xxx
		 * 3. PLCP = round up(5.583xxx) * 4 = 6 * 4 = 24 us
		 */
		u2PLCP = (u2Length << 3) + 22; /* need to add 22 bits in 11g */
		u2PLCP = (u2PLCP % u2Rate) ? (u2PLCP / u2Rate) + 1
					   : (u2PLCP / u2Rate);
		return u2PLCP << 2;
	}

	/* ex: BodyLen = 30B and rate = 11Mbps, PLCP = 30 * 8 / 11 = 22us */
	return (u2Length << 4) / u2Rate;
}

/* For G mode, no long or short preamble time, only long (20us) or short slot
 ** time (9us).
 */
static uint16_t wmmAcmTxTimeCal(uint16_t u2SecExtra, uint16_t u2EthBodyLen,
				uint16_t u2DataRate, uint16_t u2BasicRate,
				uint8_t ucFlags)
{
	uint16_t u2TxTime = 0;
	uint16_t u2PreambleTime = 0;
	u_int8_t fgIsGMode = !!(ucFlags & FLAG_G_MODE);

	u2PreambleTime =
		LMR_PREAMBL_TIME(fgIsGMode, !!(ucFlags & FLAG_S_PREAMBLE));

	/* CTS-self */
	if (ucFlags & FLAG_CTS_SELF) {
		u2TxTime = u2PreambleTime + TIME_SIFSG;
		u2TxTime += wmmAcmTxTimePLCPCal(FRM_LENGTH_ACK, u2BasicRate,
						fgIsGMode);
	} else if (ucFlags & FLAG_RTS_CTS) { /* CTS + RTS */
		u2TxTime = 2 * u2PreambleTime +
			   (fgIsGMode ? TIME_SIFSGx2 : TIME_SIFSx2);
		u2TxTime += wmmAcmTxTimePLCPCal(FRM_LENGTH_RTS + FRM_LENGTH_ACK,
						u2BasicRate, fgIsGMode);
	}
	/* Data Pkt Preamble Time + RTS/CTS time + 802.11 QoS hdr + LLC header +
	 ** Ethernet Payload + sec extra + FCS
	 */
	u2TxTime += u2PreambleTime +
		    wmmAcmTxTimePLCPCal(WLAN_MAC_HEADER_QOS_LEN + 8 +
						u2EthBodyLen + u2SecExtra + 4,
					u2DataRate, fgIsGMode);
	/* Ack frame for data packet. Preamble + Ack + SIFS */
	u2TxTime +=
		wmmAcmTxTimePLCPCal(FRM_LENGTH_ACK, u2BasicRate, fgIsGMode) +
		(fgIsGMode ? TIME_SIFSG : TIME_SIFS) + u2PreambleTime;
	return u2TxTime;
}

/* Reference to Draft802.11n_D3.07, Transmission Time =
 ** 1. Mix mode, short GI
 **	TXTIME =	T_LEG_PREAMBLE + T_L_SIG + T_HT_PREAMBLE + T_HT_SIG +
 **				T_SYM * Ceiling{T_SYMS * N_SYM / T_SYM}
 ** 2. Mix mode, regular GI
 **	TXTIME =	T_LEG_PREAMBLE + T_L_SIG + T_HT_PREAMBLE + T_HT_SIG +
 **				T_SYM * N_SYM
 ** 3. GreenField mode, short GI
 **	TXTIME =	T_GF_HT_PREAMBLE + T_HT_SIG + T_SYMS * N_SYM
 ** 4. GreenField mode, regular GI
 **	TXTIME =	T_GF_HT_PREAMBLE + T_HT_SIG + T_SYM * N_SYM
 ** Where
 ** (1) T_LEG_PREAMBLE	= T_L_STF + T_L_LTF = 8 + 8 = 16 us
 ** (2) T_L_SIG			= 4 us
 ** (3) T_HT_PREAMBLE	= T_HT_STF + T_HT_LTF1 + (N_LTF - 1) * T_HT_LTFS
 **					= 4 + 4 + ((N_DLTF + N_ELTF) - 1) * 4
 **	EX: Nss = 2, N_DLTF = 2, Ness = 0, N_ELTF = 0, T_HT_PREAMBLE = 12 us
 ** (4) T_HT_SIG		= 8 us
 ** (5) T_SYM			= 4 us
 ** (6) T_SYMS			= 3.6 us
 ** (7) N_SYM			= mSTBC * Ceil((8*len+16+6*N_ES)/(mSTBC *
 *N_DBPS))
 ** (8) T_GF_HT_PREAMBLE= T_HT_GF_STF + T_HT_LTF1 + (N_LTF - 1) * T_HT_LTFS
 **					= 8 + 4 + ((N_DLTF + N_ELTF) - 1) * 4
 */
static uint16_t wmmAcmTxTimeHtPLCPCal(uint16_t u2Length, uint8_t ucMcsId,
				      uint8_t ucNess, uint8_t ucFlags)
{
	uint32_t T_LEG_PREAMBLE, T_L_SIG, T_HT_PREAMBLE, T_HT_SIG;
	uint32_t T_SYM, T_SYMS, N_SYM;
	uint32_t T_GF_HT_PREAMBLE;
	uint32_t TxTime;
	uint32_t N_DLTF[5] = {1, 1, 2, 4, 4};
	uint32_t N_ELTF[4] = {0, 1, 2, 4};
	uint32_t N_SYM_1_NUM; /* numerator of N_SYM */
	uint8_t ucBwId = (ucFlags & FLAG_40M_BW) ? 1 : 0;
	uint8_t ucNss = 0;
	static const uint16_t gAcmRateNdbps[2][32] = {
		/* MCS0 ~ MCS31 */
		/* 20MHz */
		{26,  52,  78,  104, 156, 208, 234, 260, 52,  104, 156,
		 208, 312, 416, 468, 520, 78,  156, 234, 312, 468, 624,
		 702, 780, 104, 208, 312, 416, 624, 832, 936, 1040},

		/* MCS0 ~ MCS31 */
		/* 40MHz */
		{54,   108,  162, 216, 324,  432, 486,  540,  108,  216, 324,
		 432,  648,  864, 972, 1080, 162, 324,  486,  648,  972, 1296,
		 1458, 1620, 216, 432, 648,  864, 1296, 1728, 1944, 2160},
	};

	/* init */
	T_LEG_PREAMBLE = 16;
	T_L_SIG = 4;
	T_HT_SIG = 8;
	T_SYM = 4;
	T_SYMS = 36; /* unit: 0.1us */
	TxTime = 0;

	if (ucMcsId < 8)
		ucNss = 1;
	else if (ucMcsId < 16)
		ucNss = 2;
	else if (ucMcsId < 24)
		ucNss = 3;
	else if (ucMcsId < 32)
		ucNss = 4;
	else {
		ucMcsId = 31;
		ucNss = 1;
	}
	/* calculate N_SYM */
	/* ex: 1538B, 1st MPDU of AMPDU, (1538 * 8 + 22)/1080 + 1 = 12 */
	/* STBC is not used, BCC is used */
	N_SYM = ((u2Length << 3) + 16 + 6) / gAcmRateNdbps[ucBwId][ucMcsId] + 1;

	/* calculate transmission time */
	if (!(ucFlags & FLAG_GF_HT)) {
		/* ex: 1538B, 1st MPDU of AMPDU, 4 + 4 + 2*4 = 16us */
		T_HT_PREAMBLE =
			4 + 4 + ((N_DLTF[ucNss] + N_ELTF[ucNess] - 1) << 2);

		/* ex: 1538B, 1st MPDU of AMPDU, 16 + 4 + 16 + 8 = 44us */
		if (!(ucFlags & FLAG_ONLY_DATA))
			TxTime = T_LEG_PREAMBLE + T_L_SIG + T_HT_PREAMBLE +
				 T_HT_SIG;

		/* End of if */

		/* ex: 1538B, 1st MPDU of AMPDU, 4 * 12 = 48us */
		if (!(ucFlags & FLAG_SHORT_GI))
			TxTime += T_SYM * N_SYM;
		else {
			N_SYM_1_NUM = (T_SYMS * N_SYM) / (T_SYM * 10);

			if ((T_SYMS * N_SYM) % (T_SYM * 10))
				N_SYM_1_NUM++;

			TxTime += T_SYM * N_SYM_1_NUM;
		} /* End of if */
	} else {
		T_GF_HT_PREAMBLE =
			8 + 4 + ((N_DLTF[ucNss] + N_ELTF[ucNess] - 1) << 2);

		if (!(ucFlags & FLAG_ONLY_DATA))
			TxTime = T_GF_HT_PREAMBLE + T_HT_SIG;

		TxTime += (ucFlags & FLAG_SHORT_GI) ? ((T_SYMS * N_SYM) / 10)
						    : (T_SYM * N_SYM);
	} /* End of if */

	return TxTime;
} /* End of ACM_TX_TimePlcpCalHT */

static uint16_t wmmAcmTxTimeHtCal(uint16_t u2SecExtra, uint16_t u2EthBodyLen,
				  uint8_t ucMcsId, uint8_t ucFlags)
{
	uint16_t u2PreambleTime = 0;
	uint16_t u2TxTime = 0;

	u2PreambleTime = LMR_PREAMBL_TIME(TRUE, !!(ucFlags & FLAG_S_PREAMBLE));
	if (ucFlags & FLAG_RTS_CTS) {
		/* add RTS/CTS 24Mbps time */
		u2TxTime += 2 * u2PreambleTime + TIME_SIFSGx2;
		u2TxTime += wmmAcmTxTimePLCPCal(FRM_LENGTH_RTS + FRM_LENGTH_ACK,
						RATE_24M, TRUE);
	}
	/* SIFS + ACK, always use G mode to calculate preamble */

	u2TxTime += TIME_SIFSG + u2PreambleTime;
	/* always use block ack to calculate ack time */
	u2TxTime += wmmAcmTxTimePLCPCal(FRM_LENGTH_BLOCK_ACK, RATE_24M, TRUE);

	/* Data Pkt Preamble Time + RTS/CTS time + 802.11 QoS hdr + LLC header +
	 ** Ethernet Payload + sec extra + FCS
	 ** Nss always set to 1 due to only
	 */
	u2TxTime += wmmAcmTxTimeHtPLCPCal(WLAN_MAC_HEADER_QOS_LEN + 8 +
						  u2EthBodyLen + u2SecExtra + 4,
					  ucMcsId, 0, ucFlags);

	return u2TxTime;
} /* End of ACM_TX_TimeCalHT */

static void wmmAcmDequeueTimeOut(struct ADAPTER *prAdapter,
				 uintptr_t ulParamPtr)
{
	DBGLOG(WMM, DEBUG, "Timeout, trigger to do ACM dequeue\n");
	kalSetEvent(prAdapter->prGlueInfo);

	/* for TX direct, continue TX */
	if (HAL_IS_TX_DIRECT(prAdapter))
		nicTxDirectStartCheckQTimer(prAdapter);
}
#endif
