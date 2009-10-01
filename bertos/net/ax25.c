/**
 * \file
 * <!--
 * This file is part of BeRTOS.
 *
 * Bertos is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 *
 * Copyright 2009 Develer S.r.l. (http://www.develer.com/)
 *
 * -->
 * \brief Simple AX25 data link layer implementation.
 *
 * For now, only UI frames without any Layer 3 protocol are handled.
 * This however is enough to send/receive APRS packets.
 *
 * \version $Id$
 * \author Francesco Sacchi <batt@develer.com>
 *
 */

#include "ax25.h"
#include "cfg/cfg_ax25.h"

#include <algo/crc_ccitt.h>

#define LOG_LEVEL  AX25_LOG_LEVEL
#define LOG_FORMAT AX25_LOG_FORMAT
#include <cfg/log.h>

#include <string.h> //memset

#define DECODE_CALL(buf, addr) \
	for (unsigned i = 0; i < sizeof((addr)); i++) \
	{ \
		ASSERT(!(*(buf) & 0x01)); \
		(addr)[i] = *(buf)++ >> 1; \
	}

static void ax25_decode(AX25Ctx *ctx)
{
	AX25Msg msg;
	uint8_t *buf = ctx->buf;

	DECODE_CALL(buf, msg.dst.call);
	ASSERT(!(*buf & 0x01));
	msg.dst.ssid = (*buf++ >> 1) & 0x0F;

	DECODE_CALL(buf, msg.src.call);
	msg.src.ssid = (*buf >> 1) & 0x0F;

	LOG_INFO("SRC[%.6s-%d], DST[%.6s-%d]\n", msg.src.call, msg.src.ssid, msg.dst.call, msg.dst.ssid);

	/* Repeater addresses */
	#if CONFIG_AX25_RPT_LST
		for (msg.rpt_cnt = 0; !(*buf++ & 0x01) && (msg.rpt_cnt < countof(msg.rpt_lst)); msg.rpt_cnt++)
		{
			DECODE_CALL(buf, msg.rpt_lst[msg.rpt_cnt].call);
			msg.rpt_lst[msg.rpt_cnt].ssid = (*buf >> 1) & 0x0F;
			LOG_INFO("RPT%d[%.6s-%d]\n", msg.rpt_cnt, msg.rpt_lst[msg.rpt_cnt].call, msg.rpt_lst[msg.rpt_cnt].ssid);
		}
	#else
		while (!(*buf++ & 0x01))
		{
			char rpt[6];
			uint8_t ssid;
			DECODE_CALL(buf, rpt);
			ssid = (*buf >> 1) & 0x0F;
			LOG_INFO("RPT[%.6s-%d]\n", rpt, ssid);
		}
	#endif

	msg.ctrl = *buf++;
	if (msg.ctrl != AX25_CTRL_UI)
	{
		LOG_INFO("Only UI frames are handled, got [%02X]\n", msg.ctrl);
		return;
	}

	msg.pid = *buf++;
	if (msg.pid != AX25_PID_NOLAYER3)
	{
		LOG_INFO("Only frames without layer3 protocol are handled, got [%02X]\n", msg.pid);
		return;
	}

	msg.len = ctx->frm_len - 2 - (buf - ctx->buf);
	msg.info = buf;
	LOG_INFO("DATA[%.*s]\n", msg.len, msg.info);

	if (ctx->hook)
		ctx->hook(&msg);
}

void ax25_poll(AX25Ctx *ctx)
{
	int c;

	while ((c = kfile_getc(ctx->ch)) != EOF)
	{
		if (!ctx->escape && c == HDLC_FLAG)
		{
			LOG_INFO("Frame start\n");
			if (ctx->frm_len >= AX25_MIN_FRAME_LEN)
			{
				if (ctx->crc_in == AX25_CRC_CORRECT)
				{
					LOG_INFO("Frame found!\n");
					ax25_decode(ctx);
				}
				else
				{
					LOG_INFO("CRC error, computed [%04X]\n", ctx->crc_in);
				}
			}
			ctx->sync = true;
			ctx->crc_in = CRC_CCITT_INIT_VAL;
			ctx->frm_len = 0;
			continue;
		}

		if (!ctx->escape && c == HDLC_RESET)
		{
			LOG_INFO("HDLC reset\n");
			ctx->sync = false;
			continue;
		}

		if (!ctx->escape && c == AX25_ESC)
		{
			ctx->escape = true;
			continue;
		}

		if (ctx->sync)
		{
			if (ctx->frm_len < CONFIG_AX25_FRAME_BUF_LEN)
			{
				ctx->buf[ctx->frm_len++] = c;
				ctx->crc_in = updcrc_ccitt(c, ctx->crc_in);
			}
			else
			{
				LOG_INFO("Buffer overrun");
				ctx->sync = false;
			}
		}
		ctx->escape = false;
	}

	if (kfile_error(ctx->ch))
	{
		LOG_ERR("Channel error [%04x]\n", kfile_error(ctx->ch));
		kfile_clearerr(ctx->ch);
	}
}

void ax25_init(AX25Ctx *ctx, KFile *channel, ax25_callback_t hook)
{
	ASSERT(ctx);
	ASSERT(channel);

	memset(ctx, 0, sizeof(*ctx));
	ctx->ch = channel;
	ctx->hook = hook;
	ctx->crc_in = ctx->crc_out = CRC_CCITT_INIT_VAL;
}