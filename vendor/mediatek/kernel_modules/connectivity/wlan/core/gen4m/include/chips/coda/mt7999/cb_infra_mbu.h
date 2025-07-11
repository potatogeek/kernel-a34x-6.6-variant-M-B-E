// SPDX-License-Identifier: BSD-2-Clause
//[File]            : cb_infra_mbu.h
//[Revision time]   : Sun Apr 16 17:13:31 2023
//[Description]     : This file is auto generated by CODA
//[Copyright]       : Copyright (C) 2023 Mediatek Incorportion. All rights reserved.

#ifndef __CB_INFRA_MBU_REGS_H__
#define __CB_INFRA_MBU_REGS_H__

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif


//****************************************************************************
//
//                     CB_INFRA_MBU CR Definitions                     
//
//****************************************************************************

#define CB_INFRA_MBU_BASE                                      0x74130000

#define CB_INFRA_MBU_TRG_ADDR                                  (CB_INFRA_MBU_BASE + 0x000) // 0000
#define CB_INFRA_MBU_GPO_ADDR                                  (CB_INFRA_MBU_BASE + 0x004) // 0004
#define CB_INFRA_MBU_LPI_ADDR                                  (CB_INFRA_MBU_BASE + 0x008) // 0008
#define CB_INFRA_MBU_GPI_RTRG_ADDR                             (CB_INFRA_MBU_BASE + 0x038) // 0038
#define CB_INFRA_MBU_GPI_FTRG_ADDR                             (CB_INFRA_MBU_BASE + 0x03C) // 003C
#define CB_INFRA_MBU_OPD_A_ADDR                                (CB_INFRA_MBU_BASE + 0x040) // 0040
#define CB_INFRA_MBU_OPD_B_ADDR                                (CB_INFRA_MBU_BASE + 0x044) // 0044
#define CB_INFRA_MBU_OPD_C_ADDR                                (CB_INFRA_MBU_BASE + 0x048) // 0048
#define CB_INFRA_MBU_OPD_D_ADDR                                (CB_INFRA_MBU_BASE + 0x04C) // 004C
#define CB_INFRA_MBU_OPD_E_ADDR                                (CB_INFRA_MBU_BASE + 0x050) // 0050
#define CB_INFRA_MBU_OPD_F_ADDR                                (CB_INFRA_MBU_BASE + 0x054) // 0054
#define CB_INFRA_MBU_OPD_G_ADDR                                (CB_INFRA_MBU_BASE + 0x058) // 0058
#define CB_INFRA_MBU_OPD_H_ADDR                                (CB_INFRA_MBU_BASE + 0x05C) // 005C
#define CB_INFRA_MBU_OPD_WRMAC_ADDR                            (CB_INFRA_MBU_BASE + 0x060) // 0060
#define CB_INFRA_MBU_OPD_WRPHY_ADDR                            (CB_INFRA_MBU_BASE + 0x064) // 0064
#define CB_INFRA_MBU_OPD_WRWFON_ADDR                           (CB_INFRA_MBU_BASE + 0x068) // 0068
#define CB_INFRA_MBU_IRQ_TRG_ADDR                              (CB_INFRA_MBU_BASE + 0x06c) // 006C
#define CB_INFRA_MBU_INTA_ADDR                                 (CB_INFRA_MBU_BASE + 0x070) // 0070
#define CB_INFRA_MBU_INT_CLR_ADDR                              (CB_INFRA_MBU_BASE + 0x074) // 0074
#define CB_INFRA_MBU_ST_ADDR                                   (CB_INFRA_MBU_BASE + 0x080) // 0080
#define CB_INFRA_MBU_SW_PAUSE_ADDR                             (CB_INFRA_MBU_BASE + 0x084) // 0084
#define CB_INFRA_MBU_RESERVE_0_ADDR                            (CB_INFRA_MBU_BASE + 0x09C) // 009C
#define CB_INFRA_MBU_OPD_TOP_H_ADDR                            (CB_INFRA_MBU_BASE + 0x100) // 0100
#define CB_INFRA_MBU_OPD_TOP_L_ADDR                            (CB_INFRA_MBU_BASE + 0x104) // 0104
#define CB_INFRA_MBU_MBU_VECTOR_EVT_ADDR                       (CB_INFRA_MBU_BASE + 0x200) // 0200
#define CB_INFRA_MBU_CR_VECTOR_EVENT_MASK_ADDR                 (CB_INFRA_MBU_BASE + 0x204) // 0204




