/*
 * Copyright (c) 2023 Elektronikutvecklingsbyrån EUB AB
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
LOG_MODULE_DECLARE(bmi270);

#include "bmi270.h"

/*
 * Poll period for "context"-variant activity recognition (no hardware
 * interrupt exists for this feature - see BMI270_CONTEXT_ACT_RECOG_FEAT_PAGE
 * in bmi270.h - so it's drained out of the FIFO on a timer instead).
 */
#define BMI270_ACTIVITY_POLL_INTERVAL K_MSEC(500)

enum {
	INT_FLAGS_INT1,
	INT_FLAGS_INT2,
};

static void bmi270_raise_int_flag(const struct device *dev, int bit)
{
	struct bmi270_data *data = dev->data;

	atomic_set_bit(&data->int_flags, bit);

#if defined(CONFIG_BMI270_TRIGGER_OWN_THREAD)
	k_sem_give(&data->trig_sem);
#elif defined(CONFIG_BMI270_TRIGGER_GLOBAL_THREAD)
	k_work_submit(&data->trig_work);
#endif
}

static void bmi270_int1_callback(const struct device *dev,
				 struct gpio_callback *cb, uint32_t pins)
{
	struct bmi270_data *data =
		CONTAINER_OF(cb, struct bmi270_data, int1_cb);
	bmi270_raise_int_flag(data->dev, INT_FLAGS_INT1);
}

static void bmi270_int2_callback(const struct device *dev,
				 struct gpio_callback *cb, uint32_t pins)
{
	struct bmi270_data *data =
		CONTAINER_OF(cb, struct bmi270_data, int2_cb);
	bmi270_raise_int_flag(data->dev, INT_FLAGS_INT2);
}


static void bmi270_thread_cb(const struct device *dev)
{
	struct bmi270_data *data = dev->data;
	const struct bmi270_config *cfg = dev->config;
	int ret;

	/* INT1 is used for feature interrupts */
	if (atomic_test_and_clear_bit(&data->int_flags, INT_FLAGS_INT1)) {
		uint16_t int_status;

		ret = bmi270_reg_read(dev, BMI270_REG_INT_STATUS_0,
			(uint8_t *)&int_status, sizeof(int_status));
		if (ret < 0) {
			LOG_ERR("read interrupt status returned %d", ret);
			return;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);

		if (data->motion_handler != NULL) {
			if (int_status & BMI270_INT_STATUS_ANY_MOTION) {
				data->motion_handler(dev, data->motion_trigger);
			}
		}

		if (data->step_handler != NULL) {
			if (int_status & cfg->feature->step_cnt_int_bit) {
				data->step_handler(dev, data->step_trigger);
			}
		}

		/*
		 * Only base/max_fifo route step-activity to a real interrupt
		 * bit this way. "context" has no interrupt for
		 * BMI2_ACTIVITY_RECOGNITION at all - its activity_handler is
		 * instead invoked straight from the FIFO poll worker below,
		 * so act_recog_en != NULL means this bit is meaningless here.
		 */
		if ((data->activity_handler != NULL) && (cfg->feature->act_recog_en == NULL)) {
			if (int_status & BMI270_INT_STATUS_ACTIVITY) {
				data->activity_handler(dev, data->activity_trigger);
			}
		}

		k_mutex_unlock(&data->trigger_mutex);
	}

	/* INT2 is used for data ready interrupts */
	if (atomic_test_and_clear_bit(&data->int_flags, INT_FLAGS_INT2)) {
		k_mutex_lock(&data->trigger_mutex, K_FOREVER);

		if (data->drdy_handler != NULL) {
			data->drdy_handler(dev, data->drdy_trigger);
		}

		k_mutex_unlock(&data->trigger_mutex);
	}
}

#ifdef CONFIG_BMI270_TRIGGER_OWN_THREAD
static void bmi270_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct bmi270_data *data = p1;

	while (1) {
		k_sem_take(&data->trig_sem, K_FOREVER);
		bmi270_thread_cb(data->dev);
	}
}
#endif

