/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "examplechunks.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);


#if defined(CONFIG_LIBLC3)
static uint8_t lc3_data[400];
#include <lc3.h>
static lc3_encoder_mem_48k_t encoder_mem;

int encode_test(int nbytes)
{
	lc3_encoder_t encoder = lc3_setup_encoder(10000, 48000, 0, &encoder_mem);
	int ret = 0;
	for (int i = 0; i < 10; i++) {
		ret = lc3_encode(encoder, LC3_PCM_FORMAT_S16, examplechunks_bin + (960*i), 1, nbytes, lc3_data);
		if (ret < 0) {
			printk("Error encoding frame %d\n", i);
			return ret;
		}
	}
	return 0;
}
#endif

#if defined(CONFIG_LIBLC3PLUS)
#define DISABLE_HR_MODE
#include "functions.h"
#include "lc3plus.h"
uint8_t encoder_area[LC3PLUS_ENC_MAX_SIZE];
uint8_t lc3plus_scratch[LC3PLUS_ENC_MAX_SCRATCH_SIZE];
static uint8_t lc3_data[LC3PLUS_MAX_BYTES];
int encode_test(int nbytes)
{
	int bitrate = 8000*nbytes/10;
	int ret = 0;
	LC3PLUS_Enc * enc = (LC3PLUS_Enc *)encoder_area;

	ret = lc3plus_enc_init(enc, 48000, 1, NULL);
	if (ret < 0) {
		printk("Error initializing encoder\n");
		return ret;
	}

	ret = lc3plus_enc_set_frame_dms(enc, 10 * 10);
	if (ret < 0) {
		printk("Error setting frame duration\n");
		return ret;
	}

	ret = lc3plus_enc_set_ep_mode(enc, LC3PLUS_EP_OFF);
	if (ret < 0) {
		printk("Error setting error protection\n");
		return ret;
	}

	ret = lc3plus_enc_set_bitrate(enc, bitrate);
	if (ret < 0) {
		printk("Error setting bitrate\n");
		return ret;
	}

	for (int i = 0; i < 10; i++) {
		int16_t* channels[] = {examplechunks_bin + (960*i)};
		ret = lc3plus_enc16(enc, channels, lc3_data, &nbytes, lc3plus_scratch);
		if (ret < 0) {
			printk("Error encoding frame %d\n", i);
			return ret;
		}
	}
	return 0;
}

#endif

#if defined(CONFIG_SW_CODEC_LC3_T2_SOFTWARE)
#endif

int main(void)
{
	int ret;
	bool led_state = true;
	uint32_t start_time;

	printf("LC3 Encoder Test\n");
	printf("Cycles per second: %u\n", sys_clock_hw_cycles_per_sec());

	start_time = k_cycle_get_32();
	encode_test(20);
	printf("Time taken: %d, nbytes: 20\n", k_cycle_get_32() - start_time);

	start_time = k_cycle_get_32();
	encode_test(40);
	printf("Time taken: %d, nbytes: 40\n", k_cycle_get_32() - start_time);

	start_time = k_cycle_get_32();
	encode_test(80);
	printf("Time taken: %d, nbytes: 80\n", k_cycle_get_32() - start_time);

	start_time = k_cycle_get_32();
	encode_test(160);
	printf("Time taken: %d, nbytes: 160\n", k_cycle_get_32() - start_time);

	start_time = k_cycle_get_32();
	encode_test(320);
	printf("Time taken: %d, nbytes: 320\n", k_cycle_get_32() - start_time);

	start_time = k_cycle_get_32();
	encode_test(400);
	printf("Time taken: %d, nbytes: 400\n", k_cycle_get_32() - start_time);

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		led_state = !led_state;
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
