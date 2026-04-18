/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef _APP_SNTP_H_
#define _APP_SNTP_H_


#ifdef __cplusplus
extern "C" {
#endif

void app_sntp_init(void);
void app_sntp_set_timezone(const char *tz);

#ifdef __cplusplus
}
#endif

#endif
