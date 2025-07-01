// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#include "precomp.h"

/*******************************************************************************
 *                                 CCM Related
 *******************************************************************************
 */
#if CFG_SUPPORT_CCM
struct P2P_CCM_CSA_ENTRY {
	struct LINK_ENTRY rLinkEntry;
	struct BSS_INFO *prBssInfo;
	uint32_t u4TargetCh;
	enum ENUM_MBMC_BN eTargetHwBandIdx;
	enum ENUM_BAND eTargetBand;
};

struct CCM_STABLE_CB_ENTRY {
	struct LINK_ENTRY rLinkEntry;
	CCM_CALLBACK_FUNC pfCcmStableCb;
};

void ccmInit(struct ADAPTER *prAdapter)
{
	LINK_INITIALIZE(&prAdapter->rCcmCheckCsList);
	LINK_INITIALIZE(&prAdapter->rCcmStableCbList);
	prAdapter->ucCcmSwitchingCnt = 0;
}

void ccmUninit(struct ADAPTER *prAdapter)
{
	struct LINK *prCcmStableCbList = &prAdapter->rCcmStableCbList;
	struct CCM_STABLE_CB_ENTRY *prCcmCbEntry;
	struct CCM_STABLE_CB_ENTRY *prCcmCbEntryNext;

	if (!LINK_IS_EMPTY(prCcmStableCbList)) {
		LINK_FOR_EACH_ENTRY_SAFE(prCcmCbEntry, prCcmCbEntryNext,
				 prCcmStableCbList, rLinkEntry,
				 struct CCM_STABLE_CB_ENTRY) {
			LINK_REMOVE_KNOWN_ENTRY(prCcmStableCbList,
						prCcmCbEntry);
			cnmMemFree(prAdapter, prCcmCbEntry);
		}
	}
}

void ccmRegisterStableCb(struct ADAPTER *prAdapter,
			 CCM_CALLBACK_FUNC func)
{
	struct LINK *prCcmStableCbList = &prAdapter->rCcmStableCbList;
	struct CCM_STABLE_CB_ENTRY *prCcmCbEntry;

	LINK_FOR_EACH_ENTRY(prCcmCbEntry, prCcmStableCbList, rLinkEntry,
			    struct CCM_STABLE_CB_ENTRY) {
		if (prCcmCbEntry->pfCcmStableCb == func)
			return;
	}

	prCcmCbEntry = cnmMemAlloc(prAdapter,
		RAM_TYPE_MSG, sizeof(struct CCM_STABLE_CB_ENTRY));
	prCcmCbEntry->pfCcmStableCb = func;
	LINK_INSERT_TAIL(prCcmStableCbList, &prCcmCbEntry->rLinkEntry);
	DBGLOG(CCM, TRACE, "Register callback func=%ps[%u]\n", func,
	       prCcmStableCbList->u4NumElem);
}

