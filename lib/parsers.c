/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

#ifdef WIN32
#include <io.h>
#endif

static const struct lws_tokens lws_tokens[WSI_TOKEN_COUNT] = {

	/* win32 can't do C99 */

/*	[WSI_TOKEN_GET_URI]	=	*/{ "GET ",			 4 },
/*	[WSI_TOKEN_HOST]	=	*/{ "Host:",			 5 },
/*	[WSI_TOKEN_CONNECTION]	=	*/{ "Connection:",		11 },
/*	[WSI_TOKEN_KEY1]	=	*/{ "Sec-WebSocket-Key1:",	19 },
/*	[WSI_TOKEN_KEY2]	=	*/{ "Sec-WebSocket-Key2:",	19 },
/*	[WSI_TOKEN_PROTOCOL]	=	*/{ "Sec-WebSocket-Protocol:",	23 },
/*	[WSI_TOKEN_UPGRADE]	=	*/{ "Upgrade:",			 8 },
/*	[WSI_TOKEN_ORIGIN]	=	*/{ "Origin:",			 7 },
/*	[WSI_TOKEN_DRAFT]	=	*/{ "Sec-WebSocket-Draft:",	20 },
/*	[WSI_TOKEN_CHALLENGE]	=	*/{ "\x0d\x0a",			 2 },

/*	[WSI_TOKEN_KEY]		=	*/{ "Sec-WebSocket-Key:",	18 },
/*	[WSI_TOKEN_VERSION]	=	*/{ "Sec-WebSocket-Version:",	22 },
/*	[WSI_TOKEN_SWORIGIN]=		*/{ "Sec-WebSocket-Origin:",	21 },

/*	[WSI_TOKEN_EXTENSIONS]	=	*/{ "Sec-WebSocket-Extensions:", 25 },

/*	[WSI_TOKEN_ACCEPT]	=	*/{ "Sec-WebSocket-Accept:",	21 },
/*	[WSI_TOKEN_NONCE]	=	*/{ "Sec-WebSocket-Nonce:",	20 },
/*	[WSI_TOKEN_HTTP]	=	*/{ "HTTP/1.1 ",		 9 },
/*	[WSI_TOKEN_MUXURL]	=	*/{ "",		 -1 },

};

