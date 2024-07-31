/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "audio_datapath.h"
#include "audio_i2s.h"
#include "macros_common.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/sys/byteorder.h>
#include <math.h>
#include "sw_codec_lc3.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, 4);
#define DEBUG_INTERVAL_NUM    1000
#define MAX_FRAME_DURATION_US 10000

#define CONFIG_FIFO_FRAME_SPLIT_NUM 10
#define CONFIG_FIFO_TX_FRAME_COUNT  3
#define CONFIG_FIFO_RX_FRAME_COUNT  1

#define FIFO_TX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_TX_FRAME_COUNT)
#define FIFO_RX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_RX_FRAME_COUNT)

DATA_FIFO_DEFINE(fifo_tx, FIFO_TX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));
DATA_FIFO_DEFINE(fifo_rx, FIFO_RX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));

#define AVAILABLE_SINK_CONTEXT                                                                     \
	(BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL |                \
	 BT_AUDIO_CONTEXT_TYPE_MEDIA | BT_AUDIO_CONTEXT_TYPE_GAME |                                \
	 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

#define AVAILABLE_SOURCE_CONTEXT                                                                   \
	(BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL |                \
	 BT_AUDIO_CONTEXT_TYPE_MEDIA | BT_AUDIO_CONTEXT_TYPE_GAME |                                \
	 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_ASCS_ASE_SRC_COUNT,
			  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static const struct bt_audio_codec_cap lc3_codec_cap =
	BT_AUDIO_CODEC_CAP_LC3(BT_AUDIO_CODEC_CAP_FREQ_16KHZ, BT_AUDIO_CODEC_CAP_DURATION_10,
			       BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 40, 1u,
			       (BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL));

static struct bt_conn *default_conn;
static struct k_work_delayable audio_send_work;
static struct bt_bap_stream sink_streams[CONFIG_BT_ASCS_ASE_SNK_COUNT];
static struct audio_source {
	struct bt_bap_stream stream;
	uint16_t seq_num;
	uint16_t max_sdu;
	size_t len_to_send;
} source_streams[CONFIG_BT_ASCS_ASE_SRC_COUNT];

static size_t configured_source_stream_count;

static const struct bt_audio_codec_qos_pref qos_pref =
	BT_AUDIO_CODEC_QOS_PREF(true, BT_GAP_LE_PHY_2M, 0x02, 10, 10000, 40000, 10000, 40000);

#define CONFIG_ENCODER_STACK_SIZE  8192
#define CONFIG_ENCODER_THREAD_PRIO 3
K_THREAD_STACK_DEFINE(encoder_thread_stack, CONFIG_ENCODER_STACK_SIZE);
static struct k_thread encoder_thread_data;
static k_tid_t encoder_thread_id;
static struct bt_le_ext_adv *adv;
static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL),    /* ASCS UUID */
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED, /* Target Announcement */
	BT_BYTES_LIST_LE16(AVAILABLE_SINK_CONTEXT),
	BT_BYTES_LIST_LE16(AVAILABLE_SOURCE_CONTEXT),
	0x00, /* Metadata length */
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
};

static struct k_work_delayable adv_start_work;

static void work_adv_start(struct k_work *work)
{
	int ret;

	ret = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (ret) {
		printk("Failed to start advertising set (ret %d)\n", ret);
		return 0;
	}
}

static uint16_t get_and_incr_seq_num(const struct bt_bap_stream *stream)
{
	for (size_t i = 0U; i < configured_source_stream_count; i++) {
		if (stream == &source_streams[i].stream) {
			uint16_t seq_num;

			seq_num = source_streams[i].seq_num;

			source_streams[i].seq_num++;

			return seq_num;
		}
	}

	printk("Could not find endpoint from stream %p\n", stream);

	return 0;
}