/* =====================================================================================

  ---TRG (0x74130000 + 0x000)---

    TERMINATE[0]                 - (WO) Software terminate CMDBT procedure immediately
                                     0 : to do nothing
                                     1 : to reset FSM back to IDLE
                                     (Read value is always 0)
    TRIGGER[1]                   - (WO) Software trigger CMDBT procedure
                                     0: to do nothing
                                     1: to trigger instruction fetch procedure
                                     (Read value is always 0)
    RESET[2]                     - (WO) Software 
                                     0: to do nothing
                                     1: to  terminate FSM back to IDLE as intercution fetch
                                     (Read value is always 0)
    RESERVED3[31..3]             - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_TRG_RESET_ADDR                            CB_INFRA_MBU_TRG_ADDR
#define CB_INFRA_MBU_TRG_RESET_MASK                            0x00000004                // RESET[2]
#define CB_INFRA_MBU_TRG_RESET_SHFT                            2
#define CB_INFRA_MBU_TRG_TRIGGER_ADDR                          CB_INFRA_MBU_TRG_ADDR
#define CB_INFRA_MBU_TRG_TRIGGER_MASK                          0x00000002                // TRIGGER[1]
#define CB_INFRA_MBU_TRG_TRIGGER_SHFT                          1
#define CB_INFRA_MBU_TRG_TERMINATE_ADDR                        CB_INFRA_MBU_TRG_ADDR
#define CB_INFRA_MBU_TRG_TERMINATE_MASK                        0x00000001                // TERMINATE[0]
#define CB_INFRA_MBU_TRG_TERMINATE_SHFT                        0

/* =====================================================================================

  ---GPO (0x74130000 + 0x004)---

    GPO[15..0]                   - (RW) general purpose output
    GPO_LEVEL[31..16]            - (RW) output level of general purpose output

 =====================================================================================*/
#define CB_INFRA_MBU_GPO_GPO_LEVEL_ADDR                        CB_INFRA_MBU_GPO_ADDR
#define CB_INFRA_MBU_GPO_GPO_LEVEL_MASK                        0xFFFF0000                // GPO_LEVEL[31..16]
#define CB_INFRA_MBU_GPO_GPO_LEVEL_SHFT                        16
#define CB_INFRA_MBU_GPO_GPO_ADDR                              CB_INFRA_MBU_GPO_ADDR
#define CB_INFRA_MBU_GPO_GPO_MASK                              0x0000FFFF                // GPO[15..0]
#define CB_INFRA_MBU_GPO_GPO_SHFT                              0

/* =====================================================================================

  ---LPI (0x74130000 + 0x008)---

    LPI[31..0]                   - (WO) Low power Instruction

 =====================================================================================*/
#define CB_INFRA_MBU_LPI_LPI_ADDR                              CB_INFRA_MBU_LPI_ADDR
#define CB_INFRA_MBU_LPI_LPI_MASK                              0xFFFFFFFF                // LPI[31..0]
#define CB_INFRA_MBU_LPI_LPI_SHFT                              0

