// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "data.h"
#include "config.h"
#include "context.h"
#include "control.h"

struct iptsd_context iptsd;

static int iptsd_exit(struct iptsd_context *iptsd, int error)
{
	if (iptsd->data)
		free(iptsd->data);

	iptsd_devices_destroy(&iptsd->devices);

	int ret = iptsd_control_stop(&iptsd->control);
	if (ret < 0)
		return ret;

	return error;
}

static void iptsd_signal(int sig)
{
	iptsd_exit(&iptsd, sig);

	exit(EXIT_FAILURE);
}

static int iptsd_loop(struct iptsd_context *iptsd)
{
	uint32_t doorbell = iptsd_control_doorbell(&iptsd->control);
	if (doorbell < 0)
		return doorbell;

	int diff = doorbell - iptsd->control.current_doorbell;
	int size = iptsd->control.device_info.buffer_size;

	while (doorbell > iptsd->control.current_doorbell) {
		int ret = iptsd_control_read(&iptsd->control,
				iptsd->data, size);
		if (ret < 0)
			return ret;

		ret = iptsd_data_handle_input(iptsd);
		if (ret < 0)
			return ret;

		ret = iptsd_control_send_feedback(&iptsd->control);
		if (ret < 0)
			return ret;

		iptsd->control.current_doorbell++;
	}

	return diff;
}

int main(void)
{
	memset(&iptsd, 0, sizeof(struct iptsd_context));
	signal(SIGINT, iptsd_signal);
	signal(SIGTERM, iptsd_signal);

	int ret = iptsd_control_start(&iptsd.control);
	if (ret < 0)
		return ret;

	time_t timeout = time(NULL) + 5;
	struct ipts_device_info device_info = iptsd.control.device_info;

	printf("Connected to device %04X:%04X\n",
			device_info.vendor, device_info.product);

	iptsd_config_load(&iptsd.config, device_info);

	iptsd.data = calloc(device_info.buffer_size, sizeof(char));
	if (!iptsd.data)
		return iptsd_exit(&iptsd, -ENOMEM);

	iptsd.devices.config.width = iptsd.config.width;
	iptsd.devices.config.height = iptsd.config.height;
	iptsd.devices.config.invert_x = iptsd.config.invert_x;
	iptsd.devices.config.invert_y = iptsd.config.invert_y;

	iptsd.devices.config.vendor = device_info.vendor;
	iptsd.devices.config.product = device_info.product;
	iptsd.devices.config.version = device_info.version;
	iptsd.devices.config.max_contacts = device_info.max_contacts;

	ret = iptsd_devices_create(&iptsd.devices);
	if (ret < 0)
		return iptsd_exit(&iptsd, ret);

	while (1) {
		ret = iptsd_loop(&iptsd);

		if (ret < 0)
			return iptsd_exit(&iptsd, ret);

		if (ret > 0)
			timeout = time(NULL) + 5;

		if (timeout > time(NULL))
			usleep(10 * 1000);
		else
			usleep(200 * 1000);
	}

	return iptsd_exit(&iptsd, 0);
}

