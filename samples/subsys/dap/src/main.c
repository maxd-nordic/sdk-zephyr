/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/net/buf.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_hid, LOG_LEVEL_INF);

#include <cmsis_dap.h>

#define DAP_PACKET_SIZE		CONFIG_HID_INTERRUPT_EP_MPS

#if (DAP_PACKET_SIZE < 64U)
#error "Minimum packet size is 64"
#endif
#if (DAP_PACKET_SIZE > 32768U)
#error "Maximum packet size is 32768"
#endif

const static struct device *hid0_dev;

NET_BUF_POOL_FIXED_DEFINE(ep_out_pool, CONFIG_CMSIS_DAP_PACKET_COUNT,
			  DAP_PACKET_SIZE, 0, NULL);

K_SEM_DEFINE(hid_epin_sem, 0, 1);
K_FIFO_DEFINE(ep_out_queue);

static const uint8_t hid_report_desc[] = {
	HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2), 0x00, 0xFF,
	HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
	HID_LOGICAL_MIN8(0x00),
	HID_LOGICAL_MAX16(0xFF, 0x00),
	HID_REPORT_SIZE(8),
	HID_REPORT_COUNT(64),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
	HID_INPUT(0x02),
	HID_REPORT_COUNT(64),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
	HID_OUTPUT(0x02),
	HID_REPORT_COUNT(0x01),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
	HID_FEATURE(0x02),
	HID_END_COLLECTION,
};

static void int_in_ready_cb(const struct device *dev)
{
	ARG_UNUSED(dev);

	k_sem_give(&hid_epin_sem);
}

void int_out_ready_cb(const struct device *dev)
{
	struct net_buf *buf;
	size_t len = 0;

	buf = net_buf_alloc(&ep_out_pool, K_FOREVER);
	hid_int_ep_read(dev, buf->data, buf->size, &len);
	net_buf_add(buf, len);

	if (len == 0) {
		LOG_WRN("drop empty packet");
		net_buf_unref(buf);
		return;
	}

	k_fifo_put(&ep_out_queue, buf);
}

static const struct hid_ops ops = {
	.int_in_ready = int_in_ready_cb,
	.int_out_ready = int_out_ready_cb,
};

int main(void)
{
	const struct device *const swd_dev = DEVICE_DT_GET_ONE(zephyr_swdp_gpio);
	uint8_t response_buf[DAP_PACKET_SIZE];
	int ret;

	if (!device_is_ready(swd_dev)) {
		LOG_ERR("SWD device is not ready");
		return -ENODEV;
	}

	ret = dap_setup(swd_dev);
	if (ret) {
		LOG_ERR("Failed to initialize DAP controller (%d)", ret);
		return ret;
	}

	ret = usb_enable(NULL);
	if (ret) {
		LOG_ERR("Failed to enable USB (%d)", ret);
		return ret;
	}

	while (1) {
		struct net_buf *buf;
		size_t len;

		buf = k_fifo_get(&ep_out_queue, K_FOREVER);

		len = dap_execute_cmd(buf->data, response_buf);
		LOG_DBG("response length %u", len);
		net_buf_unref(buf);

		if (hid_int_ep_write(hid0_dev, response_buf, len, NULL)) {
			LOG_ERR("Failed to send a response");
			continue;
		}

		k_sem_take(&hid_epin_sem, K_FOREVER);
	}

	return 0;
}

static int hid_dap_preinit(void)
{
	hid0_dev = device_get_binding("HID_0");
	if (hid0_dev == NULL) {
		LOG_ERR("Cannot get HID_0");
		return -ENODEV;
	}

	usb_hid_register_device(hid0_dev, hid_report_desc,
				sizeof(hid_report_desc), &ops);

	return usb_hid_init(hid0_dev);
}

SYS_INIT(hid_dap_preinit, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