#ifdef CONFIG_BMI270_TRIGGER_GLOBAL_THREAD
static void bmi270_trig_work_cb(struct k_work *work)
{
	struct bmi270_data *data =
		CONTAINER_OF(work, struct bmi270_data, trig_work);

	bmi270_thread_cb(data->dev);
}
#endif

static int bmi270_feature_reg_write(const struct device *dev,
			     const struct bmi270_feature_reg *reg,
			     uint16_t value)
{
	int ret;
	uint8_t feat_page = reg->page;

	ret = bmi270_reg_write(dev, BMI270_REG_FEAT_PAGE, &feat_page, 1);
	if (ret < 0) {
		LOG_ERR("bmi270_reg_write (0x%02x) failed: %d", BMI270_REG_FEAT_PAGE, ret);
		return ret;
	}

	LOG_DBG("feature reg[0x%02x]@%d = 0x%04x", reg->addr, reg->page, value);

	ret = bmi270_reg_write(dev, reg->addr, (uint8_t *)&value, 2);
	if (ret < 0) {
		LOG_ERR("bmi270_reg_write (0x%02x) failed: %d", reg->addr, ret);
		return ret;
	}

	return 0;
}

/*
 * Bosch's own reference API (bmi270_get_sensor_config() / ..._set_sensor_config())
 * always reads a feature-config word before modifying it, so any bits the
 * config-file blob has already populated (or bits used by a *different*
 * feature sharing the same word, e.g. step-activity sharing the step
 * counter/detector enable word) survive untouched. bmi270_feature_reg_write()
 * above instead writes from scratch every time. Use this read-modify-write
 * helper wherever we only mean to touch a subset of a feature word's bits.
 */
static int bmi270_feature_reg_read(const struct device *dev,
			     const struct bmi270_feature_reg *reg,
			     uint16_t *value)
{
	int ret;
	uint8_t feat_page = reg->page;

	ret = bmi270_reg_write(dev, BMI270_REG_FEAT_PAGE, &feat_page, 1);
	if (ret < 0) {
		LOG_ERR("bmi270_reg_write (0x%02x) failed: %d", BMI270_REG_FEAT_PAGE, ret);
		return ret;
	}

	ret = bmi270_reg_read(dev, reg->addr, (uint8_t *)value, 2);
	if (ret < 0) {
		LOG_ERR("bmi270_reg_read (0x%02x) failed: %d", reg->addr, ret);
		return ret;
	}

	LOG_DBG("feature reg[0x%02x]@%d -> 0x%04x", reg->addr, reg->page, *value);

	return 0;
}

static int bmi270_init_int_pin(const struct gpio_dt_spec *pin,
			       struct gpio_callback *pin_cb,
			       gpio_callback_handler_t handler)
{
	int ret;

	if (!pin->port) {
		return 0;
	}

	if (!device_is_ready(pin->port)) {
		LOG_DBG("%s not ready", pin->port->name);
		return -ENODEV;
	}

	gpio_init_callback(pin_cb, handler, BIT(pin->pin));

	ret = gpio_pin_configure_dt(pin, GPIO_INPUT);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(pin, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret) {
		return ret;
	}

	ret = gpio_add_callback(pin->port, pin_cb);
	if (ret) {
		return ret;
	}

	return 0;
}


