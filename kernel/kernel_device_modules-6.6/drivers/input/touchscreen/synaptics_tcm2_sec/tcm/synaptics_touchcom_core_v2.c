// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TCM touchscreen driver
 *
 * Copyright (C) 2017-2020 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file synaptics_touchcom_core_v2.c
 *
 * This file implements the TouchComm version 2 command-response protocol
 */

#include "synaptics_touchcom_core_dev.h"

#define BITS_IN_MESSAGE_HEADER (MESSAGE_HEADER_SIZE * 8)

#define HOST_PRIMARY (0)

#define COMMAND_RETRY_TIMES (5)

#define CHECK_PACKET_CRC

/**
 * @section: Header of TouchComm v2 Message Packet
 *
 * The 4-byte header in the TouchComm v2 packet
 */
struct tcm_v2_message_header {
	union {
		struct {
			unsigned char code;
			unsigned char length[2];
			unsigned char byte3;
		};
		unsigned char data[MESSAGE_HEADER_SIZE];
	};
};

/* helper to execute a tcm v2 command
 */
static int syna_tcm_v2_execute_cmd_request(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int payload_length);

/**
 * @section: Lookup table for checksum calculation
 *
 * @subsection: crc6_table
 *              lookup table for crc6 calculation
 *
 * @subsection: crc16_table
 *              lookup table for crc16 calculation
 */
static unsigned short crc6_table[16] = {
	   0,  268,  536,  788, 1072, 1340, 1576, 1828,
	2144, 2412, 2680, 2932, 3152, 3420, 3656, 3908
};

static unsigned short crc16_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/**
 * syna_tcm_v2_crc6()
 *
 * Calculate the crc-6 with polynomial for TouchCom v2 header.
 *
 * @param
 *    [ in] p:    byte array for the calculation
 *    [ in] bits: number of bits
 *
 * @return
 *    the crc-6 value
 */
static unsigned char syna_tcm_v2_crc6(unsigned char *p, unsigned int bits)
{
	unsigned short r = 0x003F << 2;
	unsigned short x;

	for (; bits > 8; bits -= 8) {
		r ^= *p++;
		r = (r << 4) ^ crc6_table[r >> 4];
		r = (r << 4) ^ crc6_table[r >> 4];
	}

	if (bits > 0) {
		x = *p;
		while (bits--) {
			if (x & 0x80)
				r ^= 0x80;

			x <<= 1;
			r <<= 1;
			if (r & 0x100)
				r ^= (0x03 << 2);
		}
	}

	return (unsigned char)((r >> 2) & 0x3F);
}
/**
 * syna_tcm_v2_crc6()
 *
 * Calculate the crc-16 for TouchCom v2 packet.
 *
 * @param
 *    [ in] p:   byte array for the calculation
 *    [ in] len: length in bytes
 *
 * @return
 *    the crc-16 value
 */
static unsigned short syna_tcm_v2_crc16(unsigned char *p, unsigned int len)
{
	unsigned short r = 0xFFFF;

	while (len--)
		r = (r << 8) ^ crc16_table[(r >> 8) ^ *p++];

	return r;
}

/**
 * syna_tcm_v2_set_max_read_size()
 *
 * Configure the max length for message reading.
 *
 * @param
 *    [ in] tcm_dev: the device handle
 *
 * @return
 *    none.
 */
static int syna_tcm_v2_set_max_read_size(struct tcm_dev *tcm_dev)
{
	int retval;
	unsigned int rd_size;
	struct tcm_identification_info *id_info;
	unsigned char data[2] = { 0 };

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	id_info = &tcm_dev->id_info;

	rd_size = syna_pal_le2_to_uint(id_info->max_read_size);

	if (rd_size == 0) {
		LOGE("Invalid max_read_length: %d\n", rd_size);
		return 0;
	}

	if (rd_size == tcm_dev->max_rd_size)
		return 0;

	tcm_dev->max_rd_size = MIN(rd_size, tcm_dev->max_rd_size);

	LOGD("max_rd_size = %d\n", tcm_dev->max_rd_size);

	data[0] = (unsigned char)tcm_dev->max_rd_size;
	data[1] = (unsigned char)(tcm_dev->max_rd_size >> 8);

	retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
			CMD_TCM2_SET_MAX_READ_LENGTH,
			data,
			sizeof(data));
	if (retval < 0) {
		LOGE("Fail to set max_read_length\n");
		return retval;
	}

	return 0;
}

