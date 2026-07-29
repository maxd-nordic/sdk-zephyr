/*
 * Copyright (c) 2021 Bosch Sensortec GmbH
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_BMI270_BMI270_H_
#define ZEPHYR_DRIVERS_SENSOR_BMI270_BMI270_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define BMI270_WR_LEN                           32
#define BMI270_CONFIG_FILE_RETRIES              15
#define BMI270_CONFIG_FILE_POLL_PERIOD_US       10000
#define BMI270_INTER_WRITE_DELAY_US             1000

#define BMI270_REG_CHIP_ID         0x00
#define BMI270_REG_ERROR           0x02
#define BMI270_REG_STATUS          0x03
#define BMI270_REG_AUX_X_LSB       0x04
#define BMI270_REG_ACC_X_LSB       0x0C
#define BMI270_REG_GYR_X_LSB       0x12
#define BMI270_REG_SENSORTIME_0    0x18
#define BMI270_REG_EVENT           0x1B
#define BMI270_REG_INT_STATUS_0    0x1C
#define BMI270_REG_SC_OUT_0        0x1E
#define BMI270_REG_WR_GEST_ACT     0x20
#define BMI270_REG_INTERNAL_STATUS 0x21
#define BMI270_REG_TEMPERATURE_0   0x22
#define BMI270_REG_FIFO_LENGTH_0   0x24
#define BMI270_REG_FIFO_DATA       0x26
#define BMI270_REG_FEAT_PAGE       0x2F
#define BMI270_REG_FEATURES_0      0x30
#define BMI270_REG_ACC_CONF        0x40
#define BMI270_REG_ACC_RANGE       0x41
#define BMI270_REG_GYR_CONF        0x42
#define BMI270_REG_GYR_RANGE       0x43
#define BMI270_REG_AUX_CONF        0x44
#define BMI270_REG_FIFO_DOWNS      0x45
#define BMI270_REG_FIFO_WTM_0      0x46
#define BMI270_REG_FIFO_CONFIG_0   0x48
#define BMI270_REG_FIFO_CONFIG_1   0x49
#define BMI270_REG_SATURATION      0x4A
#define BMI270_REG_AUX_DEV_ID      0x4B
#define BMI270_REG_AUX_IF_CONF     0x4C
#define BMI270_REG_AUX_RD_ADDR     0x4D
#define BMI270_REG_AUX_WR_ADDR     0x4E
#define BMI270_REG_AUX_WR_DATA     0x4F
#define BMI270_REG_ERR_REG_MSK     0x52
#define BMI270_REG_INT1_IO_CTRL    0x53
#define BMI270_REG_INT2_IO_CTRL    0x54
#define BMI270_REG_INT_LATCH       0x55
#define BMI270_REG_INT1_MAP_FEAT   0x56
#define BMI270_REG_INT2_MAP_FEAT   0x57
#define BMI270_REG_INT_MAP_DATA    0x58
#define BMI270_REG_INIT_CTRL       0x59
#define BMI270_REG_INIT_ADDR_0     0x5B
#define BMI270_REG_INIT_DATA       0x5E
#define BMI270_REG_INTERNAL_ERROR  0x5F
#define BMI270_REG_AUX_IF_TRIM     0x68
#define BMI270_REG_GYR_CRT_CONF    0x69
#define BMI270_REG_NVM_CONF        0x6A
#define BMI270_REG_IF_CONF         0x6B
#define BMI270_REG_DRV             0x6C
#define BMI270_REG_ACC_SELF_TEST   0x6D
#define BMI270_REG_GYR_SELF_TEST   0x6E
#define BMI270_REG_NV_CONF         0x70
#define BMI270_REG_OFFSET_0        0x71
#define BMI270_REG_PWR_CONF        0x7C
#define BMI270_REG_PWR_CTRL        0x7D
#define BMI270_REG_CMD             0x7E
#define BMI270_REG_MASK            GENMASK(6, 0)

#define BMI270_ANYMO_1_DURATION_POS	0
#define BMI270_ANYMO_1_DURATION_MASK	BIT_MASK(12)
#define BMI270_ANYMO_1_DURATION(n)	((n) << BMI270_ANYMO_1_DURATION_POS)
#define BMI270_ANYMO_1_SELECT_X		BIT(13)
#define BMI270_ANYMO_1_SELECT_Y		BIT(14)
#define BMI270_ANYMO_1_SELECT_Z		BIT(15)
#define BMI270_ANYMO_1_SELECT_XYZ	(BMI270_ANYMO_1_SELECT_X | \
					 BMI270_ANYMO_1_SELECT_Y | \
					 BMI270_ANYMO_1_SELECT_Z)
#define BMI270_ANYMO_2_THRESHOLD_POS	0
#define BMI270_ANYMO_2_THRESHOLD_MASK	BIT_MASK(10)
#define BMI270_ANYMO_2_THRESHOLD(n)	((n) << BMI270_ANYMO_2_THRESHOLD_POS)
#define BMI270_ANYMO_2_OUT_CONF_POS	11
#define BMI270_ANYMO_2_OUT_CONF_MASK	(BIT(11) | BIT(12) | BIT(13) | BIT(14))
#define BMI270_ANYMO_2_ENABLE		BIT(15)
#define BMI270_ANYMO_2_OUT_CONF_OFF	(0x00 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_0	(0x01 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_1	(0x02 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_2	(0x03 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_3	(0x04 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_4	(0x05 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_5	(0x06 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_6	(0x07 << BMI270_ANYMO_2_OUT_CONF_POS)
#define BMI270_ANYMO_2_OUT_CONF_BIT_8	(0x08 << BMI270_ANYMO_2_OUT_CONF_POS)

#define BMI270_INT_IO_CTRL_LVL		BIT(1) /* Output level (0 = active low, 1 = active high) */
#define BMI270_INT_IO_CTRL_OD		BIT(2) /* Open-drain (0 = push-pull, 1 = open-drain)*/
#define BMI270_INT_IO_CTRL_OUTPUT_EN	BIT(3) /* Output enabled */
#define BMI270_INT_IO_CTRL_INPUT_EN	BIT(4) /* Input enabled */