int libwebsocket_parse(struct libwebsocket *wsi, unsigned char c)
{
	int n;

	switch (wsi->parser_state) {
	case WSI_TOKEN_GET_URI:
	case WSI_TOKEN_HOST:
	case WSI_TOKEN_CONNECTION:
	case WSI_TOKEN_KEY1:
	case WSI_TOKEN_KEY2:
	case WSI_TOKEN_PROTOCOL:
	case WSI_TOKEN_UPGRADE:
	case WSI_TOKEN_ORIGIN:
	case WSI_TOKEN_SWORIGIN:
	case WSI_TOKEN_DRAFT:
	case WSI_TOKEN_CHALLENGE:
	case WSI_TOKEN_KEY:
	case WSI_TOKEN_VERSION:
	case WSI_TOKEN_ACCEPT:
	case WSI_TOKEN_NONCE:
	case WSI_TOKEN_EXTENSIONS:
	case WSI_TOKEN_HTTP:
	case WSI_TOKEN_MUXURL:

		lwsl_parser("WSI_TOKEN_(%d) '%c'\n", wsi->parser_state, c);

		/* collect into malloc'd buffers */
		/* optional space swallow */
		if (!wsi->utf8_token[wsi->parser_state].token_len && c == ' ')
			break;

		/* special case space terminator for get-uri */
		if (wsi->parser_state == WSI_TOKEN_GET_URI && c == ' ') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
//			lwsl_parser("uri '%s'\n", wsi->utf8_token[wsi->parser_state].token);
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}

		/* allocate appropriate memory */
		if (wsi->utf8_token[wsi->parser_state].token_len ==
						   wsi->current_alloc_len - 1) {
			/* need to extend */
			wsi->current_alloc_len += LWS_ADDITIONAL_HDR_ALLOC;
			if (wsi->current_alloc_len >= LWS_MAX_HEADER_LEN) {
				/* it's waaay to much payload, fail it */
				strcpy(wsi->utf8_token[wsi->parser_state].token,
				   "!!! Length exceeded maximum supported !!!");
				wsi->parser_state = WSI_TOKEN_SKIPPING;
				break;
			}
			wsi->utf8_token[wsi->parser_state].token = (char *)
			       realloc(wsi->utf8_token[wsi->parser_state].token,
							wsi->current_alloc_len);
			if (wsi->utf8_token[wsi->parser_state].token == NULL) {
				lwsl_err("Out of mem\n");
				return -1;
			}
		}

		/* bail at EOL */
		if (wsi->parser_state != WSI_TOKEN_CHALLENGE && c == '\x0d') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
			lwsl_parser("*\n");
			break;
		}

		wsi->utf8_token[wsi->parser_state].token[
			    wsi->utf8_token[wsi->parser_state].token_len++] = c;

		/* per-protocol end of headers management */

		if (wsi->parser_state != WSI_TOKEN_CHALLENGE)
			break;

		/* -76 has no version header ... server */
		if (!wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
		   wsi->mode != LWS_CONNMODE_WS_CLIENT_WAITING_SERVER_REPLY &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* -76 has no version header ... client */
		if (!wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
		   wsi->mode == LWS_CONNMODE_WS_CLIENT_WAITING_SERVER_REPLY &&
			wsi->utf8_token[wsi->parser_state].token_len != 16)
			break;

		/* <= 03 has old handshake with version header needs 8 bytes */
		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			 atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) < 4 &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* no payload challenge in 01 + */

		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			   atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) > 0) {
			wsi->utf8_token[WSI_TOKEN_CHALLENGE].token_len = 0;
			free(wsi->utf8_token[WSI_TOKEN_CHALLENGE].token);
			wsi->utf8_token[WSI_TOKEN_CHALLENGE].token = NULL;
		}

		/* For any supported protocol we have enough payload */

		lwsl_parser("Setting WSI_PARSING_COMPLETE\n");
		wsi->parser_state = WSI_PARSING_COMPLETE;
		break;

	case WSI_INIT_TOKEN_MUXURL:
		wsi->parser_state = WSI_TOKEN_MUXURL;
		wsi->current_alloc_len = LWS_INITIAL_HDR_ALLOC;

		wsi->utf8_token[wsi->parser_state].token = (char *)
					 malloc(wsi->current_alloc_len);
		if (wsi->utf8_token[wsi->parser_state].token == NULL) {
			lwsl_err("Out of mem\n");
			return -1;
		}
		wsi->utf8_token[wsi->parser_state].token_len = 0;
		break;

		/* collecting and checking a name part */
	case WSI_TOKEN_NAME_PART:
		lwsl_parser("WSI_TOKEN_NAME_PART '%c'\n", c);

		if (wsi->name_buffer_pos == sizeof(wsi->name_buffer) - 1) {
			/* name bigger than we can handle, skip until next */
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}
		wsi->name_buffer[wsi->name_buffer_pos++] = c;
		wsi->name_buffer[wsi->name_buffer_pos] = '\0';

		for (n = 0; n < WSI_TOKEN_COUNT; n++) {
			if (wsi->name_buffer_pos != lws_tokens[n].token_len)
				continue;
			if (strcasecmp(lws_tokens[n].token, wsi->name_buffer))
				continue;
			lwsl_parser("known hdr '%s'\n", wsi->name_buffer);

			/*
			 * WSORIGIN is protocol equiv to ORIGIN,
			 * JWebSocket likes to send it, map to ORIGIN
			 */
			if (n == WSI_TOKEN_SWORIGIN)
				n = WSI_TOKEN_ORIGIN;

			wsi->parser_state = (enum lws_token_indexes) (WSI_TOKEN_GET_URI + n);

			n = WSI_TOKEN_COUNT;

			/*  If the header has been seen already, just append */
			if (wsi->utf8_token[wsi->parser_state].token)
				continue;

			wsi->current_alloc_len = LWS_INITIAL_HDR_ALLOC;
			wsi->utf8_token[wsi->parser_state].token = (char *)
						 malloc(wsi->current_alloc_len);
			if (wsi->utf8_token[wsi->parser_state].token == NULL) {
				lwsl_err("Out of mem\n");
				return -1;
			}
			wsi->utf8_token[wsi->parser_state].token_len = 0;
		}

		/* colon delimiter means we just don't know this name */

		if (wsi->parser_state == WSI_TOKEN_NAME_PART) {
			if (c == ':') {
				lwsl_parser("skipping unknown header '%s'\n",
							  wsi->name_buffer);
				wsi->parser_state = WSI_TOKEN_SKIPPING;
				break;
			}

			if (c == ' ' &&
				!wsi->utf8_token[WSI_TOKEN_GET_URI].token_len) {
				lwsl_parser("unknown method '%s'\n",
							  wsi->name_buffer);
				wsi->parser_state = WSI_TOKEN_GET_URI;
				wsi->current_alloc_len = LWS_INITIAL_HDR_ALLOC;
				wsi->utf8_token[WSI_TOKEN_GET_URI].token =
					(char *)malloc(wsi->current_alloc_len);
				if (wsi->utf8_token[WSI_TOKEN_GET_URI].token == NULL) {
					lwsl_err("Out of mem\n");
					return -1;
				}
				break;
			}
		}

		if (wsi->parser_state != WSI_TOKEN_CHALLENGE)
			break;

		/* don't look for payload when it can just be http headers */

		if (!wsi->utf8_token[WSI_TOKEN_UPGRADE].token_len) {
			/* they're HTTP headers, not websocket upgrade! */
			lwsl_parser("Setting WSI_PARSING_COMPLETE "
							 "from http headers\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}

		/* 04 version has no packet content after end of hdrs */

		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			 atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) >= 4) {
			lwsl_parser("04 header completed\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
			wsi->utf8_token[WSI_TOKEN_CHALLENGE].token_len = 0;
			free(wsi->utf8_token[WSI_TOKEN_CHALLENGE].token);
			wsi->utf8_token[WSI_TOKEN_CHALLENGE].token = NULL;
		}

		/* client parser? */

		if (wsi->ietf_spec_revision >= 4) {
			lwsl_parser("04 header completed\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}

		break;

		/* skipping arg part of a name we didn't recognize */
	case WSI_TOKEN_SKIPPING:
		lwsl_parser("WSI_TOKEN_SKIPPING '%c'\n", c);
		if (c == '\x0d')
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
		break;
	case WSI_TOKEN_SKIPPING_SAW_CR:
		lwsl_parser("WSI_TOKEN_SKIPPING_SAW_CR '%c'\n", c);
		if (c == '\x0a')
			wsi->parser_state = WSI_TOKEN_NAME_PART;
		else
			wsi->parser_state = WSI_TOKEN_SKIPPING;
		wsi->name_buffer_pos = 0;
		break;
		/* we're done, ignore anything else */
	case WSI_PARSING_COMPLETE:
		lwsl_parser("WSI_PARSING_COMPLETE '%c'\n", c);
		break;

	default:	/* keep gcc happy */
		break;
	}

	return 0;
}

unsigned char
xor_no_mask(struct libwebsocket *wsi, unsigned char c)
{
	return c;
}

unsigned char
xor_mask_04(struct libwebsocket *wsi, unsigned char c)
{
	c ^= wsi->masking_key_04[wsi->frame_mask_index++];
	if (wsi->frame_mask_index == 20)
		wsi->frame_mask_index = 0;

	return c;
}

unsigned char
xor_mask_05(struct libwebsocket *wsi, unsigned char c)
{
	return c ^ wsi->frame_masking_nonce_04[(wsi->frame_mask_index++) & 3];
}



int
libwebsocket_rx_sm(struct libwebsocket *wsi, unsigned char c)
{
	int n;
	unsigned char buf[20 + 4];
	struct lws_tokens eff_buf;
	int handled;
	int m;

#if 0
	lwsl_debug("RX: %02X ", c);
#endif

	switch (wsi->lws_rx_parse_state) {
	case LWS_RXPS_NEW:

		switch (wsi->ietf_spec_revision) {
		/* Firefox 4.0b6 likes this as of 30 Oct 2010 */
		case 0:
			if (c == 0xff)
				wsi->lws_rx_parse_state = LWS_RXPS_SEEN_76_FF;
			if (c == 0) {
				wsi->lws_rx_parse_state =
						       LWS_RXPS_EAT_UNTIL_76_FF;
				wsi->rx_user_buffer_head = 0;
			}
			break;
		case 4:
		case 5:
		case 6:
			wsi->all_zero_nonce = 1;
			wsi->frame_masking_nonce_04[0] = c;
			if (c)
				wsi->all_zero_nonce = 0;
			wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_1;
			break;
		case 7:
		case 8:
		case 13:
			/*
			 * no prepended frame key any more
			 */
			wsi->all_zero_nonce = 1;
			goto handle_first;

		default:
			lwsl_warn("libwebsocket_rx_sm doesn't know "
			    "about spec version %d\n", wsi->ietf_spec_revision);
			break;
		}
		break;
	case LWS_RXPS_04_MASK_NONCE_1:
		wsi->frame_masking_nonce_04[1] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_2;
		break;
	case LWS_RXPS_04_MASK_NONCE_2:
		wsi->frame_masking_nonce_04[2] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_3;
		break;
	case LWS_RXPS_04_MASK_NONCE_3:
		wsi->frame_masking_nonce_04[3] = c;
		if (c)
			wsi->all_zero_nonce = 0;

		if (wsi->protocol->owning_server->options &
					   LWS_SERVER_OPTION_DEFEAT_CLIENT_MASK)
			goto post_mask;

		if (wsi->ietf_spec_revision > 4)
			goto post_sha1;

		/*
		 * we are able to compute the frame key now
		 * it's a SHA1 of ( frame nonce we were just sent, concatenated
		 * with the connection masking key we computed at handshake
		 * time ) -- yeah every frame from the client invokes a SHA1
		 * for no real reason so much for lightweight.
		 */

		buf[0] = wsi->frame_masking_nonce_04[0];
		buf[1] = wsi->frame_masking_nonce_04[1];
		buf[2] = wsi->frame_masking_nonce_04[2];
		buf[3] = wsi->frame_masking_nonce_04[3];

		memcpy(buf + 4, wsi->masking_key_04, 20);

		/*
		 * wsi->frame_mask_04 will be our recirculating 20-byte XOR key
		 * for this frame
		 */

		SHA1((unsigned char *)buf, 4 + 20, wsi->frame_mask_04);

post_sha1:

		/*
		 * start from the zero'th byte in the XOR key buffer since
		 * this is the start of a frame with a new key
		 */

		wsi->frame_mask_index = 0;

post_mask:
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_1;
		break;

	/*
	 *  04 logical framing from the spec (all this is masked when incoming
	 *  and has to be unmasked)
	 *
	 * We ignore the possibility of extension data because we don't
	 * negotiate any extensions at the moment.
	 *
	 *    0                   1                   2                   3
	 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *   +-+-+-+-+-------+-+-------------+-------------------------------+
	 *   |F|R|R|R| opcode|R| Payload len |    Extended payload length    |
	 *   |I|S|S|S|  (4)  |S|     (7)     |             (16/63)           |
	 *   |N|V|V|V|       |V|             |   (if payload len==126/127)   |
	 *   | |1|2|3|       |4|             |                               |
	 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 *   |     Extended payload length continued, if payload len == 127  |
	 *   + - - - - - - - - - - - - - - - +-------------------------------+
	 *   |                               |         Extension data        |
	 *   +-------------------------------+ - - - - - - - - - - - - - - - +
	 *   :                                                               :
	 *   +---------------------------------------------------------------+
	 *   :                       Application data                        :
	 *   +---------------------------------------------------------------+
	 *
	 *  We pass payload through to userland as soon as we get it, ignoring
	 *  FIN.  It's up to userland to buffer it up if it wants to see a
	 *  whole unfragmented block of the original size (which may be up to
	 *  2^63 long!)
	 */

	case LWS_RXPS_04_FRAME_HDR_1:
handle_first:

		/*
		 * 04 spec defines the opcode like this: (1, 2, and 3 are
		 * "control frame" opcodes which may not be fragmented or
		 * have size larger than 126)
		 *
		 *       frame-opcode           =
		 *	       %x0 ; continuation frame
		 *		/ %x1 ; connection close
		 *		/ %x2 ; ping
		 *		/ %x3 ; pong
		 *		/ %x4 ; text frame
		 *		/ %x5 ; binary frame
		 *		/ %x6-F ; reserved
		 *
		 *		FIN (b7)
		 */

		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);

		/* translate all incoming opcodes into v7+ map */
		if (wsi->ietf_spec_revision < 7)
			switch (c & 0xf) {
			case LWS_WS_OPCODE_04__CONTINUATION:
				wsi->opcode = LWS_WS_OPCODE_07__CONTINUATION;
				break;
			case LWS_WS_OPCODE_04__CLOSE:
				wsi->opcode = LWS_WS_OPCODE_07__CLOSE;
				break;
			case LWS_WS_OPCODE_04__PING:
				wsi->opcode = LWS_WS_OPCODE_07__PING;
				break;
			case LWS_WS_OPCODE_04__PONG:
				wsi->opcode = LWS_WS_OPCODE_07__PONG;
				break;
			case LWS_WS_OPCODE_04__TEXT_FRAME:
				wsi->opcode = LWS_WS_OPCODE_07__TEXT_FRAME;
				break;
			case LWS_WS_OPCODE_04__BINARY_FRAME:
				wsi->opcode = LWS_WS_OPCODE_07__BINARY_FRAME;
				break;
			default:
				lwsl_warn("reserved opcodes not "
						    "usable pre v7 protocol\n");
				return -1;
			}
		else
			wsi->opcode = c & 0xf;
		wsi->rsv = (c & 0x70);
		wsi->final = !!((c >> 7) & 1);

		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN:

		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);

		if ((c & 0x80) && wsi->ietf_spec_revision < 7) {
			lwsl_warn("Frame has extensions set illegally 2\n");
			/* kill the connection */
			return -1;
		}

		wsi->this_frame_masked = !!(c & 0x80);

		switch (c & 0x7f) {
		case 126:
			/* control frames are not allowed to have big lengths */
			if (wsi->opcode & 8)
				goto illegal_ctl_length;

			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_2;
			break;
		case 127:
			/* control frames are not allowed to have big lengths */
			if (wsi->opcode & 8)
				goto illegal_ctl_length;

			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_8;
			break;
		default:
			wsi->rx_packet_length = c & 0x7f;
			if (wsi->this_frame_masked)
				wsi->lws_rx_parse_state =
						LWS_RXPS_07_COLLECT_FRAME_KEY_1;
			else
				wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
			break;
		}
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_2:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);

		wsi->rx_packet_length = c << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_1:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);

		wsi->rx_packet_length |= c;
		if (wsi->this_frame_masked)
			wsi->lws_rx_parse_state =
					LWS_RXPS_07_COLLECT_FRAME_KEY_1;
		else
			wsi->lws_rx_parse_state =
				LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_8:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
		if (c & 0x80) {
			lwsl_warn("b63 of length must be zero\n");
			/* kill the connection */
			return -1;
		}