/* =====================================================================================

  ---GPI_RTRG (0x74130000 + 0x038)---

    GPI_RTRG[15..0]              - (RW) general purpose input
    RESERVED16[31..16]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_GPI_RTRG_GPI_RTRG_ADDR                    CB_INFRA_MBU_GPI_RTRG_ADDR
#define CB_INFRA_MBU_GPI_RTRG_GPI_RTRG_MASK                    0x0000FFFF                // GPI_RTRG[15..0]
#define CB_INFRA_MBU_GPI_RTRG_GPI_RTRG_SHFT                    0

/* =====================================================================================

  ---GPI_FTRG (0x74130000 + 0x03C)---

    GPI_FTRG[15..0]              - (RW) general purpose input
    RESERVED16[31..16]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_GPI_FTRG_GPI_FTRG_ADDR                    CB_INFRA_MBU_GPI_FTRG_ADDR
#define CB_INFRA_MBU_GPI_FTRG_GPI_FTRG_MASK                    0x0000FFFF                // GPI_FTRG[15..0]
#define CB_INFRA_MBU_GPI_FTRG_GPI_FTRG_SHFT                    0

/* =====================================================================================

  ---OPD_A (0x74130000 + 0x040)---

    OPDA[31..0]                  - (RW) CMDBT operand A

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_A_OPDA_ADDR                           CB_INFRA_MBU_OPD_A_ADDR
#define CB_INFRA_MBU_OPD_A_OPDA_MASK                           0xFFFFFFFF                // OPDA[31..0]
#define CB_INFRA_MBU_OPD_A_OPDA_SHFT                           0

/* =====================================================================================

  ---OPD_B (0x74130000 + 0x044)---

    OPDB[31..0]                  - (RW) CMDBT operand B

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_B_OPDB_ADDR                           CB_INFRA_MBU_OPD_B_ADDR
#define CB_INFRA_MBU_OPD_B_OPDB_MASK                           0xFFFFFFFF                // OPDB[31..0]
#define CB_INFRA_MBU_OPD_B_OPDB_SHFT                           0

/* =====================================================================================

  ---OPD_C (0x74130000 + 0x048)---

    OPDC[31..0]                  - (RW) CMDBT operand C

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_C_OPDC_ADDR                           CB_INFRA_MBU_OPD_C_ADDR
#define CB_INFRA_MBU_OPD_C_OPDC_MASK                           0xFFFFFFFF                // OPDC[31..0]
#define CB_INFRA_MBU_OPD_C_OPDC_SHFT                           0

/* =====================================================================================

  ---OPD_D (0x74130000 + 0x04C)---

    OPDD[31..0]                  - (RW) CMDBT operand D

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_D_OPDD_ADDR                           CB_INFRA_MBU_OPD_D_ADDR
#define CB_INFRA_MBU_OPD_D_OPDD_MASK                           0xFFFFFFFF                // OPDD[31..0]
#define CB_INFRA_MBU_OPD_D_OPDD_SHFT                           0

/* =====================================================================================

  ---OPD_E (0x74130000 + 0x050)---

    OPDE[31..0]                  - (RW) CMDBT operand E

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_E_OPDE_ADDR                           CB_INFRA_MBU_OPD_E_ADDR
#define CB_INFRA_MBU_OPD_E_OPDE_MASK                           0xFFFFFFFF                // OPDE[31..0]
#define CB_INFRA_MBU_OPD_E_OPDE_SHFT                           0

/* =====================================================================================

  ---OPD_F (0x74130000 + 0x054)---

    OPDF[31..0]                  - (RW) CMDBT operand F

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_F_OPDF_ADDR                           CB_INFRA_MBU_OPD_F_ADDR
#define CB_INFRA_MBU_OPD_F_OPDF_MASK                           0xFFFFFFFF                // OPDF[31..0]
#define CB_INFRA_MBU_OPD_F_OPDF_SHFT                           0

/* =====================================================================================

  ---OPD_G (0x74130000 + 0x058)---

    OPDG[31..0]                  - (RW) CMDBT operand G

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_G_OPDG_ADDR                           CB_INFRA_MBU_OPD_G_ADDR
#define CB_INFRA_MBU_OPD_G_OPDG_MASK                           0xFFFFFFFF                // OPDG[31..0]
#define CB_INFRA_MBU_OPD_G_OPDG_SHFT                           0

/* =====================================================================================

  ---OPD_H (0x74130000 + 0x05C)---

    OPDH[31..0]                  - (RW) CMDBT operand H

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_H_OPDH_ADDR                           CB_INFRA_MBU_OPD_H_ADDR
#define CB_INFRA_MBU_OPD_H_OPDH_MASK                           0xFFFFFFFF                // OPDH[31..0]
#define CB_INFRA_MBU_OPD_H_OPDH_SHFT                           0

/* =====================================================================================

  ---OPD_WRMAC (0x74130000 + 0x060)---

    OPD_WR_MAC[23..0]            - (RW) CMDBT operand for WF MAC write
                                     [23:16]: write address[15:8]
                                     [15:0]: write data[31:16]
    RESERVED24[31..24]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_WRMAC_OPD_WR_MAC_ADDR                 CB_INFRA_MBU_OPD_WRMAC_ADDR
#define CB_INFRA_MBU_OPD_WRMAC_OPD_WR_MAC_MASK                 0x00FFFFFF                // OPD_WR_MAC[23..0]
#define CB_INFRA_MBU_OPD_WRMAC_OPD_WR_MAC_SHFT                 0

/* =====================================================================================

  ---OPD_WRPHY (0x74130000 + 0x064)---

    OPD_WR_PHY[23..0]            - (RW) CMDBT operand for WF PHY write
                                     [23:16]: write address[15:8]
                                     [15:0]: write data[31:16]
    RESERVED24[31..24]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_WRPHY_OPD_WR_PHY_ADDR                 CB_INFRA_MBU_OPD_WRPHY_ADDR
#define CB_INFRA_MBU_OPD_WRPHY_OPD_WR_PHY_MASK                 0x00FFFFFF                // OPD_WR_PHY[23..0]
#define CB_INFRA_MBU_OPD_WRPHY_OPD_WR_PHY_SHFT                 0

/* =====================================================================================

  ---OPD_WRWFON (0x74130000 + 0x068)---

    OPD_WR_WFON[27..0]           - (RW) CMDBT operand for WF_ON write
                                     [27:24]: write address[24:20]
                                     [23:16]: write address[15:8]
                                     [15:0]: write data[31:16]
    RESERVED28[31..28]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_WRWFON_OPD_WR_WFON_ADDR               CB_INFRA_MBU_OPD_WRWFON_ADDR
#define CB_INFRA_MBU_OPD_WRWFON_OPD_WR_WFON_MASK               0x0FFFFFFF                // OPD_WR_WFON[27..0]
#define CB_INFRA_MBU_OPD_WRWFON_OPD_WR_WFON_SHFT               0

/* =====================================================================================

  ---IRQ_TRG (0x74130000 + 0x06c)---

    RESERVED0[23..0]             - (RO) Reserved bits
    CFG_IRQ_TRG[24]              - (RW) CMDBT IRQ Trigger enable
                                     0: disable
                                     1: enable
    RESERVED25[31..25]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_IRQ_TRG_CFG_IRQ_TRG_ADDR                  CB_INFRA_MBU_IRQ_TRG_ADDR
#define CB_INFRA_MBU_IRQ_TRG_CFG_IRQ_TRG_MASK                  0x01000000                // CFG_IRQ_TRG[24]
#define CB_INFRA_MBU_IRQ_TRG_CFG_IRQ_TRG_SHFT                  24

/* =====================================================================================

  ---INTA (0x74130000 + 0x070)---

    INTA[7..0]                   - (RW) CMDBT software interrupt A
                                     0 : to do nothing
                                     1 : to assert INTA
                                     INTA will be cleared when write 1 to INTA_CLR
                                     (Note INTA[0] will be asserted automatically as executing an END instruction)
    CMDBT_INT_EN[20..8]          - (RW) CMDBT hardware interrupt enable
                                     0 : disable
                                     1 : enable
                                     Each bit enables hardware END instruction interrupt triggered by different source
                                     [0] : bn0_wf_wlanon_to_on
                                     [1] : bn0_wf_wlanon_to_sleep
                                     [2] : bn0_wf_sleep_to_wlanon
                                     [3] : bn0_wf_init_to_sleep
                                     [4] : umac backup
                                     [5] : umac restore
                                     [6] : top backup
                                     [7] : top restore
                                     [8] : software trigger
                                     [9] : bn1_wf_wlanon_to_on
                                     [10] : bn1_wf_wlanon_to_sleep
                                     [11] : bn1_wf_sleep_to_wlanon
                                     [12] : bn1_wf_init_to_sleep
    RESERVED21[30..21]           - (RO) Reserved bits
    CMDBT_INT_S[31]              - (RW) CMDBT interrupt to MCU source selection
                                     0 : hardware interrupt triggered by exeute an END instrunction
                                     1 : softwaire interrupt A triggerd by any bit of INTA is set from 0 to 1

 =====================================================================================*/