/*
 * BMI270_REG_INT_LATCH (0x55). Bosch's own step_counter_hw_int.c example
 * explicitly configures this to non-latched; our driver never wrote it at
 * all before, leaving it at whatever the reset/config-file default is.
 */
#define BMI270_INT_LATCH_MASK		BIT(0)
#define BMI270_INT_NON_LATCHED		0
#define BMI270_INT_LATCHED		BIT(0)

/* Applies to INT1_MAP_FEAT, INT2_MAP_FEAT, INT_STATUS_0 */
#define BMI270_INT_MAP_SIG_MOTION        BIT(0)
#define BMI270_INT_MAP_STEP_COUNTER      BIT(1)
#define BMI270_INT_MAP_ACTIVITY          BIT(2)
#define BMI270_INT_MAP_WRIST_WEAR_WAKEUP BIT(3)
#define BMI270_INT_MAP_WRIST_GESTURE     BIT(4)
#define BMI270_INT_MAP_NO_MOTION         BIT(5)
#define BMI270_INT_MAP_ANY_MOTION        BIT(6)

#define BMI270_INT_MAP_DATA_FFULL_INT1		BIT(0)
#define BMI270_INT_MAP_DATA_FWM_INT1		BIT(1)
#define BMI270_INT_MAP_DATA_DRDY_INT1		BIT(2)
#define BMI270_INT_MAP_DATA_ERR_INT1		BIT(3)
#define BMI270_INT_MAP_DATA_FFULL_INT2		BIT(4)
#define BMI270_INT_MAP_DATA_FWM_INT2		BIT(5)
#define BMI270_INT_MAP_DATA_DRDY_INT2		BIT(6)
#define BMI270_INT_MAP_DATA_ERR_INT2		BIT(7)

#define BMI270_INT_STATUS_ANY_MOTION		BIT(6)
#define BMI270_INT_STATUS_STEP_COUNTER		BIT(1)
/*
 * Step-activity (still/walking/running classifier) has its OWN interrupt
 * status bit, separate from the step counter/detector - it fires on a
 * classification *change*, not on every step.
 */
#define BMI270_INT_STATUS_ACTIVITY		BIT(2)