#if defined __LP64__
		wsi->rx_packet_length = ((size_t)c) << 56;
#else
		wsi->rx_packet_length = 0;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_7;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_7:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
#if defined __LP64__
		wsi->rx_packet_length |= ((size_t)c) << 48;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_6;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_6:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
#if defined __LP64__
		wsi->rx_packet_length |= ((size_t)c) << 40;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_5;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_5:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
#if defined __LP64__
		wsi->rx_packet_length |= ((size_t)c) << 32;
#endif
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_4;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_4:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
		wsi->rx_packet_length |= ((size_t)c) << 24;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_3;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_3:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
		wsi->rx_packet_length |= ((size_t)c) << 16;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_2;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_2:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
		wsi->rx_packet_length |= ((size_t)c) << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_1:
		if (wsi->ietf_spec_revision < 7)
			c = wsi->xor_mask(wsi, c);
		wsi->rx_packet_length |= ((size_t)c);
		if (wsi->this_frame_masked)
			wsi->lws_rx_parse_state =
					LWS_RXPS_07_COLLECT_FRAME_KEY_1;
		else
			wsi->lws_rx_parse_state =
				LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_EAT_UNTIL_76_FF:

		if (c == 0xff) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto issue;
		}
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
					      (wsi->rx_user_buffer_head++)] = c;

		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