/**
 * syna_tcm_v2_parse_idinfo()
 *
 * Copy the given data to the identification info structure
 * and parse the basic information, e.g. fw build id.
 *
 * @param
 *    [ in] tcm_dev:  the device handle
 *    [ in] data:     data buffer
 *    [ in] size:     size of given data buffer
 *    [ in] data_len: length of actual data
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_parse_idinfo(struct tcm_dev *tcm_dev,
		unsigned char *data, unsigned int size, unsigned int data_len)
{
	int retval;
	unsigned int wr_size = 0;
	unsigned int build_id = 0;
	struct tcm_identification_info *id_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	if ((!data) || (data_len == 0)) {
		LOGE("Invalid given data buffer\n");
		return _EINVAL;
	}

	id_info = &tcm_dev->id_info;

	retval = syna_pal_mem_cpy((unsigned char *)id_info,
			sizeof(struct tcm_identification_info),
			data,
			size,
			MIN(sizeof(*id_info), data_len));
	if (retval < 0) {
		LOGE("Fail to copy identification info\n");
		return retval;
	}

	build_id = syna_pal_le4_to_uint(id_info->build_id);

	wr_size = syna_pal_le2_to_uint(id_info->max_write_size);
	tcm_dev->max_wr_size = MIN(wr_size, WR_CHUNK_SIZE);
	if (tcm_dev->max_wr_size == 0) {
		tcm_dev->max_wr_size = wr_size;
		LOGD("max_wr_size = %d\n", tcm_dev->max_wr_size);
	}

	LOGI("TCM Fw mode: 0x%02x\n", id_info->mode);

	if (tcm_dev->packrat_number != build_id)
		tcm_dev->packrat_number = build_id;

	/* set up the max. reading length */
	retval = syna_tcm_v2_set_max_read_size(tcm_dev);
	if (retval < 0) {
		LOGE("Fail to setup the max reading length\n");
		return retval;
	}

	tcm_dev->dev_mode = id_info->mode;

	return 0;
}

/**
 * syna_tcm_v2_dispatch_report()
 *
 * Handle the TouchCom report packet being received.
 *
 * If it's an identify report, parse the identification packet and signal
 * the command completion just in case.
 * Otherwise, copy the data from internal buffer.in to internal buffer.report
 *
 * @param
 *    [ in] tcm_dev: the device handle
 *
 * @return
 *    none.
 */
static void syna_tcm_v2_dispatch_report(struct tcm_dev *tcm_dev)
{
	int retval;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_completion = &tcm_msg->cmd_completion;

	tcm_msg->report_code = tcm_msg->status_report_code;

	if (tcm_msg->payload_length == 0) {
		tcm_dev->resp_buf.data_length = tcm_msg->payload_length;
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
		goto exit;
	}

	/* The identify report may be resulted from reset or fw mode switching
	 */
	if (tcm_msg->report_code == REPORT_IDENTIFY) {

		syna_tcm_buf_lock(&tcm_msg->in);

		retval = syna_tcm_v2_parse_idinfo(tcm_dev,
				&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
				tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
				tcm_msg->payload_length);
		if (retval < 0) {
			LOGE("Fail to identify device\n");
			syna_tcm_buf_unlock(&tcm_msg->in);
			return;
		}

		syna_tcm_buf_unlock(&tcm_msg->in);

		/* in case, the identify info packet is caused by the command */
		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_BUSY) {
			switch (tcm_msg->command) {
			case CMD_RESET:
				LOGD("Reset by CMD_RESET\n");
				break;
			case CMD_REBOOT_TO_ROM_BOOTLOADER:
			case CMD_RUN_BOOTLOADER_FIRMWARE:
			case CMD_RUN_APPLICATION_FIRMWARE:
			case CMD_ENTER_PRODUCTION_TEST_MODE:
			case CMD_ROMBOOT_RUN_BOOTLOADER_FIRMWARE:
				tcm_msg->status_report_code = STATUS_OK;
				tcm_msg->response_code = STATUS_OK;
				ATOMIC_SET(tcm_msg->command_status,
					CMD_STATE_IDLE);
				syna_pal_completion_complete(cmd_completion);
				goto exit;
			default:
				LOGN("Device has been reset\n");
				ATOMIC_SET(tcm_msg->command_status,
					CMD_STATE_ERROR);
				syna_pal_completion_complete(cmd_completion);
				goto exit;
			}
		}
	}

	/* store the received report into the internal buffer.report */
	syna_tcm_buf_lock(&tcm_dev->report_buf);

	retval = syna_tcm_buf_alloc(&tcm_dev->report_buf,
			tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.report\n");
		syna_tcm_buf_unlock(&tcm_dev->report_buf);
		goto exit;
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_pal_mem_cpy(tcm_dev->report_buf.buf,
			tcm_dev->report_buf.buf_size,
			&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
			tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
			tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to copy payload to buf_report\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_tcm_buf_unlock(&tcm_dev->report_buf);
		goto exit;
	}

	tcm_dev->report_buf.data_length = tcm_msg->payload_length;

	syna_tcm_buf_unlock(&tcm_msg->in);
	syna_tcm_buf_unlock(&tcm_dev->report_buf);

exit:
	return;
}