/*
 * Step-detector/step-counter/step-activity share one enable word at
 * feature page 6, register 0x32 (= FEATURES_0 (0x30) + BMI270 "STEP_CNT_4"
 * block start address 0x02, per Bosch's BMI270 Sensor API bmi270.h /
 * bmi2_defs.h). Bit positions below are derived the same way the existing
 * ANYMO_2 enable bit was: absolute byte offset within the feature page
 * (block start + feature's FEAT_EN offset), rounded down to the containing
 * 16-bit word, with the byte's bit position shifted up by 8 if it lands in
 * the odd (high) byte of that word:
 *   - step counter:  FEAT_EN offset 0x01, bit 4  -> word bit 8+4  = 12
 *   - step detector: FEAT_EN offset 0x01, bit 3  -> word bit 8+3  = 11
 *   - step activity: FEAT_EN offset 0x01, bit 5  -> word bit 8+5  = 13
 * NOTE: these offsets come from the upstream Bosch sensor API and have not
 * been independently verified against the exact config-file blob vendored
 * in bmi270_config_file.h. Confirm on hardware (SC_OUT_0 incrementing,
 * interrupt firing) before relying on this in production.
 */
#define BMI270_STEP_CNT_FEAT_PAGE		6
#define BMI270_STEP_CNT_FEAT_ADDR		0x32
#define BMI270_STEP_CNT_FEAT_EN_STEP_DET	BIT(11)
#define BMI270_STEP_CNT_FEAT_EN_STEP_COUNT	BIT(12)
#define BMI270_STEP_CNT_FEAT_EN_STEP_ACT	BIT(13)

/*
 * Step-counter watermark/reset-counter word: feature page 3, register 0x30
 * (FEATURES_0 (0x30) + BMI270_STEP_CNT_1_STRT_ADDR (0x00) per Bosch's
 * bmi270.h). Confirmed against Bosch's official step_counter_hw_int.c
 * example - watermark_level is a plain runtime-writable field, not
 * something fixed by the firmware config-file blob. Hardware resolution is
 * 20 steps per LSB (watermark_level = 1 means "interrupt every 20 steps").
 * Bit 10 of the same word resets the running count to 0 when set.
 */
#define BMI270_STEP_CNT_PARAMS_FEAT_PAGE	3
#define BMI270_STEP_CNT_PARAMS_FEAT_ADDR	0x30
#define BMI270_STEP_CNT_WM_LEVEL_MASK		GENMASK(9, 0)
#define BMI270_STEP_CNT_RST_CNT		BIT(10)

/*
 * INT1_MAP_FEAT/INT_STATUS_0 bit used for the step counter/detector. This is
 * per-variant: base/max_fifo have a sig-motion feature occupying bit 0, so
 * step counter/detector lands on bit 1 (BMI270_INT_MAP_STEP_COUNTER above).
 * The "context" blob has no sig-motion feature, so its step counter/detector
 * moves down to bit 0 instead - confirmed from bmi270_context.c's internal
 * feature-to-interrupt-bit table, not just carried over from base/max_fifo.
 */
#define BMI270_STEP_CNT_INT_BIT_DEFAULT	BMI270_INT_MAP_STEP_COUNTER
#define BMI270_CONTEXT_STEP_CNT_INT_BIT	BIT(0)

/*
 * "context" config-file variant (v2.86.1, see bmi270_config_file.h) - unlike
 * base/max_fifo, its feature memory layout puts the step counter/detector
 * enable word and the step-counter-params (watermark) word on different
 * pages than base/max_fifo use. The byte offsets within those pages
 * (BMI270_STEP_CNT_FEAT_ADDR / BMI270_STEP_CNT_PARAMS_FEAT_ADDR above) are
 * numerically identical though - confirmed against bmi270_context.c's
 * feature I/O table.
 */
#define BMI270_CONTEXT_STEP_CNT_FEAT_PAGE		4
#define BMI270_CONTEXT_STEP_CNT_PARAMS_FEAT_PAGE	1