issue:
		if (wsi->protocol->callback)
			user_callback_handle_rxflow(wsi->protocol->callback,
			  wsi->protocol->owning_server,
			  wsi, LWS_CALLBACK_RECEIVE,
			  wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
			  wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	case LWS_RXPS_SEEN_76_FF:
		if (c)
			break;

		lwsl_parser("Seen that client is requesting "
				"a v76 close, sending ack\n");
		buf[0] = 0xff;
		buf[1] = 0;
		n = libwebsocket_write(wsi, buf, 2, LWS_WRITE_HTTP);
		if (n < 0) {
			lwsl_warn("ERROR writing to socket");
			return -1;
		}
		lwsl_parser("  v76 close ack sent, server closing skt\n");
		/* returning < 0 will get it closed in parent */
		return -1;

	case LWS_RXPS_PULLING_76_LENGTH:
		break;


	case LWS_RXPS_07_COLLECT_FRAME_KEY_1:
		wsi->frame_masking_nonce_04[0] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_2;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_2:
		wsi->frame_masking_nonce_04[1] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_3;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_3:
		wsi->frame_masking_nonce_04[2] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state = LWS_RXPS_07_COLLECT_FRAME_KEY_4;
		break;

	case LWS_RXPS_07_COLLECT_FRAME_KEY_4:
		wsi->frame_masking_nonce_04[3] = c;
		if (c)
			wsi->all_zero_nonce = 0;
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		wsi->frame_mask_index = 0;
		break;


	case LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED:
		if (wsi->ietf_spec_revision < 4 ||
			 (wsi->all_zero_nonce && wsi->ietf_spec_revision >= 5))
			wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
			       (wsi->rx_user_buffer_head++)] = c;
		else
			wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
			       (wsi->rx_user_buffer_head++)] =
							  wsi->xor_mask(wsi, c);

		if (--wsi->rx_packet_length == 0) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto spill;
		}
		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