#include "pcm_stream_channel_modifier.h"
static uint8_t send_start = 0;
static void encoder_thread(void *arg1, void *arg2, void *arg3)
{
	int ret;
	uint32_t blocks_alloced_num;
	uint32_t blocks_locked_num;

	int debug_trans_count = 0;
	size_t encoded_data_size = 0;

	void *tmp_pcm_raw_data[CONFIG_FIFO_FRAME_SPLIT_NUM];
	char pcm_raw_data[FRAME_SIZE_BYTES];

	static uint8_t *encoded_data;
	static size_t pcm_block_size;
	static int32_t pcm_raw_data_show;
	while (1) {
		/* Don't start encoding until the stream needing it has started
		ret = k_poll(&encoder_evt, 1, K_FOREVER);
		*/
		/* Get PCM data from I2S */
		/* Since one audio frame is divided into a number of
		 * blocks, we need to fetch the pointers to all of these
		 * blocks before copying it to a continuous area of memory
		 * before sending it to the encoder
		 */
		for (int i = 0; i < CONFIG_FIFO_FRAME_SPLIT_NUM; i++) {
			ret = data_fifo_pointer_last_filled_get(&fifo_rx, &tmp_pcm_raw_data[i],
								&pcm_block_size, K_FOREVER);
			ERR_CHK(ret);
			memcpy(pcm_raw_data + (i * BLOCK_SIZE_BYTES), tmp_pcm_raw_data[i],
			       pcm_block_size);

			data_fifo_block_free(&fifo_rx, tmp_pcm_raw_data[i]);
		}
		// LOG_INF("got a frame %d", BLOCK_SIZE_BYTES);
		// LOG_HEXDUMP_INF(pcm_raw_data, FRAME_SIZE_BYTES,"pcm_raw_data");
		char pcm_data_mono_system_sample_rate[2][640] = {0};
		uint32_t pcm_raw_data_show;
		uint8_t m_encoded_data[40];
		uint16_t encoded_bytes_written;
		pscm_two_channel_split(pcm_raw_data, FRAME_SIZE_BYTES, 32,
				       pcm_data_mono_system_sample_rate[0],
				       pcm_data_mono_system_sample_rate[1], &pcm_raw_data_show);

		ret = sw_codec_lc3_enc_run(pcm_data_mono_system_sample_rate[0],
					   FRAME_SIZE_BYTES / 2, 32000, 0, sizeof(m_encoded_data),
					   m_encoded_data, &encoded_bytes_written);
		if (ret) {
			LOG_INF("Failed to encode LC3 data, ret = %d", ret);
		}
		// LOG_HEXDUMP_INF(pcm_data_mono_system_sample_rate[0],
		// FRAME_SIZE_BYTES/2,"data[0]"); printf("%d\n", FRAME_SIZE_BYTES);
		/*
		if (sw_codec_cfg.encoder.enabled) {
			ret = sw_codec_encode(pcm_raw_data, FRAME_SIZE_BYTES, &encoded_data,
					      &encoded_data_size);

			ERR_CHK_MSG(ret, "Encode failed");
		}

		ret = sw_codec_lc3_enc_run(pcm_raw_data, FRAME_SIZE_BYTES, 32000, 0, uint16_t
		lc3_data_buf_size, uint8_t *const lc3_data, uint16_t *const lc3_data_wr_size);
		*/

		/* Print block usage */
		if (debug_trans_count == DEBUG_INTERVAL_NUM) {
			ret = data_fifo_num_used_get(&fifo_rx, &blocks_alloced_num,
						     &blocks_locked_num);
			ERR_CHK(ret);
			// LOG_DBG(COLOR_CYAN "RX alloced: %d, locked: %d" COLOR_RESET,
			//	blocks_alloced_num, blocks_locked_num);
			debug_trans_count = 0;
		} else {
			debug_trans_count++;
		}
		/*
		if (sw_codec_cfg.encoder.enabled) {
			streamctrl_send(encoded_data, encoded_data_size,
					sw_codec_cfg.encoder.num_ch);
		}
		STACK_USAGE_PRINT("encoder_thread", &encoder_thread_data);
		*/

		static uint8_t buf_data[CONFIG_BT_ISO_TX_MTU];
		static int i = 0;
		struct net_buf *buf;

		struct bt_bap_stream *stream = &source_streams[0].stream;

		buf = net_buf_alloc(&tx_pool, K_FOREVER);
		net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);

		net_buf_add_mem(buf, m_encoded_data, 40);

		ret = bt_bap_stream_send(stream, buf, i++);
		if (ret < 0) {
			if (i % 100 == 0) {
				printk("Failed to send audio data on streams[%zu] (%p): (%d)\n", i,
				       stream, ret);
			}
			net_buf_unref(buf);

		} else {
			if (i % 100 == 0) {
				printk("Sending mock data with len %zu on streams[%zu] (%p)\n",
				       40, i, stream);
			}
		}
	}
}

#define MAX_FRAME_DURATION_US 10000

static int frames_per_sdu;

void print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		printk("%02x", *ptr++);
	}
}