/*
 * "context"-only feature: BMI2_ACTIVITY_RECOGNITION (still/walking/running/
 * on_bicycle/in_vehicle/tilted classifier). Unlike the step-activity guess
 * used by base/max_fifo (BMI270_STEP_CNT_FEAT_EN_STEP_ACT, a plain polled
 * register), this is read out via FIFO virtual frames - see
 * BMI270_FIFO_HEADER_ACT_RECOG_FRM below - and has NO associated hardware
 * interrupt at all (bmi270_context_map_feat_int() in Bosch's API only maps
 * step counter/detector to INT1/INT2). Enable bit is at byte offset
 * start_addr directly (no +1 like the step-counter word), bit 0 - confirmed
 * from bmi270_context.c's set_act_recog().
 */
#define BMI270_CONTEXT_ACT_RECOG_FEAT_PAGE	5
#define BMI270_CONTEXT_ACT_RECOG_FEAT_ADDR	0x3A
#define BMI270_CONTEXT_ACT_RECOG_EN_MASK	BIT(0)

/*
 * FIFO_CONFIG is a 16-bit register spanning two 8-bit addresses
 * (BMI270_REG_FIFO_CONFIG_0 = LSB, BMI270_REG_FIFO_CONFIG_1 = MSB). Header
 * mode (BMI2_FIFO_HEADER_EN = 0x1000 in Bosch's bmi2_defs.h) is bit 12
 * overall = bit 4 of the MSB byte. Needed so activity-recognition virtual
 * frames in the FIFO are distinguishable by a header byte instead of being
 * indistinguishable raw sensor data.
 */
#define BMI270_FIFO_CONFIG_1_HEADER_EN		BIT(4)

/*
 * FIFO frame header bytes (top of a byte written into the FIFO stream ahead
 * of that frame's payload). BMI270_FIFO_HEADER_ACT_RECOG_FRM is Bosch's
 * BMI2_FIFO_VIRT_ACT_RECOG_FRM; the 6-byte payload that follows it is a
 * 4-byte little-endian timestamp, then prev_act, then curr_act (confirmed
 * from bmi270_context.c's unpack_act_recog_output()).
 * BMI270_FIFO_HEADER_EMPTY_FRM marks "no more valid data" at the end of
 * whatever was read out of the FIFO.
 */
#define BMI270_FIFO_HEADER_ACT_RECOG_FRM	0xC8
#define BMI270_FIFO_HEADER_EMPTY_FRM		0x80
#define BMI270_FIFO_ACT_RECOG_FRM_LEN		7 /* 1 header byte + 6 payload bytes */

/*
 * Activity classes as reported by BMI2_ACTIVITY_RECOGNITION, per Bosch's
 * enum bmi2_act_recog_type (bmi2_defs.h) - numeric values matter here, this
 * is exactly what the chip puts in the curr_act/prev_act payload bytes.
 * Naming for 0-5 taken from the user-supplied
 * `activity_reg_output[6] = {"OTHERS","STILL","WALKING","RUNNING",
 * "ON_BICYCLE","IN_VEHICLE"}`; BMI270_ACTIVITY_TILTED (6) is Bosch's, not in
 * that 6-entry list - callers indexing a display name table by this enum
 * should size it for 7 entries.
 */
enum bmi270_activity_recog {
	BMI270_ACTIVITY_OTHERS = 0,
	BMI270_ACTIVITY_STILL = 1,
	BMI270_ACTIVITY_WALKING = 2,
	BMI270_ACTIVITY_RUNNING = 3,
	BMI270_ACTIVITY_ON_BICYCLE = 4,
	BMI270_ACTIVITY_IN_VEHICLE = 5,
	BMI270_ACTIVITY_TILTED = 6,
};

/*
 * Custom trigger for the on-chip step counter/detector, as requested:
 * SENSOR_TRIG_PRIV_START + 1 (SENSOR_TRIG_PRIV_START itself is left free
 * for a possible future sensor-specific trigger).
 */
#define BMI270_SENSOR_TRIG_STEP		(SENSOR_TRIG_PRIV_START + 1)

/*
 * Custom trigger for the on-chip step-activity classifier (still/walking/
 * running). Fires on a classification change, not per-step.
 */
#define BMI270_SENSOR_TRIG_ACTIVITY	(SENSOR_TRIG_PRIV_START + 2)