spill:
		/*
		 * is this frame a control packet we should take care of at this
		 * layer?  If so service it and hide it from the user callback
		 */

		lwsl_parser("spill on %s\n", wsi->protocol->name);

		switch (wsi->opcode) {
		case LWS_WS_OPCODE_07__CLOSE:
			/* is this an acknowledgement of our close? */
			if (wsi->state == WSI_STATE_AWAITING_CLOSE_ACK) {
				/*
				 * fine he has told us he is closing too, let's
				 * finish our close
				 */
				lwsl_parser("seen client close ack\n");
				return -1;
			}
			lwsl_parser("server sees client close packet\n");
			/* parrot the close packet payload back */
			n = libwebsocket_write(wsi, (unsigned char *)
			   &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				     wsi->rx_user_buffer_head, LWS_WRITE_CLOSE);
			if (n)
				lwsl_info("write of close ack failed %d\n", n);
			wsi->state = WSI_STATE_RETURNED_CLOSE_ALREADY;
			/* close the connection */
			return -1;

		case LWS_WS_OPCODE_07__PING:
			lwsl_info("received %d byte ping, sending pong\n", wsi->rx_user_buffer_head);
			lwsl_hexdump(&wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING], wsi->rx_user_buffer_head);
			/* parrot the ping packet payload back as a pong */
			n = libwebsocket_write(wsi, (unsigned char *)
			    &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING], wsi->rx_user_buffer_head, LWS_WRITE_PONG);
			/* ... then just drop it */
			wsi->rx_user_buffer_head = 0;
			return 0;

		case LWS_WS_OPCODE_07__PONG:
			/* keep the statistics... */
			wsi->pings_vs_pongs--;
			/* ... then just drop it */
			wsi->rx_user_buffer_head = 0;
			return 0;

		case LWS_WS_OPCODE_07__CONTINUATION:
		case LWS_WS_OPCODE_07__TEXT_FRAME:
		case LWS_WS_OPCODE_07__BINARY_FRAME:
			break;

		default:

			lwsl_parser("passing opcode %x up to exts\n", wsi->opcode);

			/*
			 * It's something special we can't understand here.
			 * Pass the payload up to the extension's parsing
			 * state machine.
			 */

			eff_buf.token = &wsi->rx_user_buffer[
						   LWS_SEND_BUFFER_PRE_PADDING];
			eff_buf.token_len = wsi->rx_user_buffer_head;

			handled = 0;
			for (n = 0; n < wsi->count_active_extensions; n++) {
				m = wsi->active_extensions[n]->callback(
					wsi->protocol->owning_server,
					wsi->active_extensions[n], wsi,
					LWS_EXT_CALLBACK_EXTENDED_PAYLOAD_RX,
					    wsi->active_extensions_user[n],
								   &eff_buf, 0);
				if (m)
					handled = 1;
			}

			if (!handled)
				lwsl_ext("Unhandled extended opcode "
					"0x%x - ignoring frame\n", wsi->opcode);

			wsi->rx_user_buffer_head = 0;
			return 0;
		}

		/*
		 * No it's real payload, pass it up to the user callback.
		 * It's nicely buffered with the pre-padding taken care of
		 * so it can be sent straight out again using libwebsocket_write
		 */

		eff_buf.token = &wsi->rx_user_buffer[
						LWS_SEND_BUFFER_PRE_PADDING];
		eff_buf.token_len = wsi->rx_user_buffer_head;

		for (n = 0; n < wsi->count_active_extensions; n++) {
			m = wsi->active_extensions[n]->callback(
				wsi->protocol->owning_server,
				wsi->active_extensions[n], wsi,
				LWS_EXT_CALLBACK_PAYLOAD_RX,
				wsi->active_extensions_user[n],
				&eff_buf, 0);
			if (m < 0) {
				lwsl_ext(
			          "Extension '%s' failed to handle payload!\n",
			        	      wsi->active_extensions[n]->name);
				return -1;
			}
		}

		if (eff_buf.token_len > 0) {
		    eff_buf.token[eff_buf.token_len] = '\0';

		    if (wsi->protocol->callback)
			    user_callback_handle_rxflow(wsi->protocol->callback,
						    wsi->protocol->owning_server,
						    wsi, LWS_CALLBACK_RECEIVE,
						    wsi->user_space,
			                	    eff_buf.token,
						    eff_buf.token_len);
		    else
			    lwsl_err("No callback on payload spill!\n");
		}

		wsi->rx_user_buffer_head = 0;
		break;
	}

	return 0;

