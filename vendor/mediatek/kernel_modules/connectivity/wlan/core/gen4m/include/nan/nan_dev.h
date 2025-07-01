/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __NAN_DEV_H__
#define __NAN_DEV_H__

#if CFG_SUPPORT_NAN

#define NAN_DEFAULT_INDEX (0)

enum ENUM_MODULE {
	ENUM_NAN_DATA_MODULE,
	ENUM_NAN_RANGE_MODULE,
	ENUM_NAN_MODULE_NUM
};

/*******************************************************************************
 *                              F U N C T I O N S
 *******************************************************************************
 */
void nanResetMemory(void);

uint8_t nanDevInit(struct ADAPTER *prAdapter, uint8_t ucIdx);

void nanDevFsmUninit(struct ADAPTER *prAdapter, uint8_t ucIdx);
struct _NAN_SPECIFIC_BSS_INFO_T *
nanGetSpecificBssInfo(struct ADAPTER *prAdapter,
		      uint8_t eIndex);
uint8_t
nanGetBssIdxbyBand(struct ADAPTER *prAdapter,
		      enum ENUM_BAND eBand);

void nanDevSetMasterPreference(struct ADAPTER *prAdapter,
			       uint8_t ucMasterPreference);

enum NanStatusType nanDevEnableRequest(struct ADAPTER *prAdapter,
				       struct NanEnableRequest *prEnableReq);
enum NanStatusType nanDevDisableRequest(struct ADAPTER *prAdapter);
void nanDevMasterIndEvtHandler(struct ADAPTER *prAdapter,
			       uint8_t *pcuEvtBuf);
uint32_t nanDevGetMasterIndAttr(struct ADAPTER *prAdapter,
				uint8_t *pucMasterIndAttrBuf,
				uint32_t *pu4MasterIndAttrLength);
void nanDevClusterIdEvtHandler(struct ADAPTER *prAdapter,
		uint8_t *pcuEvtBuf);
uint32_t nanDevGetClusterId(struct ADAPTER *prAdapter,
		uint8_t *pucClusterId);
uint32_t nanDevSendEnableRequestToCnm(struct ADAPTER *prAdapter);
uint32_t nanDevSendAbortRequestToCnm(struct ADAPTER *prAdapter);
void nanDevSendEnableRequest(struct ADAPTER *prAdapter,
				struct MSG_HDR *prMsgHdr);
void nanDevSetDWInterval(struct ADAPTER *prAdapter, uint8_t ucDWInterval);

uint32_t
nanDevGetDeviceInfo(struct ADAPTER *prAdapter,
		void *pvQueryBuffer, uint32_t u4QueryBufferLen,
		uint32_t *pu4QueryInfoLen);
void nanDevEventQueryDeviceInfo(struct ADAPTER *prAdapter,
		struct CMD_INFO *prCmdInfo,
		uint8_t *pucEventBuf);

u_int8_t nanIsOn(struct ADAPTER *prAdapter);
uint8_t nanIsEhtSupport(struct ADAPTER *prAdapter);
uint8_t nanIsEhtEnable(struct ADAPTER *prAdapter);

void nanConcurrencyHandler(struct ADAPTER *prAdapter);
void nanBackToNormal(struct ADAPTER *prAdapter);

void nanBackupSapChannel(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prBssInfo);
void nanRestoreSapChannel(
	struct ADAPTER *prAdapter);
u_int8_t nanTrySwitchSapChannel(
	struct ADAPTER *prAdapter);
uint8_t nanGetSapCsaChannel(
	struct ADAPTER *prAdapter,
	struct BSS_INFO *prP2pBssInfo,
	enum ENUM_BAND *eRfBand,
	uint8_t *ucCh);

/*========================= FUNCTIONs ============================*/
#endif
#endif /* __NAN_DEV_H__ */