/*
 * BMI270_REG_WR_GEST_ACT (0x20) - "Wrist Gesture + ACTivity" combined
 * output register. Per Bosch's bmi270.h, the step-activity output lives at
 * feature-output offset BMI270_STEP_ACT_OUT_STRT_ADDR (0x04) while wrist
 * gesture is at offset 0x06 - both packed into this one byte rather than
 * two separate top-level registers like SC_OUT_0.
 *
 * UNVERIFIED: the exact bit range for the activity value within this byte
 * is a best-effort guess (bits[1:0], the most common Bosch activity-code
 * convention: 0=still, 1=walking, 2=running, 3=invalid) - I could not
 * confirm it against Bosch's internal bmi2_common.c extraction tables from
 * headers alone. bmi270_step_activity_get() returns the *raw* byte as well
 * as the decoded guess specifically so this can be checked empirically:
 * read the raw value standing still, then walking, then running/shaking,
 * and confirm which bits actually change. Adjust
 * BMI270_STEP_ACTIVITY_*_MASK/POS below if the raw byte doesn't match.
 */
#define BMI270_STEP_ACTIVITY_MASK	GENMASK(1, 0)
#define BMI270_STEP_ACTIVITY_POS	0

enum bmi270_step_activity {
	BMI270_STEP_ACTIVITY_STILL = 0,
	BMI270_STEP_ACTIVITY_WALKING = 1,
	BMI270_STEP_ACTIVITY_RUNNING = 2,
	BMI270_STEP_ACTIVITY_INVALID = 3,
};

/*
 * Custom attribute controlling the step-counter watermark (in units of 20
 * steps, see above). Not tied to a specific channel - pass any channel to
 * sensor_attr_set(), e.g. SENSOR_CHAN_ALL.
 */
#define BMI270_SENSOR_ATTR_STEP_WM	(SENSOR_ATTR_PRIV_START + 1)

#define BMI270_CHIP_ID 0x24

#define BMI270_CMD_G_TRIGGER  0x02
#define BMI270_CMD_USR_GAIN   0x03
#define BMI270_CMD_NVM_PROG   0xA0
#define BMI270_CMD_FIFO_FLUSH OxB0
#define BMI270_CMD_SOFT_RESET 0xB6

#define BMI270_POWER_ON_TIME                500
#define BMI270_SOFT_RESET_TIME              2000
#define BMI270_ACC_SUS_TO_NOR_START_UP_TIME 2000
#define BMI270_GYR_SUS_TO_NOR_START_UP_TIME 45000
#define BMI270_GYR_FAST_START_UP_TIME       2000
#define BMI270_TRANSC_DELAY_SUSPEND         450
#define BMI270_TRANSC_DELAY_NORMAL          2

#define BMI270_PREPARE_CONFIG_LOAD  0x00
#define BMI270_COMPLETE_CONFIG_LOAD 0x01

#define BMI270_INST_MESSAGE_MSK        0x0F
#define BMI270_INST_MESSAGE_NOT_INIT   0x00
#define BMI270_INST_MESSAGE_INIT_OK    0x01
#define BMI270_INST_MESSAGE_INIT_ERR   0x02
#define BMI270_INST_MESSAGE_DRV_ERR    0x03
#define BMI270_INST_MESSAGE_SNS_STOP   0x04
#define BMI270_INST_MESSAGE_NVM_ERR    0x05
#define BMI270_INST_MESSAGE_STRTUP_ERR 0x06
#define BMI270_INST_MESSAGE_COMPAT_ERR 0x07

#define BMI270_INST_AXES_REMAP_ERROR 0x20
#define BMI270_INST_ODR_50HZ_ERROR   0x40

#define BMI270_PWR_CONF_ADV_PWR_SAVE_MSK 0x01
#define BMI270_PWR_CONF_ADV_PWR_SAVE_EN  0x01
#define BMI270_PWR_CONF_ADV_PWR_SAVE_DIS 0x00

#define BMI270_PWR_CONF_FIFO_SELF_WKUP_MSK 0x02
#define BMI270_PWR_CONF_FIFO_SELF_WKUP_POS 0x01
#define BMI270_PWR_CONF_FIFO_SELF_WKUP_EN  0x01
#define BMI270_PWR_CONF_FIFO_SELF_WKUP_DIS 0x00

