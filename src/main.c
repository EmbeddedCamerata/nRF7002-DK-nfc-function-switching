/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>

#include <nrfx.h>
#include <hal/nrf_power.h>
#if !NRF_POWER_HAS_RESETREAS
#include <hal/nrf_reset.h>
#endif

#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <nfc/ndef/launchapp_msg.h>

#include <dk_buttons_and_leds.h>

#define MAX_REC_COUNT 1
#define NDEF_MSG_BUF_SIZE 256

#define NFC_FIELD_LED DK_LED1
#define SYSTEM_ON_LED DK_LED2

/* Text message in its language code. */
static const uint8_t en_payload[] = {
	'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
static const uint8_t en_code[] = {'e', 'n'};

/* Package: com.hypergryph.arknights */
static const uint8_t android_pkg_name[] = {
	'c', 'o', 'm', '.',
	'h', 'y', 'p', 'e', 'r', 'g', 'r', 'y', 'p', 'h', '.',
	'a', 'r', 'k', 'n', 'i', 'g', 'h', 't', 's'};

/* Buffer used to hold an NFC NDEF message. */
static uint8_t text_msg_buf[NDEF_MSG_BUF_SIZE];
static uint8_t launch_app_msg_buf[NDEF_MSG_BUF_SIZE];

/**
 * @brief Function that receives events from NFC.
 */
static void nfc_callback(void *context,
						 nfc_t2t_event_t event,
						 const uint8_t *data,
						 size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	switch (event)
	{
	case NFC_T2T_EVENT_FIELD_ON:
		dk_set_led_on(NFC_FIELD_LED);
		break;
	case NFC_T2T_EVENT_FIELD_OFF:
		dk_set_led_off(NFC_FIELD_LED);
		break;
	default:
		break;
	}
}

typedef enum
{
	NFC_APP_TEXT = 0U,
	NFC_APP_LAUNCHAPP
} nfc_app_t;

static int nfc_text_encode(uint8_t *buffer, uint32_t *len)
{
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_text_rec,
								  UTF_8,
								  en_code,
								  sizeof(en_code),
								  en_payload,
								  sizeof(en_payload));

	NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_COUNT);

	/* Add record */
	if (nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
								&NFC_NDEF_TEXT_RECORD_DESC(nfc_text_rec)) < 0)
	{
		printk("Cannot add record!\n");
		return -1;
	}

	/* Encode welcome message */
	if (nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), buffer, len) < 0)
	{
		printk("Cannot encode message!\n");
		return -1;
	}

	return 0;
}

static int nfc_launchapp_encode(uint8_t *buffer, uint32_t *len)
{
	/* Encode launch app data  */
	if (nfc_launchapp_msg_encode(android_pkg_name,
								 sizeof(android_pkg_name), NULL, 0, buffer, len) < 0)
	{
		printk("Cannot encode message!\n");
		return -1;
	}

	return 0;
}

/**
 * @brief Function for switching the NFC payload.
 */
static int nfc_payload_set(nfc_app_t nfc_app)
{
	uint32_t len;

	/* Set created message as the NFC payload */
	if (nfc_app == NFC_APP_TEXT)
	{
		len = sizeof(text_msg_buf);
		if (nfc_text_encode(text_msg_buf, &len) < 0)
		{
			printk("Cannot encode text message!\n");
			return -1;
		}
		/* Set created message as the NFC payload */
		if (nfc_t2t_payload_set(text_msg_buf, len) < 0)
		{
			printk("Cannot set payload!\n");
			return -1;
		}
	}
	else
	{
		len = sizeof(launch_app_msg_buf);
		if (nfc_launchapp_encode(launch_app_msg_buf, &len) < 0)
		{
			printk("Cannot encode launchapp message!\n");
			return -1;
		}
		/* Set created message as the NFC payload */
		if (nfc_t2t_payload_set(launch_app_msg_buf, len) < 0)
		{
			printk("Cannot set payload!\n");
			return -1;
		}
	}

	/* Start sensing NFC field */
	if (nfc_t2t_emulation_start() < 0)
	{
		printk("Cannot start emulation!\n");
		return -1;
	}

	printk("NFC configuration done\n");
	return 0;
}

