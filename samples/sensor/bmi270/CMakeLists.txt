#
# Copyright (c) 2021 Bosch Sensortec GmbH
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# bmi270.h (BMI270_SENSOR_TRIG_STEP, BMI270_SENSOR_ATTR_STEP_WM,
# bmi270_step_count_get()) is the driver's private header, not part of the
# public Zephyr sensor API. This is a local, not-yet-upstreamed extension
# for the step counter, so pull the driver directory onto the app's
# include path rather than duplicating declarations here.
zephyr_include_directories(${ZEPHYR_BASE}/drivers/sensor/bosch/bmi270)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
