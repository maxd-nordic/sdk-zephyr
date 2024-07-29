/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "audio_datapath.h"
#include "audio_i2s.h"


#define CONFIG_FIFO_FRAME_SPLIT_NUM 10
#define CONFIG_FIFO_TX_FRAME_COUNT 3
#define CONFIG_FIFO_RX_FRAME_COUNT 1

#define FIFO_TX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_TX_FRAME_COUNT)
#define FIFO_RX_BLOCK_COUNT (CONFIG_FIFO_FRAME_SPLIT_NUM * CONFIG_FIFO_RX_FRAME_COUNT)

DATA_FIFO_DEFINE(fifo_tx, FIFO_TX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));
DATA_FIFO_DEFINE(fifo_rx, FIFO_RX_BLOCK_COUNT, WB_UP(BLOCK_SIZE_BYTES));

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
	return 0;
}