/**
 * @brief Function for configuring and starting the NFC.
 */
static int start_nfc(nfc_app_t nfc_app)
{
	/* Set up NFC */
	if (nfc_t2t_setup(nfc_callback, NULL) < 0)
	{
		printk("Cannot setup NFC T2T library!\n");
		return -1;
	}

	return nfc_payload_set(nfc_app);
}

/**
 * @brief  Helper function for printing the reason of the last reset.
 * Can be used to confirm that NCF field actually woke up the system.
 */
static void print_reset_reason(void)
{
	uint32_t reas;

#if NRF_POWER_HAS_RESETREAS

	reas = nrf_power_resetreas_get(NRF_POWER);
	nrf_power_resetreas_clear(NRF_POWER, reas);
	if (reas & NRF_POWER_RESETREAS_NFC_MASK)
	{
		printk("Wake up by NFC field detect\n");
	}
	else if (reas & NRF_POWER_RESETREAS_RESETPIN_MASK)
	{
		printk("Reset by pin-reset\n");
	}
	else if (reas & NRF_POWER_RESETREAS_SREQ_MASK)
	{
		printk("Reset by soft-reset\n");
	}
	else if (reas)
	{
		printk("Reset by a different source (0x%08X)\n", reas);
	}
	else
	{
		printk("Power-on-reset\n");
	}

#else

	reas = nrf_reset_resetreas_get(NRF_RESET);
	nrf_reset_resetreas_clear(NRF_RESET, reas);
	if (reas & NRF_RESET_RESETREAS_NFC_MASK)
	{
		printk("Wake up by NFC field detect\n");
	}
	else if (reas & NRF_RESET_RESETREAS_RESETPIN_MASK)
	{
		printk("Reset by pin-reset\n");
	}
	else if (reas & NRF_RESET_RESETREAS_SREQ_MASK)
	{
		printk("Reset by soft-reset\n");
	}
	else if (reas)
	{
		printk("Reset by a different source (0x%08X)\n", reas);
	}
	else
	{
		printk("Power-on-reset\n");
	}

#endif
}

static int board_init(void)
{
	int err;

	err = dk_buttons_init(NULL);
	if (err)
	{
		printk("Cannot init buttons (err: %d)\n", err);
		return err;
	}

	err = dk_leds_init();
	if (err)
	{
		printk("Cannot init LEDs (err: %d)\n", err);
	}
	dk_set_led_on(SYSTEM_ON_LED);

	return err;
}

int main(void)
{
	uint32_t button_state = 0;
	nfc_app_t nfc_app = NFC_APP_LAUNCHAPP;

	/* Configure LED-pins as outputs. */
	if (board_init() < 0)
	{
		printk("Cannot initialize board!\n");
		goto fail;
	}

	/* Show last reset reason */
	print_reset_reason();

	/* Start NFC */
	if (start_nfc(nfc_app) < 0)
	{
		printk("Cannot start NFC!\n");
		goto fail;
	}

	while (true)
	{
		dk_read_buttons(&button_state, NULL);

		if (button_state & DK_BTN1_MSK)
		{
			nfc_app = 1 - nfc_app;
			printk("Switching to %s\n", nfc_app == NFC_APP_TEXT ? "text" : "app");

			/* Stop sensing NFC field */
			if (nfc_t2t_emulation_stop() < 0)
			{
				printk("Cannot stop emulation!\n");
				return -1;
			}

			if (nfc_payload_set(nfc_app) < 0)
			{
				printk("NFC payload set failed!\n");
				goto fail;
			}
		}

		k_sleep(K_MSEC(200));
	}

fail:
#if CONFIG_REBOOT
	sys_reboot(SYS_REBOOT_COLD);
#endif /* CONFIG_REBOOT */
	return -EIO;
}