/**
 * syna_tcm_v2_dispatch_response()
 *
 * Handle the response packet.
 *
 * Copy the data from internal buffer.in to internal buffer.resp,
 * and then signal the command completion.
 *
 * @param
 *    [ in] tcm_dev: the device handle
 *
 * @return
 *    none.
 */
static void syna_tcm_v2_dispatch_response(struct tcm_dev *tcm_dev)
{
	int retval;
	unsigned int resp_data_length;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_completion = &tcm_msg->cmd_completion;

	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_BUSY)
		return;

	resp_data_length = tcm_msg->payload_length;

	if (resp_data_length == 0) {
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);
		goto exit;
	}

	/* store the received report into the temporary buffer */
	syna_tcm_buf_lock(&tcm_dev->resp_buf);

	retval = syna_tcm_buf_alloc(&tcm_dev->resp_buf,
			resp_data_length + 1);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.resp\n");
		syna_tcm_buf_unlock(&tcm_dev->resp_buf);
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
		goto exit;
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_pal_mem_cpy(tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
			tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
			resp_data_length);
	if (retval < 0) {
		LOGE("Fail to copy payload to internal resp_buf\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		syna_tcm_buf_unlock(&tcm_dev->resp_buf);
		ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
		goto exit;
	}

	tcm_dev->resp_buf.data_length = resp_data_length;

	syna_tcm_buf_unlock(&tcm_msg->in);
	syna_tcm_buf_unlock(&tcm_dev->resp_buf);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);

exit:
	syna_pal_completion_complete(cmd_completion);
}

/**
 * syna_tcm_v2_read()
 *
 * Read in a TouchCom packet from device.
 * Checking the CRC is necessary to ensure a valid message received.
 *
 * @param
 *    [ in] tcm_dev:    the device handle
 *    [ in] rd_length:  number of reading bytes;
 *                     '0' means to read the message header only
 *    [out] buf:        pointer to a buffer which is stored the retrieved data
 *    [out] buf_size:   size of the buffer pointed
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_read(struct tcm_dev *tcm_dev, unsigned int rd_length,
		unsigned char **buf, unsigned int *buf_size)
{
	int retval;
	struct tcm_v2_message_header *header;
	int max_rd_size;
	int xfer_len;
	unsigned char crc6 = 0;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	max_rd_size = tcm_dev->max_rd_size;

	/* continued packet crc if containing payload data */
	xfer_len = (rd_length > 0) ? (rd_length + 2) : rd_length;
	xfer_len += sizeof(struct tcm_v2_message_header);

	if ((max_rd_size != 0) && (xfer_len > max_rd_size)) {
		LOGE("Invalid xfer length, len: %d, max_rd_size: %d\n",
			xfer_len, max_rd_size);
		tcm_msg->status_report_code = STATUS_INVALID;
		return _EINVAL;
	}

	syna_tcm_buf_lock(&tcm_msg->temp);

	/* allocate the internal temp buffer */
	retval = syna_tcm_buf_alloc(&tcm_msg->temp, xfer_len);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.temp\n");
		goto exit;
	}
	/* read data from the bus */
	retval = syna_tcm_read(tcm_dev,
			tcm_msg->temp.buf,
			xfer_len);
	if (retval < 0) {
		LOGE("Fail to read from device\n");
		goto exit;
	}

	header = (struct tcm_v2_message_header *)tcm_msg->temp.buf;

	/* check header crc always */
	crc6 = syna_tcm_v2_crc6(header->data, BITS_IN_MESSAGE_HEADER);
	if (crc6 != 0) {
		LOGE("Invalid header crc: 0x%02x\n", (header->byte3 & 0x3f));

		tcm_msg->status_report_code = STATUS_PACKET_CORRUPTED;
		goto exit;
	}

#ifdef CHECK_PACKET_CRC
	/* check packet crc */
	if (rd_length > 0) {
		if (syna_tcm_v2_crc16(&tcm_msg->temp.buf[0], xfer_len) != 0) {
			LOGE("Invalid packet crc: %02x %02x\n",
				tcm_msg->temp.buf[xfer_len - 2],
				tcm_msg->temp.buf[xfer_len - 1]);

			tcm_msg->status_report_code = STATUS_PACKET_CORRUPTED;
			goto exit;
		}
	}
#endif

	tcm_msg->status_report_code = header->code;

	tcm_msg->payload_length = syna_pal_le2_to_uint(header->length);

	if (tcm_msg->status_report_code != STATUS_IDLE)
		LOGD("Status code: 0x%02x, length: %d (%02x %02x %02x %02x)\n",
			tcm_msg->status_report_code, tcm_msg->payload_length,
			header->data[0], header->data[1], header->data[2],
			header->data[3]);

	*buf = tcm_msg->temp.buf;
	*buf_size = tcm_msg->temp.buf_size;