#define CB_INFRA_MBU_INTA_CMDBT_INT_S_ADDR                     CB_INFRA_MBU_INTA_ADDR
#define CB_INFRA_MBU_INTA_CMDBT_INT_S_MASK                     0x80000000                // CMDBT_INT_S[31]
#define CB_INFRA_MBU_INTA_CMDBT_INT_S_SHFT                     31
#define CB_INFRA_MBU_INTA_CMDBT_INT_EN_ADDR                    CB_INFRA_MBU_INTA_ADDR
#define CB_INFRA_MBU_INTA_CMDBT_INT_EN_MASK                    0x001FFF00                // CMDBT_INT_EN[20..8]
#define CB_INFRA_MBU_INTA_CMDBT_INT_EN_SHFT                    8
#define CB_INFRA_MBU_INTA_INTA_ADDR                            CB_INFRA_MBU_INTA_ADDR
#define CB_INFRA_MBU_INTA_INTA_MASK                            0x000000FF                // INTA[7..0]
#define CB_INFRA_MBU_INTA_INTA_SHFT                            0

/* =====================================================================================

  ---INT_CLR (0x74130000 + 0x074)---

    INT_CLR[7..0]                - (W1C) CMDBT software interrupt A clear
                                     0 : to do nothing
                                     1 : to clear INTA
                                     (Read value is always 0)
    RESERVED8[31..8]             - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_INT_CLR_INT_CLR_ADDR                      CB_INFRA_MBU_INT_CLR_ADDR
#define CB_INFRA_MBU_INT_CLR_INT_CLR_MASK                      0x000000FF                // INT_CLR[7..0]
#define CB_INFRA_MBU_INT_CLR_INT_CLR_SHFT                      0

/* =====================================================================================

  ---ST (0x74130000 + 0x080)---

    CMDBT_ST[3..0]               - (RO) CMDBT execution state machine for debug
    CMDBT_BUSY[4]                - (RO) CMDBT busy status for debug
    RESERVED5[7..5]              - (RO) Reserved bits
    FIFO_EMPTY[8]                - (RO) Fetch's FIFO empty staus for debug
    FIFO_FULL[9]                 - (RO) Fetch's FIFO full staus for debug
    RESERVED10[15..10]           - (RO) Reserved bits
    CMDBT_EVT_ST[28..16]         - (W1C) CMDBT triggere event status (software write-1 bit to clear)
                                     [0] : bn0_wf_wlanon_to_on
                                     [1] : bn0_wf_wlanon_to_sleep
                                     [2] : bn0_wf_sleep_to_wlanon
                                     [3] : bn0_wf_init_to_sleep
                                     [4] : umac backup
                                     [5] : umac restore
                                     [6] : top backup
                                     [7] : top restore
                                     [8] : software trigger
                                     [9] : bn1_wf_wlanon_to_on
                                     [10] : bn1_wf_wlanon_to_sleep
                                     [11] : bn1_wf_sleep_to_wlanon
                                     [12] : bn1_wf_init_to_sleep
    RESERVED29[30..29]           - (RO) Reserved bits
    CARRY[31]                    - (RO) Instruction Adder / subtractor's carry for debug

 =====================================================================================*/