static bool print_cb(struct bt_data *data, void *user_data)
{
	const char *str = (const char *)user_data;

	printk("%s: type 0x%02x value_len %u\n", str, data->type, data->data_len);
	print_hex(data->data, data->data_len);
	printk("\n");

	return true;
}

static void print_codec_cfg(const struct bt_audio_codec_cfg *codec_cfg)
{
	printk("codec_cfg 0x%02x cid 0x%04x vid 0x%04x count %u\n", codec_cfg->id, codec_cfg->cid,
	       codec_cfg->vid, codec_cfg->data_len);

	if (codec_cfg->id == BT_HCI_CODING_FORMAT_LC3) {
		enum bt_audio_location chan_allocation;
		int ret;

		/* LC3 uses the generic LTV format - other codecs might do as well */

		bt_audio_data_parse(codec_cfg->data, codec_cfg->data_len, print_cb, "data");

		ret = bt_audio_codec_cfg_get_freq(codec_cfg);
		if (ret > 0) {
			printk("  Frequency: %d Hz\n", bt_audio_codec_cfg_freq_to_freq_hz(ret));
		}

		ret = bt_audio_codec_cfg_get_frame_dur(codec_cfg);
		if (ret > 0) {
			printk("  Frame Duration: %d us\n",
			       bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret));
		}

		if (bt_audio_codec_cfg_get_chan_allocation(codec_cfg, &chan_allocation) == 0) {
			printk("  Channel allocation: 0x%x\n", chan_allocation);
		}

		printk("  Octets per frame: %d (negative means value not pressent)\n",
		       bt_audio_codec_cfg_get_octets_per_frame(codec_cfg));
		printk("  Frames per SDU: %d\n",
		       bt_audio_codec_cfg_get_frame_blocks_per_sdu(codec_cfg, true));
	} else {
		print_hex(codec_cfg->data, codec_cfg->data_len);
	}

	bt_audio_data_parse(codec_cfg->meta, codec_cfg->meta_len, print_cb, "meta");
}

static void print_qos(const struct bt_audio_codec_qos *qos)
{
	printk("QoS: interval %u framing 0x%02x phy 0x%02x sdu %u "
	       "rtn %u latency %u pd %u\n",
	       qos->interval, qos->framing, qos->phy, qos->sdu, qos->rtn, qos->latency, qos->pd);
}

/**
 * @brief Send audio data on timeout
 *
 * This will send an increasing amount of audio data, starting from 1 octet.
 * The data is just mock data, and does not actually represent any audio.
 *
 * First iteration : 0x00
 * Second iteration: 0x00 0x01
 * Third iteration : 0x00 0x01 0x02
 *
 * And so on, until it wraps around the configured MTU (CONFIG_BT_ISO_TX_MTU)
 *
 * @param work Pointer to the work structure
 */
static void audio_timer_timeout(struct k_work *work)
{
	int ret;
	static uint8_t buf_data[CONFIG_BT_ISO_TX_MTU];
	static bool data_initialized;
	struct net_buf *buf;

	if (!data_initialized) {
		/* TODO: Actually encode some audio data */
		for (size_t i = 0U; i < ARRAY_SIZE(buf_data); i++) {
			buf_data[i] = (uint8_t)i;
		}

		data_initialized = true;
	}

	/* We configured the sink streams to be first in `streams`, so that
	 * we can use `stream[i]` to select sink streams (i.e. streams with
	 * data going to the server)
	 */
	for (size_t i = 0; i < configured_source_stream_count; i++) {
		struct bt_bap_stream *stream = &source_streams[i].stream;

		buf = net_buf_alloc(&tx_pool, K_FOREVER);
		net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);

		net_buf_add_mem(buf, buf_data, ++source_streams[i].len_to_send);

		ret = bt_bap_stream_send(stream, buf, get_and_incr_seq_num(stream));
		if (ret < 0) {
			printk("Failed to send audio data on streams[%zu] (%p): (%d)\n", i, stream,
			       ret);
			net_buf_unref(buf);
		} else {
			printk("Sending mock data with len %zu on streams[%zu] (%p)\n",
			       source_streams[i].len_to_send, i, stream);
		}

		if (source_streams[i].len_to_send >= source_streams[i].max_sdu) {
			source_streams[i].len_to_send = 0;
		}
	}

	/*
	#if defined(CONFIG_LIBLC3)
		k_work_schedule(&audio_send_work, K_USEC(MAX_FRAME_DURATION_US));
	#else
		k_work_schedule(&audio_send_work, K_USEC(AUDIO_DATA_TIMEOUT_US));
	#endif
	*/
}