void ccmUnregisterStableCb(struct ADAPTER *prAdapter,
			   CCM_CALLBACK_FUNC func)
{
	struct LINK *prCcmStableCbList = &prAdapter->rCcmStableCbList;
	struct CCM_STABLE_CB_ENTRY *prCcmCbEntry;
	struct CCM_STABLE_CB_ENTRY *prCcmCbEntryNext;

	if (LINK_IS_EMPTY(prCcmStableCbList))
		return;

	LINK_FOR_EACH_ENTRY_SAFE(prCcmCbEntry, prCcmCbEntryNext,
				 prCcmStableCbList, rLinkEntry,
				 struct CCM_STABLE_CB_ENTRY) {
		if (prCcmCbEntry->pfCcmStableCb == func) {
			LINK_REMOVE_KNOWN_ENTRY(prCcmStableCbList,
						prCcmCbEntry);
			cnmMemFree(prAdapter, prCcmCbEntry);
			DBGLOG(CCM, TRACE, "Unregister func=%ps[%u]\n", func,
			       prCcmStableCbList->u4NumElem);
			break;
		}
	}
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Prevent to access the old connection info.
 */
/*----------------------------------------------------------------------------*/
void ccmRemoveBssPendingEntry(struct ADAPTER *prAdapter,
			      struct BSS_INFO *prBssInfo)
{
	struct LINK *prCcmCheckCsList = &prAdapter->rCcmCheckCsList;
	struct P2P_CCM_CSA_ENTRY *prCcmCsaEntry;
	struct P2P_CCM_CSA_ENTRY *prCcmCsaEntryNext;

	LINK_FOR_EACH_ENTRY_SAFE(prCcmCsaEntry, prCcmCsaEntryNext,
				 prCcmCheckCsList, rLinkEntry,
				 struct P2P_CCM_CSA_ENTRY) {
		if (prCcmCsaEntry->prBssInfo != prBssInfo)
			continue;
		DBGLOG(CCM, INFO, "bss=%u free, remove pending entry\n",
		       prBssInfo->ucBssIndex);
		LINK_REMOVE_KNOWN_ENTRY(prCcmCheckCsList, prCcmCsaEntry);
		cnmMemFree(prAdapter, prCcmCsaEntry);
	}
}

static void ccmPendingEventTimeout(struct ADAPTER *prAdapter,
				   uintptr_t ulParamPtr)
{
	struct BSS_INFO *targetBss = (struct BSS_INFO *) ulParamPtr;

	prAdapter->fgIsCcmPending = FALSE;
	ccmChannelSwitchProducer(prAdapter, targetBss, __func__);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Check if channel switch allowed and prepare to switchh to A+A channel
 *        or original input target channel.
 *
 * \param[in] u4TargetCh  Point to target channel we want to switch to,
 *                        may be modified for A+A.
 *            eTargetBand Point to target band we want to switch to,
 *                        may be modified for A+A.
 *
 * \return TRUE, ready to channel switch.
 *         FALSE, not alloed to channel switch.
 */
/*----------------------------------------------------------------------------*/
static u_int8_t ccmCheckAndPrepareChannelSwitch(struct ADAPTER *prAdapter,
			    struct BSS_INFO *prBssInfo,
			    uint32_t *u4TargetCh,
			    enum ENUM_MBMC_BN eTargetHwBandIdx,
			    enum ENUM_BAND *eTargetBand)
{
#if (CFG_SUPPORT_WIFI_6G == 1)
	uint32_t freqList[MAX_5G_BAND_CHN_NUM + MAX_6G_BAND_CHN_NUM] = {};
#else
	uint32_t freqList[MAX_5G_BAND_CHN_NUM] = {};
#endif
	uint32_t u4FreqListNum;
	uint32_t u4Idx;
	uint32_t u4ChForAa;
	enum ENUM_BAND eBandForAa = BAND_NULL;
	uint32_t u4Freq;

	/* pass for MCC only */
	if (prBssInfo->eHwBandIdx != eTargetHwBandIdx ||
	    prBssInfo->ucPrimaryChannel == *u4TargetCh) {
		DBGLOG(CCM, INFO,
		       "do not trigger CSA, [%s]Bss%u, ch=%u, hwBand=%u, rfBand=%u [Target]ch=%u, hwBand=%u, rfBand=%u\n",
		       bssGetRoleTypeString(prAdapter, prBssInfo),
		       prBssInfo->ucBssIndex, prBssInfo->ucPrimaryChannel,
		       prBssInfo->eHwBandIdx, prBssInfo->eBand,
		       *u4TargetCh, eTargetHwBandIdx, *eTargetBand);
		return FALSE;
	}

	if ((prBssInfo->eBand == BAND_5G
#if (CFG_SUPPORT_WIFI_6G == 1)
	     || prBssInfo->eBand == BAND_6G
#endif
	     ) &&
	    p2pFuncIsPreferWfdAa(prAdapter, prBssInfo)) {
		u4FreqListNum = p2pFuncAppendAaFreq(prAdapter, prBssInfo,
						     freqList);
		for (u4Idx = 0; u4Idx < u4FreqListNum; u4Idx++) {
			u4Freq = freqList[u4Idx];
			u4ChForAa = nicFreq2ChannelNum(u4Freq * 1000);
			if (u4Freq >= 2412 && u4Freq <= 2484)
				eBandForAa = BAND_2G4;
			else if (u4Freq >= 5180 && u4Freq <= 5900)
				eBandForAa = BAND_5G;
#if (CFG_SUPPORT_WIFI_6G == 1)
			else if (u4Freq >= 5955 && u4Freq <= 7115)
				eBandForAa = BAND_6G;
#endif
			if (p2pFuncIsCsaAllowed(prAdapter, prBssInfo,
				u4ChForAa, eBandForAa) == CSA_STATUS_SUCCESS) {
				/* modify the input ch and band! */
				*u4TargetCh = u4ChForAa;
				*eTargetBand = eBandForAa;
				return TRUE;
			}
		}
	} else if (p2pFuncIsCsaAllowed(prAdapter, prBssInfo, *u4TargetCh,
				 *eTargetBand) == CSA_STATUS_SUCCESS)
		return TRUE;

	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Check if CCM need to pending until ch grant timeout.
 *        Only pending when a GO want to CSA.
 *
 * \param[in] pvAdapter Pointer to the adapter descriptor.
 *            u4TargetCh Target BSS channel.
 *            eTargetHwBandIdx Target BSS HW band.
 *            eTargetBand Target BSS RF band.
 *
 * \return status
 */
/*----------------------------------------------------------------------------*/
void ccmPendingCheck(struct ADAPTER *prAdapter,
		     struct BSS_INFO *prTargetBss,
		     uint32_t u4GrantInterval)
{
	uint8_t i;
	struct BSS_INFO *bss;
	uint32_t u4TargetCh;
	enum ENUM_BAND eTargetBand;
	u_int8_t fgIsTargetMlo = FALSE;
#if (CFG_SUPPORT_802_11BE_MLO == 1)
	struct MLD_BSS_INFO *prMldBss = mldBssGetByBss(prAdapter, prTargetBss);

	fgIsTargetMlo = IS_MLD_BSSINFO_MULTI(prMldBss);
#endif

	if (prAdapter->fgIsCcmPending) {
		DBGLOG(CCM, WARN,
		       "skip pending check due to previous check not done\n");
		return;
	}
	prAdapter->fgIsCcmPending = FALSE;

	for (i = 0; i < MAX_BSSID_NUM && !prAdapter->fgIsCcmPending; ++i) {
		bss = GET_BSS_INFO_BY_INDEX(prAdapter, i);

		/* skip target bss itself */
		if (!fgIsTargetMlo && bss == prTargetBss)
			continue;
#if (CFG_SUPPORT_802_11BE_MLO == 1) || defined(CFG_SUPPORT_UNIFIED_COMMAND)
		else if (bss->ucGroupMldId == prTargetBss->ucGroupMldId)
			continue;
#endif

		/* Only check GO, because only GO has NoA */
		if (!IS_BSS_ALIVE(prAdapter, bss) || !IS_BSS_GO(prAdapter, bss))
			continue;

		/* Must copy, because
		 * ccmCheckAndPrepareChannelSwitch may modify it.
		 */
		u4TargetCh = prTargetBss->ucPrimaryChannel;
		eTargetBand = prTargetBss->eBand;

#if (CFG_SUPPORT_802_11BE_MLO == 1)
		/* MLO GC/STA only ch req once */
		if (IS_BSS_GC(prTargetBss) || IS_BSS_AIS(prTargetBss)) {
			struct BSS_INFO *mldTargetbss;

			if (prMldBss) {
				LINK_FOR_EACH_ENTRY(mldTargetbss,
						    &prMldBss->rBssList,
						    rLinkEntryMld,
						    struct BSS_INFO) {
					u4TargetCh =
						mldTargetbss->ucPrimaryChannel;
					eTargetBand = mldTargetbss->eBand;

					DBGLOG(CCM, INFO,
					       "checking [%s]bss=%u ch=%u, hwBand=%u, rfBand=%u [Target]ch=%u, hwBand=%u, rfBand=%u\n",
					       bssGetRoleTypeString(prAdapter,
								    bss),
					       bss->ucBssIndex,
					       bss->ucPrimaryChannel,
					       bss->eHwBandIdx, bss->eBand,
					       u4TargetCh,
					       mldTargetbss->eHwBandIdx,
					       eTargetBand);

					if (!ccmCheckAndPrepareChannelSwitch(
						prAdapter, bss, &u4TargetCh,
						mldTargetbss->eHwBandIdx,
						&eTargetBand))
						continue;

					prAdapter->fgIsCcmPending = TRUE;
					break;
				}
			} else if (ccmCheckAndPrepareChannelSwitch(prAdapter,
					bss, &u4TargetCh,
					prTargetBss->eHwBandIdx, &eTargetBand))
				prAdapter->fgIsCcmPending = TRUE;
		} else if (IS_BSS_APGO(prTargetBss))
#endif /* CFG_SUPPORT_802_11BE_MLO == 1 */
			if (ccmCheckAndPrepareChannelSwitch(prAdapter, bss,
					 &u4TargetCh, prTargetBss->eHwBandIdx,
					 &eTargetBand))
				prAdapter->fgIsCcmPending = TRUE;
	}

	if (prAdapter->fgIsCcmPending) {
		/* New connect ch req will make GO NoA for 4~5s,
		 * which may cause GC absent and not receive beacon.
		 */
		DBGLOG(CCM, INFO,
		       "pending CCM for %ums because a GO wants to CSA\n",
		       u4GrantInterval);
		cnmTimerInitTimer(prAdapter, &prAdapter->rCcmPendingTimer,
			(PFN_MGMT_TIMEOUT_FUNC) ccmPendingEventTimeout,
			(uintptr_t) prTargetBss);
		cnmTimerStartTimer(prAdapter, &prAdapter->rCcmPendingTimer,
				   u4GrantInterval);
	}
}

static void ccmGetOtherAliveBssHwBitmap(struct ADAPTER *prAdapter,
				 uint32_t *pau4Bitmap,
				 struct BSS_INFO *prBssInfo)
{
	struct BSS_INFO *bss;
	uint8_t i;

	for (i = 0; i < MAX_BSSID_NUM; ++i) {
		bss = GET_BSS_INFO_BY_INDEX(prAdapter, i);

		if (!IS_BSS_ALIVE(prAdapter, bss) || bss == prBssInfo ||
		    bss->eHwBandIdx >= AA_HW_BAND_NUM)
			continue;

		pau4Bitmap[bss->eHwBandIdx] |= BIT(i);
	}
	DBGLOG(CCM, TRACE,
	       "alive BSS hw bitmap [bn0:bn1:bn2]=[0x%x:0x%x:0x%x]\n",
	       pau4Bitmap[AA_HW_BAND_0], pau4Bitmap[AA_HW_BAND_1],
	       pau4Bitmap[AA_HW_BAND_2]);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Produce the BSS CS check event, and consumer will check whether it
 *        needs to CS to SCC with prTargetBss.
 *
 * \param[in] pvAdapter Pointer to the adapter descriptor.
 * \param[in] prTargetBss The target BSS, MLO should call twice.
 * \param[in] pucSrcFunc Source function that trigger the CSA.
 *
 * \return status
 */
/*----------------------------------------------------------------------------*/
static void __ccmChannelSwitchProducer(struct ADAPTER *prAdapter,
				struct BSS_INFO *prTargetBss,
				const char *pucSrcFunc)
{
	struct BSS_INFO *bss;
	uint8_t i;
	struct LINK *prCcmCheckCsList = &prAdapter->rCcmCheckCsList;
	u_int8_t fgIsTargetMlo = FALSE;
	struct P2P_CCM_CSA_ENTRY *prCcmCsaEntry;
	struct P2P_CCM_CSA_ENTRY *prFirstSap = NULL;
	struct WIFI_VAR *prWifiVar = &prAdapter->rWifiVar;

#if (CFG_SUPPORT_802_11BE_MLO == 1)
	fgIsTargetMlo = IS_MLD_BSSINFO_MULTI(
				mldBssGetByBss(prAdapter, prTargetBss));
#endif

	DBGLOG(CCM, INFO,
	       "%s: bss=%u, role=%s, is_mlo=%u notify other GO/SAP to CSA\n",
	       pucSrcFunc, prTargetBss->ucBssIndex,
	       bssGetRoleTypeString(prAdapter, prTargetBss), fgIsTargetMlo);

	/* find the first SAP entry in list */
	LINK_FOR_EACH_ENTRY(prCcmCsaEntry, prCcmCheckCsList, rLinkEntry,
			    struct P2P_CCM_CSA_ENTRY) {
		if (IS_BSS_AP(prAdapter, prCcmCsaEntry->prBssInfo)) {
			prFirstSap = prCcmCsaEntry;
			DBGLOG(CCM, TRACE, "first SAP in list is bss=%u\n",
			       prFirstSap->prBssInfo->ucBssIndex);
			break;
		}
	}

	/* enqueue GO before the first SAP first */
	if (prWifiVar->eP2pCcmMode != P2P_CCM_MODE_DISABLE) {
		for (i = 0; i < MAX_BSSID_NUM; ++i) {
			bss = GET_BSS_INFO_BY_INDEX(prAdapter, i);

			if (!IS_BSS_ALIVE(prAdapter, bss) ||
			    !IS_BSS_GO(prAdapter, bss))
				continue;

			/* skip target bss itself */
			if (!fgIsTargetMlo && bss == prTargetBss)
				continue;
#if (CFG_SUPPORT_802_11BE_MLO == 1) || defined(CFG_SUPPORT_UNIFIED_COMMAND)
			else if (bss->ucGroupMldId == prTargetBss->ucGroupMldId)
				continue;
#endif

			prCcmCsaEntry = cnmMemAlloc(prAdapter,
				RAM_TYPE_MSG, sizeof(struct P2P_CCM_CSA_ENTRY));
			if (!prCcmCsaEntry) {
				DBGLOG(CCM, ERROR, "Alloc mem fail\n");
				return;
			}

			prCcmCsaEntry->prBssInfo = bss;
			prCcmCsaEntry->u4TargetCh =
				prTargetBss->ucPrimaryChannel;
			prCcmCsaEntry->eTargetHwBandIdx =
				prTargetBss->eHwBandIdx;
			prCcmCsaEntry->eTargetBand = prTargetBss->eBand;

			if (prFirstSap) {
				LINK_INSERT_BEFORE(prCcmCheckCsList,
						   &prFirstSap->rLinkEntry,
						   &prCcmCsaEntry->rLinkEntry);
			} else {
				LINK_INSERT_TAIL(prCcmCheckCsList,
						 &prCcmCsaEntry->rLinkEntry);
			}

			DBGLOG(CCM, TRACE,
			       "insert GO bss=%u waiting to check\n",
			       bss->ucBssIndex);
		}
	} else {
		/* we should still notify SAP even if P2P CCM is disabled */
		DBGLOG(CCM, WARN, "CCM is disabled, skip to ckeck GO\n");
	}

	/* SAP must do CSA last, enqueue SAP at the end */
	for (i = 0; i < MAX_BSSID_NUM; ++i) {
		bss = GET_BSS_INFO_BY_INDEX(prAdapter, i);

		if (!IS_BSS_ALIVE(prAdapter, bss) || !IS_BSS_AP(prAdapter, bss))
			continue;

		/* skip target bss itself */
		if (!fgIsTargetMlo && bss == prTargetBss)
			continue;
#if (CFG_SUPPORT_802_11BE_MLO == 1) || defined(CFG_SUPPORT_UNIFIED_COMMAND)
		else if (bss->ucGroupMldId == prTargetBss->ucGroupMldId)
			continue;
#endif

		prCcmCsaEntry = cnmMemAlloc(prAdapter,
			RAM_TYPE_MSG, sizeof(struct P2P_CCM_CSA_ENTRY));
		if (!prCcmCsaEntry) {
			DBGLOG(CCM, ERROR, "Alloc mem fail\n");
			return;
		}

		prCcmCsaEntry->prBssInfo = bss;
		prCcmCsaEntry->u4TargetCh = prTargetBss->ucPrimaryChannel;
		prCcmCsaEntry->eTargetHwBandIdx = prTargetBss->eHwBandIdx;
		prCcmCsaEntry->eTargetBand = prTargetBss->eBand;

		LINK_INSERT_TAIL(prCcmCheckCsList,
				 &prCcmCsaEntry->rLinkEntry);

		DBGLOG(CCM, TRACE, "insert SAP bss=%u waiting to check\n",
		       bss->ucBssIndex);
	}

	ccmChannelSwitchConsumer(prAdapter);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Entry Point of CCM, other modules should call this.
 */
/*----------------------------------------------------------------------------*/
void ccmChannelSwitchProducer(struct ADAPTER *prAdapter,
			      struct BSS_INFO *prTargetBss,
			      const char *pucSrcFunc)
{
	if (!prAdapter->fgIsP2PRegistered)
		return;

	if (!prTargetBss) {
		DBGLOG(CCM, INFO, "null target Bss\n");
		return;
	}

	if (prAdapter->fgIsCcmPending) {
		DBGLOG(CCM, INFO, "skip CCM due to pending");
		return;
	}

#if (CFG_SUPPORT_802_11BE_MLO == 1)
	if (IS_BSS_GC(prTargetBss) || IS_BSS_AIS(prTargetBss)) {
		struct BSS_INFO *bss;
		struct MLD_BSS_INFO *prMldBss = mldBssGetByBss(prAdapter,
							       prTargetBss);

		if (prMldBss) {
			/* MLO GC/STA only ch abort once */
			LINK_FOR_EACH_ENTRY(bss, &prMldBss->rBssList,
					    rLinkEntryMld, struct BSS_INFO)
				__ccmChannelSwitchProducer(prAdapter, bss,
							   pucSrcFunc);
		} else
			__ccmChannelSwitchProducer(prAdapter, prTargetBss,
						   pucSrcFunc);
	} else if (IS_BSS_APGO(prTargetBss) || IS_BSS_NAN(prTargetBss))
#endif /* CFG_SUPPORT_802_11BE_MLO == 1 */
		__ccmChannelSwitchProducer(prAdapter, prTargetBss, pucSrcFunc);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Each time consume one entry in queue to check whether it needs to CS.
 *        Only trigger by __ccmChannelSwitchProducer,
 *        p2pRoleStateAbort_SWITCH_CHANNEL and itself.
 *
 * \param[in] pvAdapter Pointer to the adapter descriptor.
 *
 * \return status
 */
/*----------------------------------------------------------------------------*/
void ccmChannelSwitchConsumer(struct ADAPTER *prAdapter)
{
	struct LINK *prCcmCheckCsList = &prAdapter->rCcmCheckCsList;
	struct P2P_CCM_CSA_ENTRY *prCcmCsaEntry;
	struct BSS_INFO *bss;
	uint32_t u4TargetCh;
	enum ENUM_MBMC_BN eTargetHwBandIdx;
	enum ENUM_BAND eTargetBand;
	u_int8_t fgIsSwitching = FALSE;
	u_int8_t fgIsMlo = FALSE;
	int8_t i;

	if (p2pFuncIsSapGoCsa(prAdapter) ||
	    /* csa sta countdown till csadone */
	    prAdapter->rWifiVar.fgCsaInProgress) {
		DBGLOG(CCM, INFO, "skip due to CSA still in progress\n");
		return;
	} else {
		/* start switching channel till ch granted */
		for (i = 0; i < MAX_BSSID_NUM; ++i) {
			bss = GET_BSS_INFO_BY_INDEX(prAdapter, i);

			if (IS_BSS_P2P(bss) && bss->fgIsSwitchingChnl)
				break;
			else if (IS_BSS_AIS(bss) && bss->fgIsAisSwitchingChnl)
				break;
		}

		if (i < MAX_BSSID_NUM) {
			DBGLOG(CCM, INFO,
			       "skip due to Bss%u channel switch still in progress\n",
			       i);
			return;
		}
	}

	if (!LINK_IS_EMPTY(prCcmCheckCsList)) {
		LINK_REMOVE_HEAD(prCcmCheckCsList, prCcmCsaEntry,
			struct P2P_CCM_CSA_ENTRY *);
		bss = prCcmCsaEntry->prBssInfo;
		u4TargetCh = prCcmCsaEntry->u4TargetCh;
		eTargetHwBandIdx = prCcmCsaEntry->eTargetHwBandIdx;
		eTargetBand = prCcmCsaEntry->eTargetBand;

		if (!IS_BSS_APGO(bss))
			return;

		cnmMemFree(prAdapter, prCcmCsaEntry);
	} else {
		struct LINK *prCcmStableCbList = &prAdapter->rCcmStableCbList;
		struct CCM_STABLE_CB_ENTRY *prCcmCbEntry;

		/* CCM stable only means not trigger new CSA any more,
		 * but someone may still in CSA progress, which not means net is
		 * stable.
		 */
		DBGLOG(CCM, INFO,
		       "CCM is stable, switching cnt = %u, cb Num=%u\n",
		       prAdapter->ucCcmSwitchingCnt,
		       prCcmStableCbList->u4NumElem);

		/* This means net is stable */
		if (prAdapter->ucCcmSwitchingCnt == 0 &&
		    !LINK_IS_EMPTY(prCcmStableCbList)) {
			LINK_FOR_EACH_ENTRY(prCcmCbEntry, prCcmStableCbList,
					    rLinkEntry,
					    struct CCM_STABLE_CB_ENTRY) {
				DBGLOG(CCM, TRACE, "enter stable cb:%ps\n",
				       prCcmCbEntry->pfCcmStableCb);
				prCcmCbEntry->pfCcmStableCb(prAdapter);
			}
		}
		return;
	}

#if (CFG_SUPPORT_802_11BE_MLO == 1)
	fgIsMlo = IS_MLD_BSSINFO_MULTI(mldBssGetByBss(prAdapter, bss));
#endif

	DBGLOG(CCM, INFO,
	       "checking [%s]bss=%u ch=%u, hwBand=%u, rfBand=%u, is_mlo=%u, [Target]ch=%u, hwBand=%u, rfBand=%u\n",
	       bssGetRoleTypeString(prAdapter, bss),
	       bss->ucBssIndex, bss->ucPrimaryChannel,
	       bss->eHwBandIdx, bss->eBand, fgIsMlo,
	       u4TargetCh, eTargetHwBandIdx, eTargetBand);

	if (IS_BSS_AP(prAdapter, bss) && !fgIsMlo)
		fgIsSwitching = p2pFuncSwitchSapChannel(prAdapter,
					P2P_DEFAULT_SCENARIO);
	else if (ccmCheckAndPrepareChannelSwitch(prAdapter, bss, &u4TargetCh,
				      eTargetHwBandIdx, &eTargetBand)) {
		cnmIdcCsaReq(prAdapter, eTargetBand, u4TargetCh,
			     MODE_DISALLOW_TX, bss->u4PrivateData);
		fgIsSwitching = TRUE;
	}

	if (!fgIsSwitching)
		ccmChannelSwitchConsumer(prAdapter);
	else {
		prAdapter->ucCcmSwitchingCnt++;
		DBGLOG(CCM, INFO, "switching channel triggered, count=%u\n",
		       prAdapter->ucCcmSwitchingCnt);
	}
}


/*******************************************************************************
 *                                A + A Related
 *******************************************************************************
 */
uint32_t ccmAABwEnumToValue(uint8_t ucInputChnlBw)
{
	uint32_t u4OutputBw;

	if (ucInputChnlBw > MAX_BW_320_2MHZ)
		return 0;

	if (ucInputChnlBw == MAX_BW_320_1MHZ ||
		ucInputChnlBw == MAX_BW_320_2MHZ)
		u4OutputBw = 20 * BIT(4);
	else
		u4OutputBw = 20 * BIT(ucInputChnlBw);

	return u4OutputBw;
}

void ccmAAForbiddenRegionCal(struct ADAPTER *prAdapter,
			     struct RF_CHANNEL_INFO *rChnlInfo,
			     uint8_t *prForbiddenListLen,
			     uint16_t *prTargetBw,
			     struct CCM_AA_FOBIDEN_REGION_UNIT *arRegionOutput)
{
	uint32_t u4InputBw, u4TargetBw, u4RegionBw;
	uint32_t u4Freq = rChnlInfo->u4CenterFreq1;
	uint8_t  i;

	if (rChnlInfo->ucChnlBw == MAX_BW_320_1MHZ ||
	    rChnlInfo->ucChnlBw == MAX_BW_320_2MHZ)
		u4InputBw = 20 * BIT(4);
	else
		u4InputBw = 20 * BIT(rChnlInfo->ucChnlBw);

	DBGLOG(CCM, TRACE, "input: fc %u, bw %u, target: bw1 %u bw2 %u\n",
	       u4Freq, u4InputBw, prTargetBw[0], prTargetBw[1]);

	*prForbiddenListLen = 1;

	if (u4Freq <= P2P_5G_L_UPPER_BOUND) {
		u4TargetBw = prTargetBw[0];
		u4RegionBw = (u4InputBw + u4TargetBw) / 2;

		arRegionOutput[0].u4BoundForward1 =
			u4Freq + (4 * u4InputBw) + u4RegionBw;
		arRegionOutput[0].u4BoundIsolate =
			u4Freq + P2P_5G_L_H_ISOLATION_WIDTH + u4RegionBw;
		arRegionOutput[0].u4BoundForward2 =
			u4Freq + (4 * u4InputBw) - u4RegionBw;
		arRegionOutput[0].u4BoundInverse1 =
			u4Freq + (4 * u4TargetBw) + u4RegionBw;
		arRegionOutput[0].u4BoundInverse2 =
			u4Freq + (4 * u4TargetBw) - u4RegionBw;

		u4TargetBw = prTargetBw[1];
		if (u4TargetBw) {
			u4RegionBw = (u4InputBw + u4TargetBw) / 2;

			arRegionOutput[1].u4BoundForward1 =
				u4Freq + (4 * u4InputBw) + u4RegionBw;
			arRegionOutput[1].u4BoundIsolate =
				u4Freq + P2P_5G_6G_ISOLATION_WIDTH + u4RegionBw;
			arRegionOutput[1].u4BoundForward2 =
				u4Freq + (4 * u4InputBw) - u4RegionBw;
			arRegionOutput[1].u4BoundInverse1 =
				u4Freq + (4 * u4TargetBw) + u4RegionBw;
			arRegionOutput[1].u4BoundInverse2 =
				u4Freq + (4 * u4TargetBw) - u4RegionBw;

			(*prForbiddenListLen)++;
		}
	} else if (u4Freq <= P2P_5G_H_UPPER_BOUND) {
		u4TargetBw = prTargetBw[0];
		u4RegionBw = (u4InputBw + u4TargetBw) / 2;

		arRegionOutput[0].u4BoundForward1 =
			u4Freq - (4 * u4InputBw) + u4RegionBw;
		arRegionOutput[0].u4BoundForward2 =
			u4Freq - (4 * u4InputBw) - u4RegionBw;
		arRegionOutput[0].u4BoundIsolate =
			u4Freq - P2P_5G_L_H_ISOLATION_WIDTH - u4RegionBw;
		arRegionOutput[0].u4BoundInverse1 =
			u4Freq - (4 * u4TargetBw) + u4RegionBw;
		arRegionOutput[0].u4BoundInverse2 =
			u4Freq - (4 * u4TargetBw) - u4RegionBw;

		u4TargetBw = prTargetBw[1];
		if (u4TargetBw) {
			u4RegionBw = (u4InputBw + u4TargetBw) / 2;

			arRegionOutput[1].u4BoundForward1 =
				u4Freq + (4 * u4InputBw) + u4RegionBw;
			arRegionOutput[1].u4BoundIsolate =
				u4Freq + P2P_5G_6G_ISOLATION_WIDTH + u4RegionBw;
			arRegionOutput[1].u4BoundForward2 =
				u4Freq + (4 * u4InputBw) - u4RegionBw;
			arRegionOutput[1].u4BoundInverse1 =
				u4Freq + (4 * u4TargetBw) + u4RegionBw;
			arRegionOutput[1].u4BoundInverse2 =
				u4Freq + (4 * u4TargetBw) - u4RegionBw;

			(*prForbiddenListLen)++;
		}
	} else {
		u4TargetBw = prTargetBw[0];
		u4RegionBw = (u4InputBw + u4TargetBw) / 2;

		arRegionOutput[0].u4BoundForward1 =
			u4Freq - (4 * u4InputBw) + u4RegionBw;
		arRegionOutput[0].u4BoundForward2 =
			u4Freq - (4 * u4InputBw) - u4RegionBw;
		arRegionOutput[0].u4BoundIsolate =
			u4Freq - P2P_5G_6G_ISOLATION_WIDTH - u4RegionBw;
		arRegionOutput[0].u4BoundInverse1 =
			u4Freq - (4 * u4TargetBw) + u4RegionBw;
		arRegionOutput[0].u4BoundInverse2 =
			u4Freq - (4 * u4TargetBw) - u4RegionBw;
	}

	for (i = 0; i < *prForbiddenListLen; ++i) {
		DBGLOG(CCM, TRACE, "fw1:%u, fw2:%u, inv1:%u, inv2:%u, iso:%u\n",
			arRegionOutput[i].u4BoundForward1,
			arRegionOutput[i].u4BoundForward2,
			arRegionOutput[i].u4BoundInverse1,
			arRegionOutput[i].u4BoundInverse2,
			arRegionOutput[i].u4BoundIsolate);
	}
}

u_int8_t ccmIsPreferAA(struct ADAPTER *prAdapter,
		       struct BSS_INFO *prCsaBss)
{
#if (CONFIG_BAND_NUM > 2)
	struct mt66xx_chip_info *prChipInfo = prAdapter->chip_info;
	u_int8_t fgIsAaDbdcEnable = prChipInfo->isAaDbdcEnable;
	uint32_t au4AliveBssBitmap[AA_HW_BAND_NUM] = { 0 };

	DBGLOG(CCM, LOUD, "AaDbdcEnable=%u\n", fgIsAaDbdcEnable);

	if (prCsaBss)
		ccmGetOtherAliveBssHwBitmap(prAdapter,
					    au4AliveBssBitmap, prCsaBss);
	else
		bssGetAliveBssHwBitmap(prAdapter, au4AliveBssBitmap);

	if (ENUM_BAND_NUM > 2 &&
	    /* bn2 is available */
	    au4AliveBssBitmap[AA_HW_BAND_1] != 0 &&
	    au4AliveBssBitmap[AA_HW_BAND_2] == 0 &&
	    /* skyhawk sku1 */
	    fgIsAaDbdcEnable)
		return TRUE;
#endif
	return FALSE;
}

bool ccmAAAvailableCheck(struct ADAPTER *prAdapter,
			 struct RF_CHANNEL_INFO *prChnlInfo1,
			 struct RF_CHANNEL_INFO *prChnlInfo2)
{
	uint16_t arTargetBw[2];
	uint8_t prForbiddenListLen;
	struct CCM_AA_FOBIDEN_REGION_UNIT arRegionOutput[2];
	struct RF_CHANNEL_INFO *prChnlInfo_h;
	struct RF_CHANNEL_INFO *prChnlInfo_l;

	if (!ccmIsPreferAA(prAdapter, NULL))
		return FALSE;

	if (prChnlInfo1->eBand == BAND_5G && prChnlInfo2->eBand == BAND_5G) {
		if (prChnlInfo1->u4CenterFreq1 > prChnlInfo2->u4CenterFreq1) {
			prChnlInfo_h = prChnlInfo1;
			prChnlInfo_l = prChnlInfo2;
		} else {
			prChnlInfo_h = prChnlInfo2;
			prChnlInfo_l = prChnlInfo1;
		}
#if (CFG_SUPPORT_WIFI_6G == 1)
	} else if (prChnlInfo1->eBand == BAND_5G &&
		   prChnlInfo2->eBand == BAND_6G) {
		prChnlInfo_h = prChnlInfo2;
		prChnlInfo_l = prChnlInfo1;
	} else if (prChnlInfo1->eBand == BAND_6G &&
		   prChnlInfo2->eBand == BAND_5G) {
		prChnlInfo_h = prChnlInfo1;
		prChnlInfo_l = prChnlInfo2;
#endif /* CFG_SUPPORT_WIFI_6G == 1 */
	} else
		return FALSE;

	arTargetBw[0] = ccmAABwEnumToValue(prChnlInfo_l->ucChnlBw);

	ccmAAForbiddenRegionCal(prAdapter, prChnlInfo_h, &prForbiddenListLen,
				arTargetBw, arRegionOutput);

	if (/* ADC Aliasing Forbidden Spacing */
	    (arRegionOutput[0].u4BoundForward1 <= prChnlInfo_l->u4CenterFreq1 ||
	    arRegionOutput[0].u4BoundForward2 >= prChnlInfo_l->u4CenterFreq1)
	    &&
	    (arRegionOutput[0].u4BoundInverse1 <= prChnlInfo_l->u4CenterFreq1 ||
	    arRegionOutput[0].u4BoundInverse2 >= prChnlInfo_l->u4CenterFreq1)
	    /* Isolation Channel Spacing */
	    &&
	    arRegionOutput[0].u4BoundIsolate > prChnlInfo_l->u4CenterFreq1 &&
	    prChnlInfo_l->u4CenterFreq1 != prChnlInfo_h->u4CenterFreq1)
		return TRUE;
	else
		return FALSE;
}

#endif /* CFG_SUPPORT_CCM */