#define CB_INFRA_MBU_ST_CARRY_ADDR                             CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_CARRY_MASK                             0x80000000                // CARRY[31]
#define CB_INFRA_MBU_ST_CARRY_SHFT                             31
#define CB_INFRA_MBU_ST_CMDBT_EVT_ST_ADDR                      CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_CMDBT_EVT_ST_MASK                      0x1FFF0000                // CMDBT_EVT_ST[28..16]
#define CB_INFRA_MBU_ST_CMDBT_EVT_ST_SHFT                      16
#define CB_INFRA_MBU_ST_FIFO_FULL_ADDR                         CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_FIFO_FULL_MASK                         0x00000200                // FIFO_FULL[9]
#define CB_INFRA_MBU_ST_FIFO_FULL_SHFT                         9
#define CB_INFRA_MBU_ST_FIFO_EMPTY_ADDR                        CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_FIFO_EMPTY_MASK                        0x00000100                // FIFO_EMPTY[8]
#define CB_INFRA_MBU_ST_FIFO_EMPTY_SHFT                        8
#define CB_INFRA_MBU_ST_CMDBT_BUSY_ADDR                        CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_CMDBT_BUSY_MASK                        0x00000010                // CMDBT_BUSY[4]
#define CB_INFRA_MBU_ST_CMDBT_BUSY_SHFT                        4
#define CB_INFRA_MBU_ST_CMDBT_ST_ADDR                          CB_INFRA_MBU_ST_ADDR
#define CB_INFRA_MBU_ST_CMDBT_ST_MASK                          0x0000000F                // CMDBT_ST[3..0]
#define CB_INFRA_MBU_ST_CMDBT_ST_SHFT                          0