exit:
	syna_tcm_buf_unlock(&tcm_msg->temp);

	return retval;
}

/**
 * syna_tcm_v2_write()
 *
 * Construct the TouchCom v2 packet and send it to device.
 * Add 4-byte header at the beginning of a message and appended crc if needed.
 *
 * @param
 *    [ in] tcm_dev:     the device handle
 *    [ in] command:     command code
 *    [ in] payload:     data payload if any
 *    [ in] payload_len: length of data payload if have any
 *    [ in] resend:      flag for re-sending the packet
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_write(struct tcm_dev *tcm_dev, unsigned char command,
		unsigned char *payload, unsigned int payload_len, bool resend)
{
	int retval;
	struct tcm_v2_message_header *header;
	unsigned char bits = BITS_IN_MESSAGE_HEADER - 6;
	int xfer_len;
	int size = MESSAGE_HEADER_SIZE + payload_len;
	unsigned short crc16;
	int max_wr_size;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	max_wr_size = tcm_dev->max_wr_size;

	/* continued packet crc if containing payload data */
	xfer_len = (payload_len > 0) ? (payload_len + 2) : payload_len;
	xfer_len += sizeof(struct tcm_v2_message_header);

	if ((max_wr_size != 0) && (xfer_len > max_wr_size)) {
		LOGE("Invalid xfer length, len: %d, max_wr_size: %d\n",
			xfer_len, max_wr_size);
		tcm_msg->status_report_code = STATUS_INVALID;
		return _EINVAL;
	}

	syna_tcm_buf_lock(&tcm_msg->out);

	/* allocate the internal out buffer */
	retval = syna_tcm_buf_alloc(&tcm_msg->out, xfer_len);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf.out\n");
		goto exit;
	}

	/* construct packet header */
	header = (struct tcm_v2_message_header *)tcm_msg->out.buf;

	if (resend)
		tcm_msg->seq_toggle -= 1;

	header->code = command;
	header->length[0] = (unsigned char)payload_len;
	header->length[1] = (unsigned char)(payload_len >> 8);
	header->byte3 = ((HOST_PRIMARY & 0x01) << 7);
	header->byte3 |= ((tcm_msg->seq_toggle++ & 0x01) << 6);
	header->byte3 |= syna_tcm_v2_crc6(header->data, bits);

	/* copy payload, if any */
	if (payload_len) {
		retval = syna_pal_mem_cpy(
				&tcm_msg->out.buf[MESSAGE_HEADER_SIZE],
				tcm_msg->out.buf_size - MESSAGE_HEADER_SIZE,
				payload,
				payload_len,
				payload_len);
		if (retval < 0) {
			LOGE("Fail to copy payload data\n");
			goto exit;
		}

		/* append packet crc */
		crc16 = syna_tcm_v2_crc16(&tcm_msg->out.buf[0], size);
		tcm_msg->out.buf[size] = (unsigned char)((crc16 >> 8) & 0xFF);
		tcm_msg->out.buf[size + 1] = (unsigned char)(crc16 & 0xFF);
	}

	/* write command packet to the bus */
	retval = syna_tcm_write(tcm_dev,
			tcm_msg->out.buf,
			xfer_len);
	if (retval < 0) {
		LOGE("Fail to write to device\n");
		goto exit;
	}

exit:
	syna_tcm_buf_unlock(&tcm_msg->out);

	return retval;
}