int bmi270_init_interrupts(const struct device *dev)
{
	const struct bmi270_config *cfg = dev->config;
	struct bmi270_data *data = dev->data;
	int ret;

#if CONFIG_BMI270_TRIGGER_OWN_THREAD
	k_sem_init(&data->trig_sem, 0, 1);
	k_thread_create(&data->thread, data->thread_stack, CONFIG_BMI270_THREAD_STACK_SIZE,
			bmi270_thread, data, NULL, NULL,
			K_PRIO_COOP(CONFIG_BMI270_THREAD_PRIORITY), 0, K_NO_WAIT);
#elif CONFIG_BMI270_TRIGGER_GLOBAL_THREAD
	k_work_init(&data->trig_work, bmi270_trig_work_cb);
#endif

	ret = bmi270_init_int_pin(&cfg->int1, &data->int1_cb,
				  bmi270_int1_callback);
	if (ret) {
		LOG_ERR("Failed to initialize INT1");
		return -EINVAL;
	}

	ret = bmi270_init_int_pin(&cfg->int2, &data->int2_cb,
				  bmi270_int2_callback);
	if (ret) {
		LOG_ERR("Failed to initialize INT2");
		return -EINVAL;
	}

	if (cfg->int1.port) {
		uint8_t int1_io_ctrl = BMI270_INT_IO_CTRL_OUTPUT_EN;

		ret = bmi270_reg_write(dev, BMI270_REG_INT1_IO_CTRL, &int1_io_ctrl, 1);
		if (ret < 0) {
			LOG_ERR("failed configuring INT1_IO_CTRL (%d)", ret);
			return ret;
		}
	}

	if (cfg->int2.port) {
		uint8_t int2_io_ctrl = BMI270_INT_IO_CTRL_OUTPUT_EN;

		ret = bmi270_reg_write(dev, BMI270_REG_INT2_IO_CTRL, &int2_io_ctrl, 1);
		if (ret < 0) {
			LOG_ERR("failed configuring INT2_IO_CTRL (%d)", ret);
			return ret;
		}
	}

	if (cfg->int1.port || cfg->int2.port) {
		uint8_t int_latch = BMI270_INT_NON_LATCHED;

		ret = bmi270_reg_write(dev, BMI270_REG_INT_LATCH, &int_latch, 1);
		if (ret < 0) {
			LOG_ERR("failed configuring INT_LATCH (%d)", ret);
			return ret;
		}
	}

	return 0;
}

/*
 * INT1_MAP_FEAT is a single shared register: every feature interrupt that
 * routes to INT1 ORs its bit into the same byte. Recompute the whole byte
 * from currently-registered handlers instead of blindly overwriting it, so
 * enabling the step trigger doesn't silently disable any-motion (or vice
 * versa).
 */
static int bmi270_update_int1_map_feat(const struct device *dev)
{
	struct bmi270_data *data = dev->data;
	const struct bmi270_config *cfg = dev->config;
	uint8_t int1_map_feat = 0;
	int ret;

	if (data->motion_handler != NULL) {
		int1_map_feat |= BMI270_INT_MAP_ANY_MOTION;
	}

	if (data->step_handler != NULL) {
		int1_map_feat |= cfg->feature->step_cnt_int_bit;
	}

	/*
	 * "context" has no interrupt mapping for BMI2_ACTIVITY_RECOGNITION
	 * (Bosch's bmi270_context_map_feat_int() only supports step
	 * counter/detector) - setting this bit there would be meaningless at
	 * best, or alias onto some unrelated context feature at worst. Only
	 * base/max_fifo's step-activity guess uses a real interrupt bit.
	 */
	if ((data->activity_handler != NULL) && (cfg->feature->act_recog_en == NULL)) {
		int1_map_feat |= BMI270_INT_MAP_ACTIVITY;
	}

	ret = bmi270_reg_write(dev, BMI270_REG_INT1_MAP_FEAT, &int1_map_feat, 1);
	if (ret < 0) {
		LOG_ERR("failed configuring INT1_MAP_FEAT (%d)", ret);
		return ret;
	}

	return 0;
}

static int bmi270_anymo_config(const struct device *dev, bool enable)
{
	const struct bmi270_config *cfg = dev->config;
	struct bmi270_data *data = dev->data;
	uint16_t anymo_2;
	int ret;

	if (enable) {
		ret = bmi270_feature_reg_write(dev, cfg->feature->anymo_1,
					       data->anymo_1);
		if (ret < 0) {
			return ret;
		}
	}

	anymo_2 = data->anymo_2;
	if (enable) {
		anymo_2 |= BMI270_ANYMO_2_ENABLE;
	}

	ret = bmi270_feature_reg_write(dev, cfg->feature->anymo_2, anymo_2);
	if (ret < 0) {
		return ret;
	}

	return bmi270_update_int1_map_feat(dev);
}

