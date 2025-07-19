/*
 * rgb_led.h
 *
 *  Created on: 16 thg 7, 2025
 *      Author: Lenovo
 */

#ifndef MAIN_RGB_LED_H_
#define MAIN_RGB_LED_H_

#define RGB_LED_GREEN_GPIO			41
#define RGB_LED_BLUE_GPIO			42

#define RGB_LED_CHANNEL_NUM			2

typedef struct{
	int channel;
	int gpio;
	int mode;
	int timer_index;
} ledc_info_t;



void rgb_led_wifi_app_started(void);

void rgb_led_http_server_started(void);

void rgb_led_wifi_connected(void);


#endif /* MAIN_RGB_LED_H_ */
