/*
 * rgb_led.c
 *
 *  Created on: 16 thg 7, 2025
 *      Author: Lenovo
 */

#include <stdbool.h>

#include "driver/ledc.h"
#include "hal/ledc_types.h"
#include "rgb_led.h"


ledc_info_t ledc_ch[RGB_LED_CHANNEL_NUM];

bool g_pwm_init_handle = false;// cờ để hàm rgb_led_pwm_init chỉ khởi tạo 1 lần duy nhất

static void rgb_led_pwm_init(void){// khởi tạo các led
	int rgb_ch;
	
	//GREEN
	ledc_ch[0].channel			= LEDC_CHANNEL_0; //kênh PWM số 0
	ledc_ch[0].gpio				= RGB_LED_GREEN_GPIO;
	ledc_ch[0].mode				= LEDC_LOW_SPEED_MODE; // đếm tốc độ thâp
	ledc_ch[0].timer_index		= LEDC_TIMER_0;			//timer  0
	
	//BLUE												//tuong tu
	ledc_ch[1].channel			= LEDC_CHANNEL_1;
	ledc_ch[1].gpio				= RGB_LED_BLUE_GPIO;
	ledc_ch[1].mode				= LEDC_LOW_SPEED_MODE;
	ledc_ch[1].timer_index		= LEDC_TIMER_0;
	
	//configure timer zero
	ledc_timer_config_t ledc_timer = 					//cấu hình timer cho led pwm
	{
		.duty_resolution	= LEDC_TIMER_8_BIT,			//Độ phân giải 8 bit
		.freq_hz			= 100,						//10 ms
		.speed_mode			= LEDC_LOW_SPEED_MODE,		
		.timer_num			= LEDC_TIMER_0
	};
	ledc_timer_config(&ledc_timer);			// truyen cấu hình cho esp32-s3
	for (rgb_ch = 0; rgb_ch < RGB_LED_CHANNEL_NUM; rgb_ch++) {
		
		ledc_channel_config_t ledc_channel = 			//cấu hình cho từng kênh
		{
			.channel 	= ledc_ch[rgb_ch].channel,		// kênh cần truyền
			.duty	 	= 0,							// do rộng xung =0
			.hpoint  	= 0,								// thời điểm bắt đầu xung
			.gpio_num	= ledc_ch[rgb_ch].gpio,				//chân led
			.intr_type	= LEDC_INTR_DISABLE,				// không ngắt
			.speed_mode	= ledc_ch[rgb_ch].mode,				//chế độ
			.timer_sel	= ledc_ch[rgb_ch].timer_index,		//chan led timer
		};
		ledc_channel_config(&ledc_channel);		//truyền cấu hình
	}
	g_pwm_init_handle = true;
} 

static void rgb_led_set_color(uint8_t green, uint8_t blue)		// Set PWM màu cho led
{
	//Giá trị có độ phân giải 8-bit(0-255)
	ledc_set_duty(ledc_ch[0].mode, ledc_ch[0].channel, green);		//Thiết lập độ rộng xung
	ledc_update_duty(ledc_ch[0].mode, ledc_ch[0].channel);					// update độ rộng xung lên esp32-s3

	ledc_set_duty(ledc_ch[1].mode, ledc_ch[1].channel, blue);
	ledc_update_duty(ledc_ch[1].mode, ledc_ch[1].channel);

	
}

void rgb_led_wifi_app_started(void) //màu led khi khởi tạo wifi
{
	if (g_pwm_init_handle == false)
	{
		rgb_led_pwm_init();
	}

	rgb_led_set_color(255, 0);
}

void rgb_led_http_server_started(void) // màu khi bắt đầu server http
{
	if (g_pwm_init_handle == false)
	{
		rgb_led_pwm_init();
	}

	rgb_led_set_color(0, 255);
}


void rgb_led_wifi_connected(void)		// màu led khi kết nối wifi
{
	if (g_pwm_init_handle == false)
	{
		rgb_led_pwm_init();
	}

	rgb_led_set_color(100, 100);
}