#define BMI270_PWR_CONF_FUP_EN_MSK 0x04
#define BMI270_PWR_CONF_FUP_EN_POS 0x02
#define BMI270_PWR_CONF_FUP_EN     0x01
#define BMI270_PWR_CONF_FUP_DIS    0x00

#define BMI270_PWR_CTRL_MSK     0x0F
#define BMI270_PWR_CTRL_AUX_EN  0x01
#define BMI270_PWR_CTRL_GYR_EN  0x02
#define BMI270_PWR_CTRL_ACC_EN  0x04
#define BMI270_PWR_CTRL_TEMP_EN 0x08

#define BMI270_ACC_ODR_MSK      0x0F
#define BMI270_ACC_ODR_25D32_HZ 0x01
#define BMI270_ACC_ODR_25D16_HZ 0x02
#define BMI270_ACC_ODR_25D8_HZ  0x03
#define BMI270_ACC_ODR_25D4_HZ  0x04
#define BMI270_ACC_ODR_25D2_HZ  0x05
#define BMI270_ACC_ODR_25_HZ    0x06
#define BMI270_ACC_ODR_50_HZ    0x07
#define BMI270_ACC_ODR_100_HZ   0x08
#define BMI270_ACC_ODR_200_HZ   0x09
#define BMI270_ACC_ODR_400_HZ   0x0A
#define BMI270_ACC_ODR_800_HZ   0x0B
#define BMI270_ACC_ODR_1600_HZ  0x0C

#define BMI270_ACC_BWP_MSK        0x30
#define BMI270_ACC_BWP_POS        4
#define BMI270_ACC_BWP_OSR4_AVG1  0x00
#define BMI270_ACC_BWP_OSR2_AVG2  0x01
#define BMI270_ACC_BWP_NORM_AVG4  0x02
#define BMI270_ACC_BWP_CIC_AVG8   0x03
#define BMI270_ACC_BWP_RES_AVG16  0x04
#define BMI270_ACC_BWP_RES_AVG32  0x05
#define BMI270_ACC_BWP_RES_AVG64  0x06
#define BMI270_ACC_BWP_RES_AVG128 0x07

#define BMI270_ACC_FILT_MSK      0x80
#define BMI270_ACC_FILT_POS      7
#define BMI270_ACC_FILT_PWR_OPT  0x00
#define BMI270_ACC_FILT_PERF_OPT 0x01

#define BMI270_ACC_RANGE_MSK 0x03
#define BMI270_ACC_RANGE_2G  0x00
#define BMI270_ACC_RANGE_4G  0x01
#define BMI270_ACC_RANGE_8G  0x02
#define BMI270_ACC_RANGE_16G 0x03

#define BMI270_GYR_ODR_MSK     0x0F
#define BMI270_GYR_ODR_25_HZ   0x06
#define BMI270_GYR_ODR_50_HZ   0x07
#define BMI270_GYR_ODR_100_HZ  0x08
#define BMI270_GYR_ODR_200_HZ  0x09
#define BMI270_GYR_ODR_400_HZ  0x0A
#define BMI270_GYR_ODR_800_HZ  0x0B
#define BMI270_GYR_ODR_1600_HZ 0x0C
#define BMI270_GYR_ODR_3200_HZ 0x0D

#define BMI270_GYR_BWP_MSK  0x30
#define BMI270_GYR_BWP_POS  4
#define BMI270_GYR_BWP_OSR4 0x00
#define BMI270_GYR_BWP_OSR2 0x01
#define BMI270_GYR_BWP_NORM 0x02

#define BMI270_GYR_FILT_NOISE_MSK      0x40
#define BMI270_GYR_FILT_NOISE_POS      6
#define BMI270_GYR_FILT_NOISE_PWR      0x00
#define BMI270_GYR_FILT_NOISE_PERF     0x01

#define BMI270_GYR_FILT_MSK      0x80
#define BMI270_GYR_FILT_POS      7
#define BMI270_GYR_FILT_PWR_OPT  0x00
#define BMI270_GYR_FILT_PERF_OPT 0x01

