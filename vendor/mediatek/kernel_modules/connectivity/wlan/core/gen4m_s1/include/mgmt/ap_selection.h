/*******************************************************************************
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
 ******************************************************************************/

#ifndef _AP_SELECTION_H
#define _AP_SELECTION_H

/* Support AP Selection */
#if (CFG_SUPPORT_802_11AX == 1)
#define AX_SEL_DEF_WEIGHT		(0)
#define AX_SEL_DEF_DIVIDER		(1)
#endif

#define AP_SELECTION_AMSDU_HT_3K	(3839)
#define AP_SELECTION_AMSDU_HT_8K	(7935)
#define AP_SELECTION_AMSDU_VHT_HE_3K	(3895)
#define AP_SELECTION_AMSDU_VHT_HE_8K	(7991)
#define AP_SELECTION_AMSDU_VHT_HE_11K	(11454)

enum ROAM_TYPE {
	ROAM_TYPE_RCPI,
	ROAM_TYPE_PER,
	ROAM_TYPE_NUM
};

typedef uint8_t(*PFN_SELECTION_POLICY_FUNC) (
	enum ENUM_BAND eCurrentBand,
	int8_t cCandidateRssi,
	int8_t cCurrentRssi
);

struct NETWORK_SELECTION_POLICY_BY_BAND {
	enum ENUM_BAND eCandidateBand;
	PFN_SELECTION_POLICY_FUNC pfnNetworkSelection;
};

struct CU_INFO_BY_FREQ {
	uint8_t ucTotalApHaveCu;
	uint16_t ucTotalCu;
	enum ENUM_BAND eBand;
};

#if (CFG_SUPPORT_AVOID_DESENSE == 1)
struct WFA_DESENSE_CHANNEL_LIST {
	int8_t ucChLowerBound;
	int8_t ucChUpperBound;
};

extern const struct WFA_DESENSE_CHANNEL_LIST desenseChList[BAND_NUM];

#define IS_CHANNEL_IN_DESENSE_RANGE(_prAdapter, _ch, _band) \
	(!!(_prAdapter->fgIsNeedAvoidDesenseFreq && (_band != BAND_2G4) && \
	(_ch >= desenseChList[_band].ucChLowerBound) && \
	(_ch <= desenseChList[_band].ucChUpperBound)))
#endif

struct BSS_DESC *scanSearchBssDescByScoreForAis(struct ADAPTER *prAdapter,
	enum ENUM_ROAMING_REASON eRoamReason, uint8_t ucBssIndex);
void scanGetCurrentEssChnlList(struct ADAPTER *prAdapter, uint8_t ucBssIndex);
uint8_t scanCheckNeedDriverRoaming(
	struct ADAPTER *prAdapter, uint8_t ucBssIndex);
uint8_t scanBeaconTimeoutFilterPolicyForAis(struct ADAPTER *prAdapter,
	uint8_t ucBssIndex);
uint8_t scanNetworkReplaceHandler2G4(enum ENUM_BAND eCurrentBand,
	int8_t cCandidateRssi, int8_t cCurrentRssi);
uint8_t scanNetworkReplaceHandler5G(enum ENUM_BAND eCurrentBand,
	int8_t cCandidateRssi, int8_t cCurrentRssi);
#if (CFG_SUPPORT_WIFI_6G == 1)
uint8_t scanNetworkReplaceHandler6G(enum ENUM_BAND eCurrentBand,
	int8_t cCandidateRssi, int8_t cCurrentRssi);
#endif
#endif