static enum bt_audio_dir stream_dir(const struct bt_bap_stream *stream)
{
	for (size_t i = 0U; i < ARRAY_SIZE(source_streams); i++) {
		if (stream == &source_streams[i].stream) {
			return BT_AUDIO_DIR_SOURCE;
		}
	}

	for (size_t i = 0U; i < ARRAY_SIZE(sink_streams); i++) {
		if (stream == &sink_streams[i]) {
			return BT_AUDIO_DIR_SINK;
		}
	}

	__ASSERT(false, "Invalid stream %p", stream);
	return 0;
}

static struct bt_bap_stream *stream_alloc(enum bt_audio_dir dir)
{
	if (dir == BT_AUDIO_DIR_SOURCE) {
		for (size_t i = 0; i < ARRAY_SIZE(source_streams); i++) {
			struct bt_bap_stream *stream = &source_streams[i].stream;

			if (!stream->conn) {
				return stream;
			}
		}
	} else {
		for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
			struct bt_bap_stream *stream = &sink_streams[i];

			if (!stream->conn) {
				return stream;
			}
		}
	}

	return NULL;
}

static int lc3_config(struct bt_conn *conn, const struct bt_bap_ep *ep, enum bt_audio_dir dir,
		      const struct bt_audio_codec_cfg *codec_cfg, struct bt_bap_stream **stream,
		      struct bt_audio_codec_qos_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Config: conn %p ep %p dir %u\n", conn, ep, dir);

	print_codec_cfg(codec_cfg);

	*stream = stream_alloc(dir);
	if (*stream == NULL) {
		printk("No streams available\n");
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_NO_MEM, BT_BAP_ASCS_REASON_NONE);

		return -ENOMEM;
	}

	printk("ASE Codec Config stream %p\n", *stream);

	if (dir == BT_AUDIO_DIR_SOURCE) {
		configured_source_stream_count++;
	}

	*pref = qos_pref;

	return 0;
}

static int lc3_reconfig(struct bt_bap_stream *stream, enum bt_audio_dir dir,
			const struct bt_audio_codec_cfg *codec_cfg,
			struct bt_audio_codec_qos_pref *const pref, struct bt_bap_ascs_rsp *rsp)
{
	printk("ASE Codec Reconfig: stream %p\n", stream);

	print_codec_cfg(codec_cfg);

	*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED, BT_BAP_ASCS_REASON_NONE);

	/* We only support one QoS at the moment, reject changes */
	return -ENOEXEC;
}

static int lc3_qos(struct bt_bap_stream *stream, const struct bt_audio_codec_qos *qos,
		   struct bt_bap_ascs_rsp *rsp)
{
	printk("QoS: stream %p qos %p\n", stream, qos);

	print_qos(qos);

	for (size_t i = 0U; i < configured_source_stream_count; i++) {
		if (stream == &source_streams[i].stream) {
			source_streams[i].max_sdu = qos->sdu;
			break;
		}
	}

	return 0;
}

static int lc3_enable(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
		      struct bt_bap_ascs_rsp *rsp)
{
	printk("Enable: stream %p meta_len %zu\n", stream, meta_len);

#if 0
	{
		int frame_duration_us;
		int freq;
		int ret;

		ret = bt_audio_codec_cfg_get_freq(stream->codec_cfg);
		if (ret > 0) {
			freq = bt_audio_codec_cfg_freq_to_freq_hz(ret);
		} else {
			printk("Error: Codec frequency not set, cannot start codec.");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
					       BT_BAP_ASCS_REASON_CODEC_DATA);
			return ret;
		}

		ret = bt_audio_codec_cfg_get_frame_dur(stream->codec_cfg);
		if (ret > 0) {
			frame_duration_us = bt_audio_codec_cfg_frame_dur_to_frame_dur_us(ret);
		} else {
			printk("Error: Frame duration not set, cannot start codec.");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
					       BT_BAP_ASCS_REASON_CODEC_DATA);
			return ret;
		}

		frames_per_sdu =
			bt_audio_codec_cfg_get_frame_blocks_per_sdu(stream->codec_cfg, true);

		lc3_decoder = lc3_setup_decoder(frame_duration_us,
						freq,
						0, /* No resampling */
						&lc3_decoder_mem);

		if (lc3_decoder == NULL) {
			printk("ERROR: Failed to setup LC3 encoder - wrong parameters?\n");
			*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_CONF_INVALID,
					       BT_BAP_ASCS_REASON_CODEC_DATA);
			return -1;
		}
	}