/**
 * syna_tcm_v2_continued_read()
 *
 * Write a CMD_ACK to read in the remaining data payload continuously
 * until the end of data. All the retrieved data is appended to the
 * internal buffer.in.
 *
 * @param
 *    [ in] tcm_dev:  the device handle
 *    [ in] length:   remaining data length in bytes
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_continued_read(struct tcm_dev *tcm_dev,
		unsigned int length)
{
	int retval;
	unsigned char *tmp_buf;
	unsigned int tmp_buf_size;
	int retry_cnt = 0;
	unsigned int idx;
	unsigned int offset;
	unsigned int chunks;
	unsigned int chunk_space;
	unsigned int xfer_length;
	unsigned int total_length;
	unsigned int remaining_length;
	unsigned char command;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;

	/* continued read packet contains the header and its payload */
	total_length = MESSAGE_HEADER_SIZE + tcm_msg->payload_length;

	remaining_length = length;

	offset = tcm_msg->payload_length - length;

	syna_tcm_buf_lock(&tcm_msg->in);

	/* extend the internal buf_in if needed */
	retval = syna_tcm_buf_realloc(&tcm_msg->in,
			total_length + 1);
	if (retval < 0) {
		LOGE("Fail to allocate memory for internal buf_in\n");
		goto exit;
	}

	/* available space for payload = total chunk size - header - crc */
	chunk_space = tcm_dev->max_rd_size;
	if (chunk_space == 0)
		chunk_space = remaining_length;
	else
		chunk_space = chunk_space - (MESSAGE_HEADER_SIZE + 2);

	chunks = syna_pal_ceil_div(remaining_length, chunk_space);
	chunks = chunks == 0 ? 1 : chunks;

	offset += MESSAGE_HEADER_SIZE;

	/* send CMD_ACK for a continued read */
	command = CMD_TCM2_ACK;

	for (idx = 0; idx < chunks; idx++) {
retry:
		LOGD("Command: 0x%02x\n", command);

		/* construct the command packet */
		retval = syna_tcm_v2_write(tcm_dev,
				command,
				NULL,
				0,
				false);
		if (retval < 0) {
			LOGE("Fail to send CMD_TCM2_ACK in continued read\n");
			goto exit;
		}

		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

		/* read in the requested size of data */
		retval = syna_tcm_v2_read(tcm_dev,
				xfer_length,
				&tmp_buf,
				&tmp_buf_size);
		if (retval < 0) {
			LOGE("Fail to read %d bytes from device\n",
					xfer_length);
			goto exit;
		}

		/* If see an error, retry the previous read transaction
		 * Send RETRY instead of CMD_ACK
		 */
		if (tcm_msg->status_report_code == STATUS_PACKET_CORRUPTED) {
			if (retry_cnt > COMMAND_RETRY_TIMES) {
				LOGE("Continued read packet corrupted\n");
				goto exit;
			}

			retry_cnt += 1;
			command = CMD_TCM2_RETRY;

			LOGW("Read corrupted, retry %d\n", retry_cnt);
			goto retry;
		}

		retry_cnt = 0;
		command = CMD_TCM2_ACK;

		/* append data from temporary buffer to in_buf */
		syna_tcm_buf_lock(&tcm_msg->temp);

		/* copy data from internal buffer.temp to buffer.in */
		retval = syna_pal_mem_cpy(&tcm_msg->in.buf[offset],
				tcm_msg->in.buf_size - offset,
				&tmp_buf[MESSAGE_HEADER_SIZE],
				tmp_buf_size - MESSAGE_HEADER_SIZE,
				xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy payload to internal buf_in\n");
			syna_tcm_buf_unlock(&tcm_msg->temp);
			goto exit;
		}

		syna_tcm_buf_unlock(&tcm_msg->temp);

		remaining_length -= xfer_length;

		offset += xfer_length;
	}

	retval = 0;

exit:
	syna_tcm_buf_unlock(&tcm_msg->in);

	return retval;
}

/**
 * syna_tcm_v2_get_response()
 *
 * Read in the response packet from device.
 * If containing payload data, use continued_read() function and read the
 * remaining payload data.
 *
 * @param
 *    [ in] tcm_dev: the device handle
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_get_response(struct tcm_dev *tcm_dev)
{
	int retval;
	struct tcm_v2_message_header *header;
	unsigned char *tmp_buf;
	unsigned int tmp_buf_size;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;

	/* read in the message header at first */
	retval = syna_tcm_v2_read(tcm_dev,
			0,
			&tmp_buf,
			&tmp_buf_size);
	if (retval < 0) {
		LOGE("Fail to read message header from device\n");
		return retval;
	}

	/* error out once the response packet is corrupted */
	if (tcm_msg->status_report_code == STATUS_PACKET_CORRUPTED)
		return 0;


	/* allocate the required space = header + payload */
	syna_tcm_buf_lock(&tcm_msg->in);

	retval = syna_tcm_buf_alloc(&tcm_msg->in,
			MESSAGE_HEADER_SIZE + tcm_msg->payload_length);
	if (retval < 0) {
		LOGE("Fail to reallocate memory for for internal buf.in\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		return retval;
	}

	retval = syna_pal_mem_cpy(tcm_msg->in.buf,
			tcm_msg->in.buf_size,
			tmp_buf,
			tmp_buf_size,
			MESSAGE_HEADER_SIZE);
	if (retval < 0) {
		LOGE("Fail to copy data to internal buf_in\n");
		syna_tcm_buf_unlock(&tcm_msg->in);
		return retval;
	}

	syna_tcm_buf_unlock(&tcm_msg->in);

	/* read in payload, if any */
	if (tcm_msg->payload_length > 0) {

		retval = syna_tcm_v2_continued_read(tcm_dev,
				tcm_msg->payload_length);
		if (retval < 0) {
			LOGE("Fail to read in payload data, size: %d)\n",
				tcm_msg->payload_length);
			return retval;
		}
	}

	syna_tcm_buf_lock(&tcm_msg->in);

	header = (struct tcm_v2_message_header *)tcm_msg->in.buf;

	tcm_msg->payload_length = syna_pal_le2_to_uint(header->length);
	tcm_msg->status_report_code = header->code;

	syna_tcm_buf_unlock(&tcm_msg->in);

	return retval;
}