#define BMI270_GYR_RANGE_MSK     0x07
#define BMI270_GYR_RANGE_2000DPS 0x00
#define BMI270_GYR_RANGE_1000DPS 0x01
#define BMI270_GYR_RANGE_500DPS  0x02
#define BMI270_GYR_RANGE_250DPS  0x03
#define BMI270_GYR_RANGE_125DPS  0x04

#define BMI270_GYR_OIS_RANGE_MSK     0x80
#define BMI270_GYR_OIS_RANGE_POS     3
#define BMI270_GYR_OIS_RANGE_250DPS  0x00
#define BMI270_GYR_OIS_RANGE_2000DPS 0x01

#define BMI270_SET_BITS(reg_data, bitname, data)		  \
	((reg_data & ~(bitname##_MSK)) | ((data << bitname##_POS) \
					  & bitname##_MSK))
#define BMI270_SET_BITS_POS_0(reg_data, bitname, data) \
	((reg_data & ~(bitname##_MSK)) | (data & bitname##_MSK))

struct bmi270_data {
	int16_t ax, ay, az, gx, gy, gz;
	uint8_t acc_range, acc_odr, gyr_odr;
	uint16_t gyr_range;

#if CONFIG_BMI270_TRIGGER
	const struct device *dev;
	struct k_mutex trigger_mutex;
	sensor_trigger_handler_t motion_handler;
	const struct sensor_trigger *motion_trigger;
	sensor_trigger_handler_t drdy_handler;
	const struct sensor_trigger *drdy_trigger;
	sensor_trigger_handler_t step_handler;
	const struct sensor_trigger *step_trigger;
	sensor_trigger_handler_t activity_handler;
	const struct sensor_trigger *activity_trigger;
	struct gpio_callback int1_cb;
	struct gpio_callback int2_cb;
	atomic_t int_flags;
	uint16_t anymo_1;
	uint16_t anymo_2;
	uint16_t step_wm_level;

	/*
	 * "context" variant activity recognition: no hardware interrupt
	 * exists for this feature, so it's driven by periodically polling
	 * the FIFO instead (see activity_poll_work in bmi270_trigger.c).
	 * activity_curr/prev/timestamp cache the latest decoded frame,
	 * protected by trigger_mutex like the rest of this struct.
	 */
	struct k_work_delayable activity_poll_work;
	enum bmi270_activity_recog activity_curr;
	enum bmi270_activity_recog activity_prev;
	uint32_t activity_timestamp;
	bool activity_data_valid;

#if CONFIG_BMI270_TRIGGER_OWN_THREAD
	struct k_sem trig_sem;

	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_BMI270_THREAD_STACK_SIZE);
	struct k_thread thread;

#elif CONFIG_BMI270_TRIGGER_GLOBAL_THREAD
	struct k_work trig_work;
#endif
#endif /* CONFIG_BMI270_TRIGGER */
};

struct bmi270_feature_reg {
	/* Which feature page the register resides in */
	uint8_t page;
	uint8_t addr;
};

struct bmi270_feature_config {
	const char *name;
	const uint8_t *config_file;
	size_t config_file_len;
	struct bmi270_feature_reg *anymo_1;
	struct bmi270_feature_reg *anymo_2;
	struct bmi270_feature_reg *step_cnt_en;
	struct bmi270_feature_reg *step_cnt_params;
	/*
	 * INT1_MAP_FEAT/INT_STATUS_0 bit for the step counter/detector -
	 * per-variant, see BMI270_STEP_CNT_INT_BIT_DEFAULT /
	 * BMI270_CONTEXT_STEP_CNT_INT_BIT.
	 */
	uint8_t step_cnt_int_bit;
	/*
	 * Only non-NULL for the "context" blob: enable bit for the real
	 * BMI2_ACTIVITY_RECOGNITION feature (FIFO-based, no interrupt). NULL
	 * for base/max_fifo, which instead (mis)use step_cnt_en's
	 * BMI270_STEP_CNT_FEAT_EN_STEP_ACT bit - see bmi270_activity_config()
	 * in bmi270_trigger.c for how this selects between the two.
	 */
	struct bmi270_feature_reg *act_recog_en;
};

union bmi270_bus {
#if CONFIG_BMI270_BUS_SPI
	struct spi_dt_spec spi;
#endif
#if CONFIG_BMI270_BUS_I2C
	struct i2c_dt_spec i2c;
#endif
};

typedef int (*bmi270_bus_check_fn)(const union bmi270_bus *bus);
typedef int (*bmi270_bus_init_fn)(const union bmi270_bus *bus);
typedef int (*bmi270_reg_read_fn)(const union bmi270_bus *bus,
				  uint8_t start,
				  uint8_t *data,
				  uint16_t len);
typedef int (*bmi270_reg_write_fn)(const union bmi270_bus *bus,
				   uint8_t start,
				   const uint8_t *data,
				   uint16_t len);

struct bmi270_bus_io {
	bmi270_bus_check_fn check;
	bmi270_reg_read_fn read;
	bmi270_reg_write_fn write;
	bmi270_bus_init_fn init;
};

struct bmi270_config {
	union bmi270_bus bus;
	const struct bmi270_bus_io *bus_io;
	const struct bmi270_feature_config *feature;
#if CONFIG_BMI270_TRIGGER
	struct gpio_dt_spec int1;
	struct gpio_dt_spec int2;
#endif
};

#if CONFIG_BMI270_BUS_SPI
#define BMI270_SPI_OPERATION (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define BMI270_SPI_ACC_DELAY_US 2
extern const struct bmi270_bus_io bmi270_bus_io_spi;
#endif

#if CONFIG_BMI270_BUS_I2C
extern const struct bmi270_bus_io bmi270_bus_io_i2c;
#endif

int bmi270_reg_read(const struct device *dev, uint8_t reg, uint8_t *data, uint16_t length);

int bmi270_reg_write(const struct device *dev, uint8_t reg,
		     const uint8_t *data, uint16_t length);

int bmi270_reg_write_with_delay(const struct device *dev,
				uint8_t reg,
				const uint8_t *data,
				uint16_t length,
				uint32_t delay_us);

#ifdef CONFIG_BMI270_TRIGGER
int bmi270_trigger_set(const struct device *dev,
		       const struct sensor_trigger *trig,
		       sensor_trigger_handler_t handler);

int bmi270_init_interrupts(const struct device *dev);
#endif

/*
 * Read the running step count from the on-chip step counter (SC_OUT_0,
 * a plain 16-bit little-endian register - no feature-page switch needed).
 * Only meaningful once the step counter feature has been enabled via a
 * BMI270_SENSOR_TRIG_STEP trigger_set() call.
 */
int bmi270_step_count_get(const struct device *dev, uint32_t *count);

/*
 * Read the step-activity classifier output (BMI270_REG_WR_GEST_ACT). Only
 * meaningful once enabled via a BMI270_SENSOR_TRIG_ACTIVITY trigger_set()
 * call. Returns the raw register byte in *raw (for empirically checking the
 * bit-decode - see the comment above BMI270_STEP_ACTIVITY_MASK) and the
 * best-effort decoded classification in *activity.
 */
int bmi270_step_activity_get(const struct device *dev, uint8_t *raw,
			     enum bmi270_step_activity *activity);

#ifdef CONFIG_BMI270_TRIGGER
/*
 * Read the latest BMI2_ACTIVITY_RECOGNITION classification ("context" blob
 * only - see struct bmi270_feature_config::act_recog_en). Unlike
 * bmi270_step_activity_get(), this isn't tied to a polled register: the
 * driver maintains *curr/*prev/*timestamp by periodically draining the
 * FIFO in the background once a BMI270_SENSOR_TRIG_ACTIVITY trigger_set()
 * call enables it. Returns -EAGAIN if no frame has been decoded yet.
 * Any of curr/prev/timestamp may be NULL if not needed.
 */
int bmi270_activity_recognition_get(const struct device *dev,
				    enum bmi270_activity_recog *curr,
				    enum bmi270_activity_recog *prev,
				    uint32_t *timestamp);
#endif

#endif /* ZEPHYR_DRIVERS_SENSOR_BMI270_BMI270_H_ */