#endif

	return 0;
}

static int lc3_start(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Start: stream %p\n", stream);

	for (size_t i = 0U; i < configured_source_stream_count; i++) {
		if (stream == &source_streams[i].stream) {
			source_streams[i].seq_num = 0U;
			source_streams[i].len_to_send = 0U;
			break;
		}
	}

	if (configured_source_stream_count > 0 && !k_work_delayable_is_pending(&audio_send_work)) {

		/* Start send timer */
		// k_work_schedule(&audio_send_work, K_MSEC(0));
	}

	return 0;
}

static bool data_func_cb(struct bt_data *data, void *user_data)
{
	struct bt_bap_ascs_rsp *rsp = (struct bt_bap_ascs_rsp *)user_data;

	if (!BT_AUDIO_METADATA_TYPE_IS_KNOWN(data->type)) {
		printk("Invalid metadata type %u or length %u\n", data->type, data->data_len);
		*rsp = BT_BAP_ASCS_RSP(BT_BAP_ASCS_RSP_CODE_METADATA_REJECTED, data->type);

		return -EINVAL;
	}

	return true;
}

static int lc3_metadata(struct bt_bap_stream *stream, const uint8_t meta[], size_t meta_len,
			struct bt_bap_ascs_rsp *rsp)
{
	printk("Metadata: stream %p meta_len %zu\n", stream, meta_len);

	return bt_audio_data_parse(meta, meta_len, data_func_cb, rsp);
}

static int lc3_disable(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Disable: stream %p\n", stream);

	return 0;
}

static int lc3_stop(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Stop: stream %p\n", stream);

	return 0;
}

static int lc3_release(struct bt_bap_stream *stream, struct bt_bap_ascs_rsp *rsp)
{
	printk("Release: stream %p\n", stream);
	return 0;
}

static const struct bt_bap_unicast_server_cb unicast_server_cb = {
	.config = lc3_config,
	.reconfig = lc3_reconfig,
	.qos = lc3_qos,
	.enable = lc3_enable,
	.start = lc3_start,
	.metadata = lc3_metadata,
	.disable = lc3_disable,
	.stop = lc3_stop,
	.release = lc3_release,
};

static void stream_recv(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			struct net_buf *buf)
{
	if (info->flags & BT_ISO_FLAGS_VALID) {
		printk("Incoming audio on stream %p len %u\n", stream, buf->len);
	}
}

static void stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	printk("Audio Stream %p stopped with reason 0x%02X\n", stream, reason);
	send_start = 0;
}

static void stream_enabled_cb(struct bt_bap_stream *stream)
{
	/* The unicast server is responsible for starting sink ASEs after the
	 * client has enabled them.
	 */
	if (stream_dir(stream) == BT_AUDIO_DIR_SINK) {
		const int err = bt_bap_stream_start(stream);

		if (err != 0) {
			printk("Failed to start stream %p: %d", stream, err);
		}
	}
}
static void stream_started_cb(struct bt_bap_stream *stream)
{
	printk("Audio Stream %p started\n", stream);
	send_start = 1;
}

static struct bt_bap_stream_ops stream_ops = {
	.recv = stream_recv,
	.stopped = stream_stopped,
	.enabled = stream_enabled_cb,
	.started = stream_started_cb,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		printk("Failed to connect to %s (%u)\n", addr, err);

		default_conn = NULL;
		return;
	}

	printk("Connected: %s\n", addr);
	default_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;
	k_work_schedule(&adv_start_work, K_MSEC(10));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static struct bt_pacs_cap cap_sink = {
	.codec_cap = &lc3_codec_cap,
};

static struct bt_pacs_cap cap_source = {
	.codec_cap = &lc3_codec_cap,
};

static int set_location(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK_LOC)) {
		err = bt_pacs_set_location(BT_AUDIO_DIR_SINK, BT_AUDIO_LOCATION_FRONT_LEFT);
		if (err != 0) {
			printk("Failed to set sink location (err %d)\n", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC_LOC)) {
		err = bt_pacs_set_location(BT_AUDIO_DIR_SOURCE, (BT_AUDIO_LOCATION_FRONT_LEFT));
		if (err != 0) {
			printk("Failed to set source location (err %d)\n", err);
			return err;
		}
	}

	printk("Location successfully set\n");

	return 0;
}

