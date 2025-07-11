/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2016 MediaTek Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
/*
 ** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/nic/nic_tx.c#2
 */

/*! \file   nic_tx.c
 *    \brief  Functions that provide TX operation in NIC Layer.
 *
 *    This file provides TX functions which are responsible for both Hardware
 *    and Software Resource Management and keep their Synchronization.
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

/*******************************************************************************
 *                              C O N S T A N T S
 *******************************************************************************
 */

/*******************************************************************************
 *                             D A T A   T Y P E S
 *******************************************************************************
 */

/*******************************************************************************
 *                            P U B L I C   D A T A
 *******************************************************************************
 */


/*******************************************************************************
 *                           P R I V A T E   D A T A
 *******************************************************************************
 */

/*******************************************************************************
 *                                 M A C R O S
 *******************************************************************************
 */

/*******************************************************************************
 *                  F U N C T I O N   D E C L A R A T I O N S
 *******************************************************************************
 */

/*******************************************************************************
 *                              F U N C T I O N S
 *******************************************************************************
 */

uint16_t nic_rxd_v1_get_rx_byte_count(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_RX_BYTE_CNT(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_packet_type(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_PKT_TYPE(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_wlan_idx(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_WLAN_IDX(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_sec_mode(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_SEC_MODE(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_sw_class_error_bit(
	void *prRxStatus)
{
	struct HW_MAC_RX_DESC *prRxD;

	prRxD = (struct HW_MAC_RX_DESC *)prRxStatus;

	if ((prRxD->u2StatusFlag & RXS_DW2_RX_CLASSERR_BITMAP)
		== RXS_DW2_RX_CLASSERR_VALUE) {
		DBGLOG(RSN, ERROR,
		       "RX_CLASSERR: StatusFlag=0x%x\n",
		       prRxD->u2StatusFlag);
		return TRUE;
	} else
		return FALSE;
}

uint8_t nic_rxd_v1_get_ch_num(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_CHNL_NUM(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_rf_band(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_RF_BAND(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_tcl(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_TCL(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

uint8_t nic_rxd_v1_get_ofld(
	void *prRxStatus)
{
	return HAL_RX_STATUS_GET_OFLD(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}
/*----------------------------------------------------------------------------*/
/*!
 * @brief Fill RFB
 *
 * @param prAdapter pointer to the Adapter handler
 * @param prSWRfb   specify the RFB to receive rx data
 *
 * @return (none)
 *
 */
/*----------------------------------------------------------------------------*/
void nic_rxd_v1_fill_rfb(
	struct ADAPTER *prAdapter,
	struct SW_RFB *prSwRfb)
{
	struct mt66xx_chip_info *prChipInfo;
	struct HW_MAC_RX_DESC *prRxStatus;

	uint32_t u4PktLen = 0;
	/* UINT_32 u4MacHeaderLen; */
	uint32_t u4HeaderOffset;
	uint16_t u2RxStatusOffset;

	DEBUGFUNC("nicRxFillRFB");

	prChipInfo = prAdapter->chip_info;
	prRxStatus = prSwRfb->prRxStatus;

	u4PktLen = (uint32_t) HAL_RX_STATUS_GET_RX_BYTE_CNT(
			   prRxStatus);
	u4HeaderOffset = (uint32_t) (
				 HAL_RX_STATUS_GET_HEADER_OFFSET(prRxStatus));

	u2RxStatusOffset = prChipInfo->rxd_size;
	prSwRfb->ucGroupVLD = (uint8_t) HAL_RX_STATUS_GET_GROUP_VLD(
		prRxStatus);
	if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_4)) {
		prSwRfb->prRxStatusGroup4 = (struct HW_MAC_RX_STS_GROUP_4 *)
			((uint8_t *) prRxStatus + u2RxStatusOffset);
		u2RxStatusOffset += sizeof(struct HW_MAC_RX_STS_GROUP_4);

	}
	if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_1)) {
		prSwRfb->prRxStatusGroup1 = (struct HW_MAC_RX_STS_GROUP_1 *)
			((uint8_t *) prRxStatus + u2RxStatusOffset);
		u2RxStatusOffset += sizeof(struct HW_MAC_RX_STS_GROUP_1);

	}
	if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_2)) {
		prSwRfb->prRxStatusGroup2 = (struct HW_MAC_RX_STS_GROUP_2 *)
			((uint8_t *) prRxStatus + u2RxStatusOffset);
		u2RxStatusOffset += sizeof(struct HW_MAC_RX_STS_GROUP_2);

	}
	if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_3)) {
		prSwRfb->prRxStatusGroup3 = (struct HW_MAC_RX_STS_GROUP_3 *)
			((uint8_t *) prRxStatus + u2RxStatusOffset);
		u2RxStatusOffset += sizeof(struct HW_MAC_RX_STS_GROUP_3);
	}

	prSwRfb->u2RxStatusOffst = u2RxStatusOffset;
	prSwRfb->pvHeader = (uint8_t *) prRxStatus +
		u2RxStatusOffset + u4HeaderOffset;
	prSwRfb->u2RxByteCount = u4PktLen;
	prSwRfb->u2PacketLen = (uint16_t) (u4PktLen -
		(u2RxStatusOffset + u4HeaderOffset));
	prSwRfb->u2HeaderLen = (uint16_t)
		HAL_RX_STATUS_GET_HEADER_LEN(prRxStatus);
	prSwRfb->ucWlanIdx = (uint8_t) HAL_RX_STATUS_GET_WLAN_IDX(
		prRxStatus);
	prSwRfb->ucStaRecIdx = secGetStaIdxByWlanIdx(prAdapter,
		(uint8_t) HAL_RX_STATUS_GET_WLAN_IDX(prRxStatus));
	prSwRfb->prStaRec = cnmGetStaRecByIndex(prAdapter,
		prSwRfb->ucStaRecIdx);
	prSwRfb->ucTid = (uint8_t) HAL_RX_STATUS_GET_TID(
		prRxStatus);
	prSwRfb->fgHdrTran = HAL_RX_STATUS_IS_HEADER_TRAN(prRxStatus);
	prSwRfb->ucPayloadFormat = HAL_RX_STATUS_GET_PAYLOAD_FORMAT(prRxStatus);
	prSwRfb->fgIcvErr = HAL_RX_STATUS_IS_ICV_ERROR(prRxStatus);
	prSwRfb->ucSecMode = HAL_RX_STATUS_GET_SEC_MODE(prRxStatus);
	prSwRfb->ucOFLD = HAL_RX_STATUS_GET_OFLD(prRxStatus);
	prSwRfb->fgIsBC = HAL_RX_STATUS_IS_BC(prRxStatus);
	prSwRfb->fgIsMC = HAL_RX_STATUS_IS_MC(prRxStatus);
	prSwRfb->fgIsCipherMS = HAL_RX_STATUS_IS_CIPHER_MISMATCH(prRxStatus);
	prSwRfb->fgIsCipherLenMS = HAL_RX_STATUS_IS_CLM_ERROR(prRxStatus);
	prSwRfb->ucKeyID = HAL_RX_STATUS_GET_KEY_ID(prRxStatus);
	prSwRfb->fgIsFrag = HAL_RX_STATUS_IS_FRAG(prRxStatus);
	prSwRfb->fgIsFCS = HAL_RX_STATUS_IS_FCS_ERROR(prRxStatus);
	prSwRfb->fgIsAmpdu = HAL_RX_STATUS_IS_AMPDU_FORMAT(prRxStatus);
	prSwRfb->ucRxvSeqNo = HAL_RX_STATUS_GET_RXV_SEQ_NO(prRxStatus);
	prSwRfb->ucChnlNum = HAL_RX_STATUS_GET_CHNL_NUM(prRxStatus);

#if 0
	if (prHifRxHdr->ucReorder &
	    HIF_RX_HDR_80211_HEADER_FORMAT) {
		prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_802_11_FORMAT;
		DBGLOG(RX, TRACE, "HIF_RX_HDR_FLAG_802_11_FORMAT\n");
	}

	if (prHifRxHdr->ucReorder & HIF_RX_HDR_DO_REORDER) {
		prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_DO_REORDERING;
		DBGLOG(RX, TRACE, "HIF_RX_HDR_FLAG_DO_REORDERING\n");

		/* Get Seq. No and TID, Wlan Index info */
		if (prHifRxHdr->u2SeqNoTid & HIF_RX_HDR_BAR_FRAME) {
			prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_BAR_FRAME;
			DBGLOG(RX, TRACE, "HIF_RX_HDR_FLAG_BAR_FRAME\n");
		}

		prSwRfb->u2SSN = prHifRxHdr->u2SeqNoTid &
				 HIF_RX_HDR_SEQ_NO_MASK;
		prSwRfb->ucTid = (uint8_t) ((prHifRxHdr->u2SeqNoTid &
					     HIF_RX_HDR_TID_MASK)
					    >> HIF_RX_HDR_TID_OFFSET);
		DBGLOG(RX, TRACE, "u2SSN = %d, ucTid = %d\n",
		       prSwRfb->u2SSN, prSwRfb->ucTid);
	}

	if (prHifRxHdr->ucReorder & HIF_RX_HDR_WDS) {
		prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_AMP_WDS;
		DBGLOG(RX, TRACE, "HIF_RX_HDR_FLAG_AMP_WDS\n");
	}
#endif
}

void nic_rxd_v1_parse_drop_pkt(struct SW_RFB *prSwRfb,
	struct ADAPTER *prAdapter, uint8_t ucBssIndex)
{
	uint16_t *pu2EtherType;

	pu2EtherType = (uint16_t *)
			((uint8_t *)prSwRfb->pvHeader +
			2 * MAC_ADDR_LEN);
	DBGLOG(RX, INFO,
		"u2PacketLen:%d ucSecMode:%d ucWlanIdx:%d ucStaRecIdx:%d\n",
		prSwRfb->u2PacketLen, prSwRfb->ucSecMode,
		prSwRfb->ucWlanIdx, prSwRfb->ucStaRecIdx
	);
	STATS_RX_PKT_INFO_DISPLAY(prSwRfb, prAdapter, ucBssIndex);
}

u_int8_t nic_rxd_v1_sanity_check(
	struct ADAPTER *prAdapter,
	struct SW_RFB *prSwRfb)
{
	struct mt66xx_chip_info *prChipInfo;
	struct HW_MAC_RX_DESC *prRxStatus;
	u_int8_t fgDrop = FALSE;
	struct RX_CTRL *prRxCtrl;
	uint8_t ucBssIndex = 0;

	prRxCtrl = &prAdapter->rRxCtrl;

	prChipInfo = prAdapter->chip_info;
	prRxStatus = (struct HW_MAC_RX_DESC *)prSwRfb->prRxStatus;

	if (prSwRfb->pvPacket == NULL) {
		fgDrop = TRUE;
		goto end;
	}

	/* BA session */
	if ((prRxStatus->u2StatusFlag & RXS_DW2_AMPDU_nERR_BITMAP)
	    == RXS_DW2_AMPDU_nERR_VALUE)
		prSwRfb->fgReorderBuffer = TRUE;
	/* non BA session */
	else if ((prRxStatus->u2StatusFlag & RXS_DW2_RX_nERR_BITMAP)
		 == RXS_DW2_RX_nERR_VALUE) {
		if ((prRxStatus->u2StatusFlag & RXS_DW2_RX_nDATA_BITMAP) ==
		    RXS_DW2_RX_nDATA_VALUE)
			prSwRfb->fgDataFrame = FALSE;

		if ((prRxStatus->u2StatusFlag & RXS_DW2_RX_FRAG_BITMAP) ==
		    RXS_DW2_RX_FRAG_VALUE)
			prSwRfb->fgFragFrame = TRUE;

	} else {
		DBGLOG(RX, TEMP, "Sanity check to drop\n");
		fgDrop = TRUE;
		if (!HAL_RX_STATUS_IS_ICV_ERROR(prRxStatus)
		    && HAL_RX_STATUS_IS_TKIP_MIC_ERROR(prRxStatus)) {
			struct STA_RECORD *prStaRec = NULL;
			struct PARAM_BSSID_EX *prCurrBssid =
				aisGetCurrBssId(prAdapter,
				ucBssIndex);

			ucBssIndex =
				secGetBssIdxByWlanIdx(prAdapter,
				HAL_RX_STATUS_GET_WLAN_IDX(prRxStatus));

			if (prCurrBssid)
				prStaRec = cnmGetStaRecByAddress(prAdapter,
					ucBssIndex,
					prCurrBssid->arMacAddress);
			if (prStaRec) {
				DBGLOG(RSN, EVENT, "MIC_ERR_PKT\n");
				rsnTkipHandleMICFailure(prAdapter, prStaRec, 0);
			}
		}
#if UNIFIED_MAC_RX_FORMAT
		else if (HAL_RX_STATUS_IS_LLC_MIS(prRxStatus)
			 && !HAL_RX_STATUS_IS_ERROR(prRxStatus)
			 && !FEAT_SUP_LLC_VLAN_RX(prChipInfo)) {
			uint16_t *pu2EtherType;

			nicRxFillRFB(prAdapter, prSwRfb);

			pu2EtherType = (uint16_t *)
				((uint8_t *)prSwRfb->pvHeader +
				2 * MAC_ADDR_LEN);

			/* If ethernet type is VLAN, do not drop it.
			 * Pass up to driver process
			 */
			if (prSwRfb->u2HeaderLen >= ETH_HLEN
			    && *pu2EtherType == NTOHS(ETH_P_VLAN))
				fgDrop = FALSE;

#if CFG_SUPPORT_FRAG_AGG_ATTACK_DETECTION
			/*
			 * let qmAmsduAttackDetection check this subframe
			 * before drop it
			 */
			if (prSwRfb->ucPayloadFormat
				== RX_PAYLOAD_FORMAT_FIRST_SUB_AMSDU) {
				fgDrop = FALSE;
				prSwRfb->fgIsFirstSubAMSDULLCMS = TRUE;
			}
#endif /* CFG_SUPPORT_FRAG_AGG_ATTACK_DETECTION */
		}
#else
		else if (HAL_RX_STATUS_IS_LLC_MIS(prRxStatus)) {
			DBGLOG(RSN, EVENT, ("LLC_MIS_ERR\n"));
			fgDrop = TRUE;	/* Drop after send de-auth  */
		}
#endif

end:
		if (fgDrop) {
			if (prSwRfb->pvPacket == NULL)
				RX_INC_CNT(prRxCtrl, RX_NULL_PACKET_COUNT);
		}

		DBGLOG(RSN, TRACE, "Sanity check to drop:%d\n", fgDrop);
	}

	/* Drop plain text during security connection */
	if (prSwRfb->fgIsCipherMS && prSwRfb->fgDataFrame == TRUE) {
		uint16_t *pu2EtherType;

		pu2EtherType = (uint16_t *)
				((uint8_t *)prSwRfb->pvHeader +
				2 * MAC_ADDR_LEN);
		if (prSwRfb->u2HeaderLen >= ETH_HLEN
			&& (*pu2EtherType == NTOHS(ETH_P_1X)
#if CFG_SUPPORT_WAPI
			|| (*pu2EtherType == NTOHS(ETH_WPI_1X))
#endif
		)) {
			fgDrop = FALSE;
			DBGLOG(RSN, INFO,
				"Don't drop eapol or wpi packet\n");
		} else {
			nic_rxd_v1_parse_drop_pkt(prSwRfb,
				prAdapter, ucBssIndex);

			fgDrop = TRUE;
			DBGLOG(RSN, INFO,
				"Drop plain text during security connection\n");
		}
	}

#if CFG_SUPPORT_FRAG_AGG_ATTACK_DETECTION
	/* Drop fragmented broadcast and multicast frame */
	if ((prSwRfb->fgIsBC | prSwRfb->fgIsMC)
		&& (prSwRfb->fgFragFrame == TRUE)) {
		fgDrop = TRUE;
		DBGLOG(RSN, INFO,
			"Drop fragmented broadcast and multicast\n");
	}

	if (HAL_RX_STATUS_IS_DE_AMSDU_FAIL(prRxStatus))
		DBGLOG(RSN, INFO, "De-amsdu fail, drop:%d\n", fgDrop);
#endif /* CFG_SUPPORT_FRAG_AGG_ATTACK_DETECTION */

	/* check CLS for MD */
	if (HAL_RX_STATUS_GET_CLS_BITMAP(prRxStatus) & BITS(6, 9))
		DBGLOG(RX, WARN, "RX DW3[0x%08x]\n",
		       prRxStatus->u4PatternFilterInfo);

	return fgDrop;
}

uint8_t nic_rxd_v1_get_HdrTrans(
	void *prRxStatus)
{
	return HAL_RX_STATUS_IS_HEADER_TRAN(
		(struct HW_MAC_RX_DESC *)prRxStatus);
}

#if CFG_SUPPORT_WAKEUP_REASON_DEBUG
void nic_rxd_v1_check_wakeup_reason(
	struct ADAPTER *prAdapter,
	struct SW_RFB *prSwRfb)
{
	struct mt66xx_chip_info *prChipInfo;
	struct WIFI_EVENT *prEvent;
	uint8_t *pvHeader = NULL;
	struct HW_MAC_RX_DESC *prRxStatus;
	uint16_t u2PktLen = 0;
	uint32_t u4HeaderOffset;
#if CFG_TC10_FEATURE
	struct sk_buff *skb = (struct sk_buff *)prSwRfb->pvPacket;
#endif

	prChipInfo = prAdapter->chip_info;

	prRxStatus = (struct HW_MAC_RX_DESC *)prSwRfb->prRxStatus;
	if (!prRxStatus)
		return;

	prSwRfb->ucGroupVLD = (uint8_t) HAL_RX_STATUS_GET_GROUP_VLD(prRxStatus);

	switch (prSwRfb->ucPacketType) {
	case RX_PKT_TYPE_RX_DATA:
	{
		uint16_t u2Temp = 0;

/* fos_change begin */
#if CFG_SUPPORT_WAKEUP_STATISTICS
		nicUpdateWakeupStatistics(prAdapter, RX_DATA_INT);
#endif /* fos_change end */
#if CFG_TC10_FEATURE
		skb->mark |= 0x80000000;
#endif

		u2PktLen = HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus);
		u4HeaderOffset = (uint32_t)
			(HAL_RX_STATUS_GET_HEADER_OFFSET(prRxStatus));
		u2Temp = prChipInfo->rxd_size;
		if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_4))
			u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_4);
		if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_1))
			u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_1);
		if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_2))
			u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_2);
		if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_3))
			u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_3);
		pvHeader = (uint8_t *)prRxStatus + u2Temp + u4HeaderOffset;
		u2PktLen -= u2Temp + u4HeaderOffset;
		if (!pvHeader) {
			DBGLOG(RX, ERROR,
				"data packet but pvHeader is NULL!\n");
			break;
		}
		if ((prRxStatus->u2StatusFlag & (RXS_DW2_RX_nERR_BITMAP |
			RXS_DW2_RX_nDATA_BITMAP)) == RXS_DW2_RX_nDATA_VALUE) {
			struct WLAN_MAC_HEADER *prWlanMacHeader =
				(struct WLAN_MAC_HEADER *)pvHeader;

			if ((prWlanMacHeader->u2FrameCtrl & MASK_FRAME_TYPE) ==
				MAC_FRAME_BLOCK_ACK_REQ) {
				DBGLOG(RX, INFO,
					"BAR frame[SSN:%d,TID:%d] wakeup host\n"
					, prSwRfb->u2SSN, prSwRfb->ucTid);
				break;
			}
		}
		u2Temp = (pvHeader[ETH_TYPE_LEN_OFFSET] << 8) |
			 (pvHeader[ETH_TYPE_LEN_OFFSET + 1]);

		switch (u2Temp) {
		case ETH_P_IPV4:
			u2Temp = *(uint16_t *) &pvHeader[ETH_HLEN + 4];
			DBGLOG(RX, INFO,
				"IP Pkt [" IPV4STR ",IPID:0x%04x] wakeup host",
				IPV4TOSTR(&pvHeader[ETH_HLEN + 12]),
				u2Temp);
			break;
		case ETH_P_ARP:
		case ETH_P_1X:
		case ETH_P_PRE_1X:
#if CFG_SUPPORT_WAPI
		case ETH_WPI_1X:
#endif
		case ETH_P_AARP:
		case ETH_P_IPV6:
		case ETH_P_IPX:
		case 0x8100: /* VLAN */
		case 0x890d: /* TDLS */
			DBGLOG(RX, INFO,
				"Data Packet, EthType 0x%04x wakeup host\n",
				u2Temp);
			break;
		default:
			if (HAL_RX_STATUS_IS_LLC_MIS(prRxStatus)) {
				DBGLOG(RX, WARN,
					"abnormal packet, Header translate fail\n");
				DBGLOG_MEM8(RX, INFO,
					(uint8_t *)prSwRfb->prRxStatus,
					prChipInfo->rxd_size);
				if (u2PktLen < CFG_RX_MAX_PKT_SIZE) {
					DBGLOG_MEM8(RX, INFO,
						pvHeader, u2PktLen);
				}
			} else {
				DBGLOG(RX, WARN,
					"abnormal packet, EthType 0x%04x wakeup host\n",
					u2Temp);
				DBGLOG_MEM8(RX, INFO,
					pvHeader, u2PktLen > 50 ? 50:u2PktLen);
			}
			break;
		}
		break;
	}
	case RX_PKT_TYPE_SW_DEFINED:
		/* HIF_RX_PKT_TYPE_EVENT */
		if ((NIC_RX_GET_U2_SW_PKT_TYPE(prSwRfb->prRxStatus) &
			RXM_RXD_PKT_TYPE_SW_BITMAP) ==
			RXM_RXD_PKT_TYPE_SW_EVENT) {

			prEvent = (struct WIFI_EVENT *)
				(prSwRfb->pucRecvBuff + prChipInfo->rxd_size);
/* fos_change begin */
#if CFG_SUPPORT_WAKEUP_STATISTICS
			nicUpdateWakeupStatistics(prAdapter, RX_EVENT_INT);
			prAdapter->wake_event_count[prEvent->ucEID]++;
#endif

			DBGLOG(RX, INFO, "Event 0x%02x wakeup host\n",
				prEvent->ucEID);
			break;

		}
		/* case HIF_RX_PKT_TYPE_MANAGEMENT: */
		else if ((NIC_RX_GET_U2_SW_PKT_TYPE(prSwRfb->prRxStatus) &
			RXM_RXD_PKT_TYPE_SW_BITMAP) ==
			RXM_RXD_PKT_TYPE_SW_FRAME) {
			uint8_t ucSubtype;
			struct WLAN_MAC_MGMT_HEADER *prWlanMgmtHeader;
			uint16_t u2Temp = prChipInfo->rxd_size;
			/* fos_change begin */
#if CFG_SUPPORT_WAKEUP_STATISTICS
			nicUpdateWakeupStatistics(prAdapter, RX_MGMT_INT);
#endif /* fos_change end */


			u4HeaderOffset = (uint32_t)
				(HAL_RX_STATUS_GET_HEADER_OFFSET(prRxStatus));
			if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_4))
				u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_4);
			if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_1))
				u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_1);
			if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_2))
				u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_2);
			if (prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_3))
				u2Temp += sizeof(struct HW_MAC_RX_STS_GROUP_3);
			pvHeader = (uint8_t *)
				prRxStatus + u2Temp + u4HeaderOffset;
			if (!pvHeader) {
				DBGLOG(RX, ERROR,
					"Mgmt Frame but pvHeader is NULL!\n");
				break;
			}
			prWlanMgmtHeader =
				(struct WLAN_MAC_MGMT_HEADER *)pvHeader;
			ucSubtype = (prWlanMgmtHeader->u2FrameCtrl &
				MASK_FC_SUBTYPE) >> OFFSET_OF_FC_SUBTYPE;
			DBGLOG(RX, INFO,
				"MGMT frame subtype: %d\n",
				ucSubtype);
			DBGLOG(RX, INFO,
				" SeqCtrl %d wakeup host\n",
				prWlanMgmtHeader->u2SeqCtrl);
		} else {
			DBGLOG(RX, ERROR,
				"[%s]: u2PktTYpe(0x%04X) is OUT OF DEF.!!!\n",
				__func__, prSwRfb->ucPacketType);
			ASSERT(0);
		}
		break;
	default:
		/* fos_change begin */
#if CFG_SUPPORT_WAKEUP_STATISTICS
		nicUpdateWakeupStatistics(prAdapter, RX_OTHERS_INT);
#endif
		DBGLOG(RX, WARN, "Unknown Packet %d wakeup host\n",
			prSwRfb->ucPacketType);
		break;
	}
}
#endif /* CFG_SUPPORT_WAKEUP_REASON_DEBUG */
