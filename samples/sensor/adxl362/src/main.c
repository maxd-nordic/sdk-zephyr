/*
 * Copyright (c) 2019 Brett Witherspoon
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <stdio.h>
#include <device.h>
#include <drivers/sensor.h>

int adxl362_get_status(const struct device *dev, uint8_t *status);
int adxl362_get_pwrctl(const struct device *dev, uint8_t *status);
//int adxl362_set_power_mode(const struct device *dev, uint8_t measure_on, uint8_t wakeup, uint8_t autosleep);

K_SEM_DEFINE(sem, 0, 1);

static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_DATA_READY:
		if (sensor_sample_fetch(dev) < 0) {
			printf("Sample fetch error\n");
			return;
		}
		k_sem_give(&sem);
		break;
	case SENSOR_TRIG_THRESHOLD:
		printf("Threshold trigger\n");
		break;
	default:
		printf("Unknown trigger\n");
	}
}

void main(void)
{
	struct sensor_value accel[3];

	const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, adi_adxl362)));
	if (dev == NULL) {
		printf("Device get binding device\n");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printf("Trigger set error\n");
			return;
		}

		trig.type = SENSOR_TRIG_DATA_READY;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printf("Trigger set error\n");
		}
	}

	while (true) {
		if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
			k_sem_take(&sem, K_FOREVER);
		} else {
			k_sleep(K_MSEC(1000));
			if (sensor_sample_fetch(dev) < 0) {
				printf("Sample fetch error\n");
				return;
			}
		}

		if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, &accel[0]) < 0) {
			printf("Channel get error\n");
			return;
		}

		uint8_t status = 0;
		if (adxl362_get_status(dev, &status)) {
			printf("Status get error\n");
			return;
		}
		uint8_t pwrctl = 0;
		if (adxl362_get_pwrctl(dev, &pwrctl)) {
			printf("pwrctl get error\n");
			return;
		}

		printf("x: % 06.1f, y: % 06.1f, z: % 06.1f (m/s^2), status: %u%u%u%u%u%u%u, pwrctl: %u%u%u%u%u%u%u\r\n",
		       sensor_value_to_double(&accel[0]),
		       sensor_value_to_double(&accel[1]),
		       sensor_value_to_double(&accel[2]),
			   (status>>7)&1,
			   (status>>6)&1,
			   (status>>5)&1,
			   (status>>4)&1,
			   (status>>3)&1,
			   (status>>2)&1,
			   (status>>1)&1,
			   (status>>0)&1,
			   (pwrctl>>7)&1,
			   (pwrctl>>6)&1,
			   (pwrctl>>5)&1,
			   (pwrctl>>4)&1,
			   (pwrctl>>3)&1,
			   (pwrctl>>2)&1,
			   (pwrctl>>1)&1,
			   (pwrctl>>0)&1);
	}
}