static int bmi270_step_cnt_config(const struct device *dev, bool enable)
{
	const struct bmi270_config *cfg = dev->config;
	struct bmi270_data *data = dev->data;
	/*
	 * Step counter and step detector share the same INT_STATUS_0/INT_MAP
	 * bit, and this same 16-bit word also holds the step-activity enable
	 * bit (BMI270_STEP_CNT_FEAT_EN_STEP_ACT) - a *different* feature we
	 * are not touching here. Read-modify-write so we don't clobber it
	 * (or step-activity's own bits, if the config-file blob or another
	 * code path ever sets them) when flipping our two bits.
	 */
	uint16_t step_word;
	int ret;

	if ((cfg->feature->step_cnt_en == NULL) || (cfg->feature->step_cnt_params == NULL)) {
		LOG_ERR("step counter feature registers not defined for this variant");
		return -ENOTSUP;
	}

	ret = bmi270_feature_reg_read(dev, cfg->feature->step_cnt_en, &step_word);
	if (ret < 0) {
		return ret;
	}

	step_word &= ~(BMI270_STEP_CNT_FEAT_EN_STEP_COUNT | BMI270_STEP_CNT_FEAT_EN_STEP_DET);
	if (enable) {
		/*
		 * Enabling the detector alongside the counter makes the
		 * interrupt fire on every single step instead of waiting for
		 * the counter's 20-step watermark - much faster to confirm
		 * the interrupt path works at all, at the cost of more
		 * frequent wakeups.
		 */
		step_word |= BMI270_STEP_CNT_FEAT_EN_STEP_COUNT | BMI270_STEP_CNT_FEAT_EN_STEP_DET;
	}

	ret = bmi270_feature_reg_write(dev, cfg->feature->step_cnt_en, step_word);
	if (ret < 0) {
		return ret;
	}

	if (enable) {
		/*
		 * Watermark is in units of 20 steps; see bmi270.h for
		 * details. Read-modify-write: only bits 0-10 (watermark +
		 * reset-count) are ours; preserve whatever else the
		 * config-file blob put in the rest of this word.
		 */
		uint16_t wm_word;

		ret = bmi270_feature_reg_read(dev, cfg->feature->step_cnt_params, &wm_word);
		if (ret < 0) {
			return ret;
		}

		wm_word &= ~(BMI270_STEP_CNT_WM_LEVEL_MASK | BMI270_STEP_CNT_RST_CNT);
		wm_word |= FIELD_PREP(BMI270_STEP_CNT_WM_LEVEL_MASK, data->step_wm_level);

		ret = bmi270_feature_reg_write(dev, cfg->feature->step_cnt_params, wm_word);
		if (ret < 0) {
			return ret;
		}
	}

	return bmi270_update_int1_map_feat(dev);
}

/*
 * base/max_fifo only: the unverified step-activity register guess (see
 * BMI270_STEP_ACTIVITY_MASK in bmi270.h). Shares the step counter/detector
 * enable word (page 6, reg 0x32) - read-modify-write so we only ever touch
 * our own bit.
 */
static int bmi270_activity_config_legacy(const struct device *dev, bool enable)
{
	const struct bmi270_config *cfg = dev->config;
	uint16_t step_word;
	int ret;

	if (cfg->feature->step_cnt_en == NULL) {
		LOG_ERR("step-activity feature register not defined for this variant");
		return -ENOTSUP;
	}

	ret = bmi270_feature_reg_read(dev, cfg->feature->step_cnt_en, &step_word);
	if (ret < 0) {
		return ret;
	}

	step_word &= ~BMI270_STEP_CNT_FEAT_EN_STEP_ACT;
	if (enable) {
		step_word |= BMI270_STEP_CNT_FEAT_EN_STEP_ACT;
	}

	ret = bmi270_feature_reg_write(dev, cfg->feature->step_cnt_en, step_word);
	if (ret < 0) {
		return ret;
	}

	return bmi270_update_int1_map_feat(dev);
}

/*
 * FIFO_CONFIG (registers 0x48/0x49) is a plain top-level register, not a
 * feature-page one - no FEAT_PAGE select needed, unlike
 * bmi270_feature_reg_write(). Only header mode is toggled here; we
 * deliberately leave ACC_EN/GYR_EN off so the FIFO only ever fills with the
 * activity-recognition virtual frames we actually want.
 */