/* =====================================================================================

  ---SW_PAUSE (0x74130000 + 0x084)---

    PAUSE[0]                     - (RW) Software pause the CMDBT instruction procedure
                                     0 : release pause
                                     1 : pause the procedure
                                     (The bit will be clear when each of TERMINATE/TRIGGER/RESET bits occurs)
    RESERVED1[31..1]             - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_SW_PAUSE_PAUSE_ADDR                       CB_INFRA_MBU_SW_PAUSE_ADDR
#define CB_INFRA_MBU_SW_PAUSE_PAUSE_MASK                       0x00000001                // PAUSE[0]
#define CB_INFRA_MBU_SW_PAUSE_PAUSE_SHFT                       0

/* =====================================================================================

  ---RESERVE_0 (0x74130000 + 0x09C)---

    FLAG_B0_SEL[7..0]            - (RW) monitor flag selection of conn_cmdbt_dbg0
    FLAG_B1_SEL[15..8]           - (RW) monitor flag selection of conn_cmdbt_dbg1
    DEBUG_B0_EN[16]              - (RW) monitor flag enable of conn_cmdbt_dbg0
    DEBUG_B1_EN[17]              - (RW) monitor flag enable of conn_cmdbt_dbg1
    HCLK_3X_MODE[18]             - (RW) HREADY ECO for AHB layer 4
    BURS_FLOWCTL[19]             - (RW) flow control of conn_cmdbt_brrs_fsm
    RESERVED20[29..20]           - (RO) Reserved bits
    REG_CG_DIS[30]               - (RW) Clock gating disable control of REG part
                                     0: enable clock gating cell of REG part
                                     1: disable clock gating cell of REG part
    LOG_CG_DIS[31]               - (RW) Clock gating disable control of logic part
                                     0: enable clock gating cell of logic part
                                     1: disable clock gating cell of logic part

 =====================================================================================*/
#define CB_INFRA_MBU_RESERVE_0_LOG_CG_DIS_ADDR                 CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_LOG_CG_DIS_MASK                 0x80000000                // LOG_CG_DIS[31]
#define CB_INFRA_MBU_RESERVE_0_LOG_CG_DIS_SHFT                 31
#define CB_INFRA_MBU_RESERVE_0_REG_CG_DIS_ADDR                 CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_REG_CG_DIS_MASK                 0x40000000                // REG_CG_DIS[30]
#define CB_INFRA_MBU_RESERVE_0_REG_CG_DIS_SHFT                 30
#define CB_INFRA_MBU_RESERVE_0_BURS_FLOWCTL_ADDR               CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_BURS_FLOWCTL_MASK               0x00080000                // BURS_FLOWCTL[19]
#define CB_INFRA_MBU_RESERVE_0_BURS_FLOWCTL_SHFT               19
#define CB_INFRA_MBU_RESERVE_0_HCLK_3X_MODE_ADDR               CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_HCLK_3X_MODE_MASK               0x00040000                // HCLK_3X_MODE[18]
#define CB_INFRA_MBU_RESERVE_0_HCLK_3X_MODE_SHFT               18
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B1_EN_ADDR                CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B1_EN_MASK                0x00020000                // DEBUG_B1_EN[17]
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B1_EN_SHFT                17
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B0_EN_ADDR                CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B0_EN_MASK                0x00010000                // DEBUG_B0_EN[16]
#define CB_INFRA_MBU_RESERVE_0_DEBUG_B0_EN_SHFT                16
#define CB_INFRA_MBU_RESERVE_0_FLAG_B1_SEL_ADDR                CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_FLAG_B1_SEL_MASK                0x0000FF00                // FLAG_B1_SEL[15..8]
#define CB_INFRA_MBU_RESERVE_0_FLAG_B1_SEL_SHFT                8
#define CB_INFRA_MBU_RESERVE_0_FLAG_B0_SEL_ADDR                CB_INFRA_MBU_RESERVE_0_ADDR
#define CB_INFRA_MBU_RESERVE_0_FLAG_B0_SEL_MASK                0x000000FF                // FLAG_B0_SEL[7..0]
#define CB_INFRA_MBU_RESERVE_0_FLAG_B0_SEL_SHFT                0