illegal_ctl_length:

	lwsl_warn("Control frame asking for "
			"extended length is illegal\n");
	/* kill the connection */
	return -1;
}




int libwebsocket_interpret_incoming_packet(struct libwebsocket *wsi,
						 unsigned char *buf, size_t len)
{
	size_t n;
	int m;
	int clear_rxflow = !!wsi->rxflow_buffer;
	struct libwebsocket_context *context = wsi->protocol->owning_server;

#ifdef DEBUG
	lwsl_parser("received %d byte packet\n", (int)len);
	lwsl_hexdump(buf, len);
#endif

	if (buf && wsi->rxflow_buffer)
		lwsl_err("!!!! libwebsocket_interpret_incoming_packet: was pending rxflow, data loss\n");

	/* let the rx protocol state machine have as much as it needs */

	n = 0;
	if (!buf) {
		lwsl_info("dumping stored rxflow buffer len %d pos=%d\n", wsi->rxflow_len, wsi->rxflow_pos);
		buf = wsi->rxflow_buffer;
		n = wsi->rxflow_pos;
		len = wsi->rxflow_len;
		/* let's pretend he's already allowing input */
		context->fds[wsi->position_in_fds_table].events |= POLLIN;
	}

	while (n < len) {
		if (!(context->fds[wsi->position_in_fds_table].events & POLLIN)) {
			/* his RX is flowcontrolled */
			if (!wsi->rxflow_buffer) { /* a new rxflow in effect, buffer it and warn caller */
				lwsl_info("new rxflow input buffer len %d\n", len - n);
				wsi->rxflow_buffer = (unsigned char *)malloc(len - n);
				wsi->rxflow_len = len - n;
				wsi->rxflow_pos = 0;
				memcpy(wsi->rxflow_buffer, buf + n, len - n);
			} else {
				lwsl_info("re-using rxflow input buffer\n");
				/* rxflow while we were spilling previous rxflow buffer */
				wsi->rxflow_pos = n;
			}
			return 1;
		}
		m = libwebsocket_rx_sm(wsi, buf[n]);
		if (m < 0)
			return -1;
		n++;
	}

	if (clear_rxflow) {
		lwsl_info("flow: clearing it\n");
		free(wsi->rxflow_buffer);
		wsi->rxflow_buffer = NULL;
		context->fds[wsi->position_in_fds_table].events &= ~POLLIN;
	}

	return 0;
}


/**
 * libwebsockets_remaining_packet_payload() - Bytes to come before "overall"
 *					      rx packet is complete
 * @wsi:		Websocket instance (available from user callback)
 *
 *	This function is intended to be called from the callback if the
 *  user code is interested in "complete packets" from the client.
 *  libwebsockets just passes through payload as it comes and issues a buffer
 *  additionally when it hits a built-in limit.  The LWS_CALLBACK_RECEIVE
 *  callback handler can use this API to find out if the buffer it has just
 *  been given is the last piece of a "complete packet" from the client --
 *  when that is the case libwebsockets_remaining_packet_payload() will return
 *  0.
 *
 *  Many protocols won't care becuse their packets are always small.
 */

size_t
libwebsockets_remaining_packet_payload(struct libwebsocket *wsi)
{
	return wsi->rx_packet_length;
}