static int set_supported_contexts(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK)) {
		err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink supported contexts (err %d)\n", err);

			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC)) {
		err = bt_pacs_set_supported_contexts(BT_AUDIO_DIR_SOURCE, AVAILABLE_SOURCE_CONTEXT);
		if (err != 0) {
			printk("Failed to set source supported contexts (err %d)\n", err);

			return err;
		}
	}

	printk("Supported contexts successfully set\n");

	return 0;
}

static int set_available_contexts(void)
{
	int err;

	if (IS_ENABLED(CONFIG_BT_PAC_SNK)) {
		err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK, AVAILABLE_SINK_CONTEXT);
		if (err != 0) {
			printk("Failed to set sink available contexts (err %d)\n", err);
			return err;
		}
	}

	if (IS_ENABLED(CONFIG_BT_PAC_SRC)) {
		err = bt_pacs_set_available_contexts(BT_AUDIO_DIR_SOURCE, AVAILABLE_SOURCE_CONTEXT);
		if (err != 0) {
			printk("Failed to set source available contexts (err %d)\n", err);
			return err;
		}
	}

	printk("Available contexts successfully set\n");
	return 0;
}
#define LC3_BITRATE       32000
#define LC3_FRAME_SIZE_US 10000
#define PCM_SAMPLE_RATE   16000
#define PCM_BIT_DEPTH     32
#define LC3_NUM_CHANNELS  2
static void audio_init()
{
	int ret;
	uint16_t pcm_bytes_req_enc;

	ret = sw_codec_lc3_init(NULL, NULL, MAX_FRAME_DURATION_US);
	if (ret) {
		printk("sw_codec_lc3_init failed (ret %d)\n", ret);
	}

	ret = sw_codec_lc3_enc_init(PCM_SAMPLE_RATE, PCM_BIT_DEPTH, LC3_FRAME_SIZE_US, LC3_BITRATE,
				    LC3_NUM_CHANNELS, &pcm_bytes_req_enc);

	if (ret) {
		printk("sw_codec_lc3_enc_init failed (ret %d)\n", ret);
	} else {
		printk("LC3 encoder initialized, PCM bytes required for encoding: %u\n",
		       pcm_bytes_req_enc);
	}

	ret = data_fifo_init(&fifo_rx);
	if (ret) {
		printf("Failed to initialize TX FIFO\n");
		return ret;
	}

	audio_datapath_init();
	ret = audio_datapath_start(&fifo_rx);
	if (ret) {
		printf("Failed to start audio datapath\n");
		return ret;
	}

	if (encoder_thread_id == NULL) {
		encoder_thread_id = k_thread_create(
			&encoder_thread_data, encoder_thread_stack, CONFIG_ENCODER_STACK_SIZE,
			(k_thread_entry_t)encoder_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(CONFIG_ENCODER_THREAD_PRIO), 0, K_NO_WAIT);
		ret = k_thread_name_set(encoder_thread_id, "ENCODER");
		ERR_CHK(ret);
	}
}

int main(void)
{
	int ret;

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	k_work_init_delayable(&adv_start_work, work_adv_start);

	ret = bt_enable(NULL);
	if (ret != 0) {
		printk("Bluetooth init failed (ret %d)\n", ret);
		return 0;
	}

	printk("Bluetooth initialized\n");

	bt_bap_unicast_server_register_cb(&unicast_server_cb);

	bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap_sink);
	bt_pacs_cap_register(BT_AUDIO_DIR_SOURCE, &cap_source);

	for (size_t i = 0; i < ARRAY_SIZE(sink_streams); i++) {
		bt_bap_stream_cb_register(&sink_streams[i], &stream_ops);
	}

	for (size_t i = 0; i < ARRAY_SIZE(source_streams); i++) {
		bt_bap_stream_cb_register(&source_streams[i].stream, &stream_ops);
	}

	ret = set_location();
	if (ret != 0) {
		return 0;
	}

	ret = set_supported_contexts();
	if (ret != 0) {
		return 0;
	}

	ret = set_available_contexts();
	if (ret != 0) {
		return 0;
	}

	audio_init();

	ret = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN_NAME, NULL, &adv);
	if (ret) {
		printk("Failed to create advertising set (ret %d)\n", ret);
		return 0;
	}

	ret = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		printk("Failed to set advertising data (ret %d)\n", ret);
		return 0;
	}

	k_work_schedule(&adv_start_work, K_MSEC(10));

	return 0;
}