/* =====================================================================================

  ---OPD_TOP_H (0x74130000 + 0x100)---

    OPD_TOP_H[23..0]             - (RW) CMDBT operand for TOP write
                                     [23:16]: not used
                                     [15:0]: write address[31:16]
    RESERVED24[31..24]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_TOP_H_OPD_TOP_H_ADDR                  CB_INFRA_MBU_OPD_TOP_H_ADDR
#define CB_INFRA_MBU_OPD_TOP_H_OPD_TOP_H_MASK                  0x00FFFFFF                // OPD_TOP_H[23..0]
#define CB_INFRA_MBU_OPD_TOP_H_OPD_TOP_H_SHFT                  0

/* =====================================================================================

  ---OPD_TOP_L (0x74130000 + 0x104)---

    OPD_TOP_L[23..0]             - (RW) CMDBT operand for TOP write
                                     [23:16]: write address[15:8]
                                     [15:0]: write data[31:16]
    RESERVED24[31..24]           - (RO) Reserved bits

 =====================================================================================*/
#define CB_INFRA_MBU_OPD_TOP_L_OPD_TOP_L_ADDR                  CB_INFRA_MBU_OPD_TOP_L_ADDR
#define CB_INFRA_MBU_OPD_TOP_L_OPD_TOP_L_MASK                  0x00FFFFFF                // OPD_TOP_L[23..0]
#define CB_INFRA_MBU_OPD_TOP_L_OPD_TOP_L_SHFT                  0

/* =====================================================================================

  ---MBU_VECTOR_EVT (0x74130000 + 0x200)---

    MBU_VECTOR_EV[31..0]         - (RO) Vector events to trigger mbu
                                     [31:16]: msi_mir_int_b_bus
                                     [15]: pcie_clock_switch
                                     [14]: bus_clock_switch
                                     [13:10]: cb_infra_mbu_mailbox_int_b
                                     [9:6]: cb_dma_1_int_b
                                     [5:2]: cb_dma_0_int_b
                                     [1]: MET
                                     [0]: GPT

 =====================================================================================*/
#define CB_INFRA_MBU_MBU_VECTOR_EVT_MBU_VECTOR_EV_ADDR         CB_INFRA_MBU_MBU_VECTOR_EVT_ADDR
#define CB_INFRA_MBU_MBU_VECTOR_EVT_MBU_VECTOR_EV_MASK         0xFFFFFFFF                // MBU_VECTOR_EV[31..0]
#define CB_INFRA_MBU_MBU_VECTOR_EVT_MBU_VECTOR_EV_SHFT         0

/* =====================================================================================

  ---CR_VECTOR_EVENT_MASK (0x74130000 + 0x204)---

    CR_VECTOR_EVENT_MASK[31..0]  - (RW)  xxx 

 =====================================================================================*/
#define CB_INFRA_MBU_CR_VECTOR_EVENT_MASK_CR_VECTOR_EVENT_MASK_ADDR CB_INFRA_MBU_CR_VECTOR_EVENT_MASK_ADDR
#define CB_INFRA_MBU_CR_VECTOR_EVENT_MASK_CR_VECTOR_EVENT_MASK_MASK 0xFFFFFFFF                // CR_VECTOR_EVENT_MASK[31..0]
#define CB_INFRA_MBU_CR_VECTOR_EVENT_MASK_CR_VECTOR_EVENT_MASK_SHFT 0

#ifdef __cplusplus
}
#endif

#endif // __CB_INFRA_MBU_REGS_H__
