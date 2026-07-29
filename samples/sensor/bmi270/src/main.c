/*
 * Copyright (c) 2021 Bosch Sensortec GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

/*
 * Driver-private header (not part of the public Zephyr sensor API) that
 * adds a step-counter trigger and readout API on top of the upstream
 * bmi270 driver. See CMakeLists.txt for how this gets onto the include
 * path.
 */
#include <bmi270.h>

/*
 * Own module name (distinct from the driver's own "bmi270" module) so log
 * output is tagged per-source and timestamped, e.g.
 * "[00:00:03.360,000] <inf> bmi270_sample: Step count: 20"
 */
LOG_MODULE_REGISTER(bmi270_sample, LOG_LEVEL_INF);

/*
 * Fires on the BMI270's on-chip step-counter interrupt (INT1). By default
 * this is every 20 steps (watermark_level = 1, see BMI270_SENSOR_ATTR_STEP_WM
 * below) - a hardware quantization of the step counter, not a driver
 * limitation.
 */
static void step_trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	uint32_t steps;
	int ret;

	ARG_UNUSED(trig);

	ret = bmi270_step_count_get(dev, &steps);
	if (ret != 0) {
		LOG_ERR("Failed to read step count (%d)", ret);
		return;
	}

	LOG_INF("Step count: %u", steps);
}

/*
 * Fires on the BMI270 "context" blob's real BMI2_ACTIVITY_RECOGNITION
 * classifier every time the driver drains a new activity frame out of the
 * FIFO (see bmi270_activity_poll_work_cb() in bmi270_trigger.c - there is no
 * hardware interrupt for this feature, so it's polled internally on a
 * timer). curr/prev report the classification right after/before the
 * change; timestamp is the sensor's own internal clock, not wall time.
 */
static void activity_trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	/* Bosch's enum bmi2_act_recog_type values 0-6; see bmi270.h. */
	static const char *const activity_name[] = {
		[BMI270_ACTIVITY_OTHERS] = "OTHERS",
		[BMI270_ACTIVITY_STILL] = "STILL",
		[BMI270_ACTIVITY_WALKING] = "WALKING",
		[BMI270_ACTIVITY_RUNNING] = "RUNNING",
		[BMI270_ACTIVITY_ON_BICYCLE] = "ON_BICYCLE",
		[BMI270_ACTIVITY_IN_VEHICLE] = "IN_VEHICLE",
		[BMI270_ACTIVITY_TILTED] = "TILTED",
	};
	enum bmi270_activity_recog curr, prev;
	uint32_t timestamp;
	int ret;

	ARG_UNUSED(trig);

	ret = bmi270_activity_recognition_get(dev, &curr, &prev, &timestamp);
	if (ret != 0) {
		LOG_ERR("Failed to read activity recognition (%d)", ret);
		return;
	}

	LOG_INF("Activity changed: %s -> %s (sensor time %u)",
		activity_name[prev], activity_name[curr], timestamp);
}

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bmi270);
	struct sensor_value full_scale, sampling_freq, oversampling;
	struct sensor_value step_wm;
	struct sensor_trigger step_trig = {
		.type = BMI270_SENSOR_TRIG_STEP,
		.chan = SENSOR_CHAN_ALL,
	};
	struct sensor_trigger activity_trig = {
		.type = BMI270_SENSOR_TRIG_ACTIVITY,
		.chan = SENSOR_CHAN_ALL,
	};
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("Device %s is not ready", dev->name);
		return 0;
	}

	LOG_INF("Device %p name is %s", (void *)dev, dev->name);

	/* Setting scale in G, due to loss of precision if the SI unit m/s^2
	 * is used
	 */
	full_scale.val1 = 2;            /* G */
	full_scale.val2 = 0;
	sampling_freq.val1 = 100;       /* Hz. Performance mode */
	sampling_freq.val2 = 0;
	oversampling.val1 = 1;          /* Normal mode */
	oversampling.val2 = 0;

	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE,
			&full_scale);
	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_OVERSAMPLING,
			&oversampling);
	/* Set sampling frequency last as this also sets the appropriate
	 * power mode. If already sampling, change to 0.0Hz before changing
	 * other attributes
	 */
	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY,
			&sampling_freq);


	/* Setting scale in degrees/s to match the sensor scale */
	full_scale.val1 = 500;          /* dps */
	full_scale.val2 = 0;
	sampling_freq.val1 = 100;       /* Hz. Performance mode */
	sampling_freq.val2 = 0;
	oversampling.val1 = 1;          /* Normal mode */
	oversampling.val2 = 0;

	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE,
			&full_scale);
	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_OVERSAMPLING,
			&oversampling);
	/* Set sampling frequency last as this also sets the appropriate
	 * power mode. If already sampling, change sampling frequency to
	 * 0.0Hz before changing other attributes
	 */
	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY,
			&sampling_freq);

	/* Step counter interrupt: fire every 20 steps (watermark_level = 1).
	 * Raise this to batch more steps per interrupt, e.g. val1 = 5 for
	 * every 100 steps.
	 */
	step_wm.val1 = 1;
	step_wm.val2 = 0;
	ret = sensor_attr_set(dev, SENSOR_CHAN_ALL, BMI270_SENSOR_ATTR_STEP_WM, &step_wm);
	if (ret != 0) {
		LOG_ERR("Failed to set step counter watermark (%d)", ret);
	}

	ret = sensor_trigger_set(dev, &step_trig, step_trigger_handler);
	if (ret != 0) {
		LOG_ERR("Failed to set step counter trigger (%d)", ret);
	}

	ret = sensor_trigger_set(dev, &activity_trig, activity_trigger_handler);
	if (ret != 0) {
		LOG_ERR("Failed to set step activity trigger (%d)", ret);
	}

	k_sleep(K_FOREVER);

	return 0;
}