/**
 * syna_tcm_v2_send_cmd()
 *
 * Forward the given command and payload to syna_tcm_v2_write().
 *
 * @param
 *    [ in] tcm_dev:     the device handle
 *    [ in] command:     command code
 *    [ in] payload:     data payload if any
 *    [ in] payload_len: length of data payload if have any
 *    [ in] resend:      flag for re-sending the packet
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static inline int syna_tcm_v2_send_cmd(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int length, bool resend)
{
	int retval;

	retval = syna_tcm_v2_write(tcm_dev,
			command,
			payload,
			length,
			resend);
	if (retval < 0)
		LOGE("Fail to write Command 0x%02x to device\n", command);

	return retval;
}

/**
 * syna_tcm_v2_execute_cmd_request()
 *
 * Process the command message.
 * The helper is responsible for sending the given command and its payload,
 * to device. Once the total size of message is over the wr_chunk, divide
 * into continued writes
 *
 * In addition, the response to the command generated by the device will be
 * read in immediately.
 *
 * @param
 *    [ in] tcm_dev:        the device handle
 *    [ in] command:        command code
 *    [ in] payload:        data payload if any
 *    [ in] payload_length: length of payload in bytes
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_tcm_v2_execute_cmd_request(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int payload_length)
{
	int retval;
	unsigned int idx;
	unsigned int offset;
	unsigned int chunks;
	unsigned int xfer_length;
	unsigned int remaining_length;
	int retry_cnt = 0;
	unsigned int chunk_space;
	struct tcm_message_data_blob *tcm_msg = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	chunk_space = tcm_dev->max_wr_size;

	remaining_length = payload_length;

	/* available space for payload = total size - header - crc */
	if (chunk_space == 0)
		chunk_space = remaining_length;
	else
		chunk_space = chunk_space - (MESSAGE_HEADER_SIZE + 2);

	chunks = syna_pal_ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = 0;

	/* process the command message and handle the response
	 * to the command
	 */
	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space)
			xfer_length = chunk_space;
		else
			xfer_length = remaining_length;

retry:
		/* send command to device */
		command = (idx == 0) ? command : CMD_CONTINUE_WRITE;

		LOGD("Command: 0x%02x\n", command);

		retval = syna_tcm_v2_send_cmd(tcm_dev,
				command,
				&payload[offset],
				xfer_length,
				(retry_cnt > 0));
		if (retval < 0)
			goto exit;

		/* bus turnaround delay */
		syna_pal_sleep_us(TAT_DELAY_US_MIN, TAT_DELAY_US_MAX);

		/* get the response to the command immediately */
		retval = syna_tcm_v2_get_response(tcm_dev);
		if (retval < 0) {
			LOGE("Fail to get the response to command 0x%02x\n",
				command);
			goto exit;
		}

		/* check the response code */
		tcm_msg->response_code = tcm_msg->status_report_code;

		LOGD("Response code: 0x%x\n", tcm_msg->response_code);

		if (tcm_msg->status_report_code >= REPORT_IDENTIFY)
			goto next;

		switch (tcm_msg->status_report_code) {
		case STATUS_NO_REPORT_AVAILABLE:
		case STATUS_OK:
		case STATUS_ACK:
			retry_cnt = 0;
			break;
		case STATUS_PACKET_CORRUPTED:
		case STATUS_RETRY_REQUESTED:
			retry_cnt += 1;
			break;
		default:
			LOGE("Incorrect status code 0x%02x of command 0x%02x\n",
				tcm_msg->status_report_code, command);
			goto exit;
		}

		if (retry_cnt > 0) {
			if (command == CMD_RESET) {
				LOGE("Command CMD_RESET corrupted, exit\n");
				/* assume ACK and wait for interrupt assertion
				 * once the response of reset is corrupted
				 */
				tcm_msg->response_code = STATUS_ACK;
				goto exit;
			} else if (retry_cnt > COMMAND_RETRY_TIMES) {
				LOGE("Command 0x%02x corrupted\n", command);
				goto exit;
			}

			LOGN("Command 0x%02x, retry %d\n", command, retry_cnt);
			syna_pal_sleep_us(WR_DELAY_US_MIN, WR_DELAY_US_MAX);

			goto retry;
		}
next:
		offset += xfer_length;

		remaining_length -= xfer_length;

		if (chunks > 1)
			syna_pal_sleep_us(WR_DELAY_US_MIN, WR_DELAY_US_MAX);
	}