static int bmi270_fifo_enable_headers(const struct device *dev, bool enable)
{
	uint8_t buf[2] = { 0 };

	if (enable) {
		buf[1] = BMI270_FIFO_CONFIG_1_HEADER_EN;
	}

	return bmi270_reg_write(dev, BMI270_REG_FIFO_CONFIG_0, buf, sizeof(buf));
}

/*
 * Drains whatever is currently in the FIFO looking for
 * BMI270_FIFO_HEADER_ACT_RECOG_FRM frames (1 header byte + 6-byte payload:
 * 4-byte LE timestamp, prev_act, curr_act - per bmi270_context.c's
 * unpack_act_recog_output()). Caches the latest decoded frame into *data and
 * invokes activity_handler once per frame found. Stops at the first
 * "empty"/unrecognized header rather than risk misparsing a partial frame.
 */
static int bmi270_fifo_read_act_recog(const struct device *dev)
{
	struct bmi270_data *data = dev->data;
	uint8_t len_buf[2];
	uint8_t fifo_buf[32];
	uint16_t fifo_len;
	uint16_t i = 0;
	int ret;

	ret = bmi270_reg_read(dev, BMI270_REG_FIFO_LENGTH_0, len_buf, sizeof(len_buf));
	if (ret < 0) {
		return ret;
	}

	fifo_len = sys_get_le16(len_buf) & GENMASK(13, 0);
	if (fifo_len == 0) {
		return 0;
	}

	if (fifo_len > sizeof(fifo_buf)) {
		fifo_len = sizeof(fifo_buf);
	}

	ret = bmi270_reg_read(dev, BMI270_REG_FIFO_DATA, fifo_buf, fifo_len);
	if (ret < 0) {
		return ret;
	}

	while (i < fifo_len) {
		uint8_t header = fifo_buf[i];

		if (header == BMI270_FIFO_HEADER_EMPTY_FRM) {
			break;
		}

		if (header != BMI270_FIFO_HEADER_ACT_RECOG_FRM) {
			/* Unexpected frame type - bail rather than misparse. */
			break;
		}

		if ((i + BMI270_FIFO_ACT_RECOG_FRM_LEN) > fifo_len) {
			/* Partial frame; the rest will show up next poll. */
			break;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);
		data->activity_timestamp = sys_get_le32(&fifo_buf[i + 1]);
		data->activity_prev = (enum bmi270_activity_recog)fifo_buf[i + 5];
		data->activity_curr = (enum bmi270_activity_recog)fifo_buf[i + 6];
		data->activity_data_valid = true;
		k_mutex_unlock(&data->trigger_mutex);

		if (data->activity_handler != NULL) {
			data->activity_handler(dev, data->activity_trigger);
		}

		i += BMI270_FIFO_ACT_RECOG_FRM_LEN;
	}

	return 0;
}

static void bmi270_activity_poll_work_cb(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bmi270_data *data = CONTAINER_OF(dwork, struct bmi270_data,
						activity_poll_work);
	int ret;

	ret = bmi270_fifo_read_act_recog(data->dev);
	if (ret < 0) {
		LOG_ERR("activity recognition FIFO read failed (%d)", ret);
	}

	k_work_reschedule(dwork, BMI270_ACTIVITY_POLL_INTERVAL);
}

/*
 * "context" only: the real BMI2_ACTIVITY_RECOGNITION feature. No interrupt
 * exists for it, so enabling it means (a) flipping its feature-enable bit,
 * (b) turning on FIFO header mode so its virtual frames are identifiable,
 * and (c) starting a periodic poll of the FIFO in the background.
 */
