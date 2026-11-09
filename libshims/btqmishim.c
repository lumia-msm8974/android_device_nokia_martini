/*
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Preload shim for hci_qcomm_init on the MSM8974 Lumias (from the Lumia
 * 1520 port).
 *
 * hci_qcomm_init asks the modem for the Bluetooth address through QMI DMS
 * before it downloads the Pronto Bluetooth NVM. Without a running modem
 * QMI client setup waits about 60 s before failing; the Bluetooth stack
 * gives up much earlier, closes the HAL and the AOSP 1.0 HAL aborts. The
 * address comes from /persist/bdaddr.txt through "-b" anyway, so make QMI
 * setup fail at once for this binary only.
 */
#define QMI_INTERNAL_ERR (-1)

int qmi_init(void *rx_cb, void *user_data)
{
	(void)rx_cb; (void)user_data;
	return -1;
}

int qmi_client_init(void *service_info, void *service_obj, void *ind_cb,
		    void *ind_cb_data, void *os_params, void **user_handle)
{
	(void)service_info; (void)service_obj; (void)ind_cb;
	(void)ind_cb_data; (void)os_params;
	if (user_handle)
		*user_handle = 0;
	return QMI_INTERNAL_ERR;
}
