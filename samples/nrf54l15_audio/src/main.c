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
#define DEBUG_INTERVAL_NUM     1000
#define MAX_FRAME_DURATION_US 10000

#define CONFIG_FIFO_FRAME_SPLIT_NUM 10
#define CONFIG_FIFO_TX_FRAME_COUNT 3
#define CONFIG_FIFO_RX_FRAME_COUNT 1

#define FIFO_TX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_TX_FRAME_COUNT)
#define FIFO_RX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_RX_FRAME_COUNT)

DATA_FIFO_DEFINE(fifo_tx, FIFO_TX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));
DATA_FIFO_DEFINE(fifo_rx, FIFO_RX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));


#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | \
				 BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA | \
				 BT_AUDIO_CONTEXT_TYPE_GAME | \
				 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

#define AVAILABLE_SOURCE_CONTEXT (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | \
				  BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL)

NET_BUF_POOL_FIXED_DEFINE(tx_pool, CONFIG_BT_ASCS_ASE_SRC_COUNT,
			  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_16KHZ, BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 120u, 1u,
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

#define CONFIG_ENCODER_STACK_SIZE 4096
#define CONFIG_ENCODER_THREAD_PRIO 3
K_THREAD_STACK_DEFINE(encoder_thread_stack, CONFIG_ENCODER_STACK_SIZE);
static struct k_thread encoder_thread_data;
static k_tid_t encoder_thread_id;
static struct bt_le_ext_adv *adv;
static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL), /* ASCS UUID */
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
		//printf("%d\n", FRAME_SIZE_BYTES);
		/*
		if (sw_codec_cfg.encoder.enabled) {
			ret = sw_codec_encode(pcm_raw_data, FRAME_SIZE_BYTES, &encoded_data,
					      &encoded_data_size);

			ERR_CHK_MSG(ret, "Encode failed");
		}
		*/

		/* Print block usage */
		if (debug_trans_count == DEBUG_INTERVAL_NUM) {
			ret = data_fifo_num_used_get(&fifo_rx, &blocks_alloced_num,
						     &blocks_locked_num);
			ERR_CHK(ret);
			//LOG_DBG(COLOR_CYAN "RX alloced: %d, locked: %d" COLOR_RESET,
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
	}
}

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

int main(void)
{
	int ret;


	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

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

	k_work_init_delayable(&adv_start_work, work_adv_start);


	ret = bt_enable(NULL);
	if (ret != 0) {
		printk("Bluetooth init failed (ret %d)\n", ret);
		return 0;
	}

	printk("Bluetooth initialized\n");

	ret = sw_codec_lc3_init(NULL, NULL, MAX_FRAME_DURATION_US);
	if (ret) {
		printk("sw_codec_lc3_init failed (ret %d)\n", ret);
	}

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