exit:
	return retval;
}

/**
 * syna_tcm_v2_read_message()
 *
 * Send a CMD_GET_REPORT to acquire a TouchCom v2 report packet from device.
 * Meanwhile, the retrieved data will be stored in the internal buffer.resp
 * or buffer.report.
 *
 * @param
 *    [ in] tcm_dev:            the device handle
 *    [out] status_report_code: status code or report code received
 *
 * @return
 *    0 or positive value on success; otherwise, on error.
 */
static int syna_tcm_v2_read_message(struct tcm_dev *tcm_dev,
		unsigned char *status_report_code)
{
	int retval;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_mutex_t *rw_mutex = NULL;
	syna_pal_completion_t *cmd_completion = NULL;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	rw_mutex = &tcm_msg->rw_mutex;
	cmd_completion = &tcm_msg->cmd_completion;

	if (status_report_code)
		*status_report_code = STATUS_INVALID;

	syna_pal_mutex_lock(rw_mutex);

	/* request a command */
	retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
			CMD_TCM2_GET_REPORT,
			NULL,
			0);
	if (retval < 0) {
		LOGE("Fail to send command CMD_TCM2_GET_REPORT\n");

		if (ATOMIC_GET(tcm_msg->command_status) == CMD_STATE_BUSY) {
			ATOMIC_SET(tcm_msg->command_status, CMD_STATE_ERROR);
			syna_pal_completion_complete(cmd_completion);
		}
		goto exit;
	}

	/* duplicate the data to external buffer */
	if (tcm_msg->payload_length > 0) {
		retval = syna_tcm_buf_alloc(&tcm_dev->external_buf,
				tcm_msg->payload_length);
		if (retval < 0) {
			LOGE("Fail to allocate memory, external_buf invalid\n");
			goto exit;
		} else {
			retval = syna_pal_mem_cpy(&tcm_dev->external_buf.buf[0],
				tcm_msg->payload_length,
				&tcm_msg->in.buf[MESSAGE_HEADER_SIZE],
				tcm_msg->in.buf_size - MESSAGE_HEADER_SIZE,
				tcm_msg->payload_length);
			if (retval < 0) {
				LOGE("Fail to copy data to external buffer\n");
				goto exit;
			}
		}
	}
	tcm_dev->external_buf.data_length = tcm_msg->payload_length;

	if (tcm_msg->response_code == STATUS_NO_REPORT_AVAILABLE)
		goto exit;

	/* process the retrieved packet */
	if (tcm_msg->status_report_code >= REPORT_IDENTIFY)
		syna_tcm_v2_dispatch_report(tcm_dev);
	else
		syna_tcm_v2_dispatch_response(tcm_dev);

	/* copy the status report code to caller */
	if (status_report_code)
		*status_report_code = tcm_msg->status_report_code;

exit:
	syna_pal_mutex_unlock(rw_mutex);

	return retval;
}

/**
 * syna_tcm_v2_write_message()
 *
 * Write message including command and its payload to TouchCom device.
 * Then, the response of the command generated by the device will be
 * read in and stored in internal buffer.resp.
 *
 * @param
 *    [ in] tcm_dev:          the device handle
 *    [ in] command:          TouchComm command
 *    [ in] payload:          data payload, if any
 *    [ in] payload_length:   length of data payload, if any
 *    [out] resp_code:        response code returned
 *    [ in] delay_ms_resp:    delay time for response reading.
 *                            '0' is in default; and
 *                            'FORCE_ATTN_DRIVEN' is to do reads in ISR
 *
 * @return
 *    0 or positive value on success; otherwise, on error.
 */