static int bmi270_activity_config_context(const struct device *dev, bool enable)
{
	const struct bmi270_config *cfg = dev->config;
	struct bmi270_data *data = dev->data;
	uint16_t en_word;
	int ret;

	ret = bmi270_feature_reg_read(dev, cfg->feature->act_recog_en, &en_word);
	if (ret < 0) {
		return ret;
	}

	en_word &= ~BMI270_CONTEXT_ACT_RECOG_EN_MASK;
	if (enable) {
		en_word |= BMI270_CONTEXT_ACT_RECOG_EN_MASK;
	}

	ret = bmi270_feature_reg_write(dev, cfg->feature->act_recog_en, en_word);
	if (ret < 0) {
		return ret;
	}

	ret = bmi270_fifo_enable_headers(dev, enable);
	if (ret < 0) {
		return ret;
	}

	if (enable) {
		data->activity_data_valid = false;
		k_work_init_delayable(&data->activity_poll_work, bmi270_activity_poll_work_cb);
		k_work_reschedule(&data->activity_poll_work, BMI270_ACTIVITY_POLL_INTERVAL);
	} else {
		k_work_cancel_delayable(&data->activity_poll_work);
	}

	return 0;
}

static int bmi270_activity_config(const struct device *dev, bool enable)
{
	const struct bmi270_config *cfg = dev->config;

	if (cfg->feature->act_recog_en != NULL) {
		return bmi270_activity_config_context(dev, enable);
	}

	return bmi270_activity_config_legacy(dev, enable);
}

int bmi270_activity_recognition_get(const struct device *dev,
				    enum bmi270_activity_recog *curr,
				    enum bmi270_activity_recog *prev,
				    uint32_t *timestamp)
{
	struct bmi270_data *data = dev->data;

	if (!data->activity_data_valid) {
		return -EAGAIN;
	}

	k_mutex_lock(&data->trigger_mutex, K_FOREVER);
	if (curr != NULL) {
		*curr = data->activity_curr;
	}
	if (prev != NULL) {
		*prev = data->activity_prev;
	}
	if (timestamp != NULL) {
		*timestamp = data->activity_timestamp;
	}
	k_mutex_unlock(&data->trigger_mutex);

	return 0;
}

static int bmi270_drdy_config(const struct device *dev, bool enable)
{
	int ret;

	uint8_t int_map_data = 0;

	if (enable) {
		int_map_data |= BMI270_INT_MAP_DATA_DRDY_INT2;
	}

	ret = bmi270_reg_write(dev, BMI270_REG_INT_MAP_DATA, &int_map_data, 1);
	if (ret < 0) {
		LOG_ERR("failed configuring INT_MAP_DATA (%d)", ret);
		return ret;
	}

	return 0;
}

int bmi270_trigger_set(const struct device *dev,
		       const struct sensor_trigger *trig,
		       sensor_trigger_handler_t handler)
{
	struct bmi270_data *data = dev->data;
	const struct bmi270_config *cfg = dev->config;

	switch ((uint32_t)trig->type) {
	case SENSOR_TRIG_MOTION:
		if (!cfg->int1.port) {
			return -ENOTSUP;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);
		data->motion_handler = handler;
		data->motion_trigger = trig;
		k_mutex_unlock(&data->trigger_mutex);
		return bmi270_anymo_config(dev, handler != NULL);

	case SENSOR_TRIG_DATA_READY:
		if (!cfg->int2.port) {
			return -ENOTSUP;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);
		data->drdy_handler = handler;
		data->drdy_trigger = trig;
		k_mutex_unlock(&data->trigger_mutex);
		return bmi270_drdy_config(dev, handler != NULL);

	case BMI270_SENSOR_TRIG_STEP:
		if (!cfg->int1.port) {
			return -ENOTSUP;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);
		data->step_handler = handler;
		data->step_trigger = trig;
		k_mutex_unlock(&data->trigger_mutex);
		return bmi270_step_cnt_config(dev, handler != NULL);

	case BMI270_SENSOR_TRIG_ACTIVITY:
		if (!cfg->int1.port) {
			return -ENOTSUP;
		}

		k_mutex_lock(&data->trigger_mutex, K_FOREVER);
		data->activity_handler = handler;
		data->activity_trigger = trig;
		k_mutex_unlock(&data->trigger_mutex);
		return bmi270_activity_config(dev, handler != NULL);
	default:
		return -ENOTSUP;
	}

	return 0;
}