static int syna_tcm_v2_write_message(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int payload_length, unsigned char *resp_code,
		unsigned int delay_ms_resp)
{
	int retval;
	int timeout = 0;
	int polling_ms = 0;
	struct tcm_message_data_blob *tcm_msg = NULL;
	syna_pal_mutex_t *cmd_mutex = NULL;
	syna_pal_mutex_t *rw_mutex = NULL;
	syna_pal_completion_t *cmd_completion = NULL;
	bool has_irq_ctrl = false;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	tcm_msg = &tcm_dev->msg_data;
	cmd_mutex = &tcm_msg->cmd_mutex;
	rw_mutex = &tcm_msg->rw_mutex;
	cmd_completion = &tcm_msg->cmd_completion;

	if (resp_code)
		*resp_code = STATUS_INVALID;

	/* irq control is enabled only when the operations is implemented
	 * and the current status of irq is enabled.
	 * do not enable irq if it is disabled by someone.
	 */
	has_irq_ctrl = (bool)(tcm_dev->hw_if->ops_enable_irq != NULL);
	has_irq_ctrl &= tcm_dev->hw_if->bdata_attn.irq_enabled;

	/* disable irq control once the caller forces to use ATTN */
	has_irq_ctrl &= (delay_ms_resp != FORCE_ATTN_DRIVEN);

	/* disable the irq during the command execution */
	if (has_irq_ctrl && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, false);

	LOGD("write command: 0x%02x, payload size: %d\n",
		command, payload_length);

	syna_pal_mutex_lock(cmd_mutex);

	syna_pal_mutex_lock(rw_mutex);

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_BUSY);

	/* reset the command completion */
	syna_pal_completion_reset(cmd_completion);

	tcm_msg->command = command;

	/* request a command execution */
	retval = syna_tcm_v2_execute_cmd_request(tcm_dev,
			command,
			payload,
			payload_length);
	if (retval < 0) {
		LOGE("Fail to send command 0x%02x to device\n", command);
		goto exit;
	}

	syna_pal_mutex_unlock(rw_mutex);

	/* waiting for an interrupt asserted only if it is STATUS_ACK */
	if (tcm_msg->response_code != STATUS_ACK) {
		syna_tcm_v2_dispatch_response(tcm_dev);

		goto check_response;
	}

	/* polling the report generated by the command */
	timeout = 0;
	if (delay_ms_resp == FORCE_ATTN_DRIVEN)
		polling_ms = 1000;
	else
		polling_ms = (delay_ms_resp > 0) ? delay_ms_resp :
				CMD_RESPONSE_POLLING_DELAY_MS;

	do {
		/* wait for the command completion triggered by read_message */
		retval = syna_pal_completion_wait_for(cmd_completion,
				polling_ms);
		if (retval < 0)
			ATOMIC_SET(tcm_msg->command_status, CMD_STATE_BUSY);

		if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_IDLE)
			timeout += polling_ms + 10;
		else
			goto check_response;

		/* retrieve the message packet back */
		retval = syna_tcm_v2_read_message(tcm_dev, NULL);
		if (retval < 0)
			syna_pal_completion_reset(cmd_completion);

	} while (timeout < CMD_RESPONSE_TIMEOUT_MS);

	if (timeout >= CMD_RESPONSE_TIMEOUT_MS) {
		LOGE("Timed out waiting for payload of command 0x%02x\n",
			command);
		retval = _ETIMEDOUT;
		goto exit;
	}

check_response:
	if (ATOMIC_GET(tcm_msg->command_status) != CMD_STATE_IDLE) {
		LOGE("Fail to get valid response of command 0x%02x\n",
			command);
		retval = _EIO;
		goto exit;
	}

	/* copy response code to the caller */
	if (resp_code)
		*resp_code = tcm_msg->response_code;

	if (tcm_msg->response_code != STATUS_OK) {
		LOGE("Error code 0x%02x of command 0x%02x\n",
			tcm_msg->response_code, tcm_msg->command);
		retval = _EIO;
	} else {
		retval = 0;
	}

exit:
	tcm_msg->command = CMD_NONE;

	ATOMIC_SET(tcm_msg->command_status, CMD_STATE_IDLE);

	syna_pal_mutex_unlock(cmd_mutex);

	/* recovery the irq at the end of command execution */
	if (has_irq_ctrl && tcm_dev->hw_if->ops_enable_irq)
		tcm_dev->hw_if->ops_enable_irq(tcm_dev->hw_if, true);

	return retval;
}

 /**
  * syna_tcm_v2_detect()
  *
  * For TouchCom v2 protocol, the given data must have a valid crc-6 at the end.
  * If so, send an identify command to identify the device.
  *
  * @param
  *    [ in] tcm_dev: the device handle
  *    [ in] data:    raw 4-byte data
  *    [ in] size:    length of input data in bytes
  *
  * @return
  *    on success, 0 or positive value; otherwise, negative value on error.
  */
int syna_tcm_v2_detect(struct tcm_dev *tcm_dev, unsigned char *data,
		unsigned int size)
{
	int retval;
	unsigned char resp_code = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return _EINVAL;
	}

	if ((!data) || (size < MESSAGE_HEADER_SIZE)) {
		LOGE("Invalid parameters\n");
		return _EINVAL;
	}

	if (syna_tcm_v2_crc6(data, BITS_IN_MESSAGE_HEADER) != 0)
		return _ENODEV;

	/* send an identify command to identify the device */
	retval = syna_tcm_v2_write_message(tcm_dev,
			CMD_IDENTIFY,
			NULL,
			0,
			&resp_code,
			0);
	if (retval < 0) {
		LOGE("Fail to get identification info from device\n");
		return retval;
	}

	/* expose the read / write operations */
	tcm_dev->read_message = syna_tcm_v2_read_message;
	tcm_dev->write_message = syna_tcm_v2_write_message;

	return retval;
}
