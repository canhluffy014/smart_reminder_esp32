	/*
 * ldr_gl5537.h
 *
 *  Created on: 19 thg 7, 2025
 *      Author: Lenovo
 */

#ifndef MAIN_LDR_GL5537_H_
#define MAIN_LDR_GL5537_H_

#ifndef _LDR_GL5537_H_
#define _LDR_GL5537_H_

#include <stdint.h>
#include "driver/adc.h"
#include "esp_err.h"

#define LDR_REFERENCE_VOLTAGE 3.3f 
#define LDR_PULL_DOWN_RESISTOR 10000 
#define LDR_MAX_RESISTANCE 2000000 
#define LDR_MIN_RESISTANCE 20000 
#define LDR_LUX_CONSTANT_1 1000000 
#define LDR_LUX_CONSTANT_2 1.2 

typedef struct {
    adc1_channel_t adc_channel; 
    float reference_voltage;
    uint32_t pull_down_resistor; 
} LDR_GL5537_t;


esp_err_t ldr_gl5537_init(LDR_GL5537_t *ldr, adc1_channel_t channel);


esp_err_t ldr_gl5537_read_voltage(LDR_GL5537_t *ldr, float *voltage);


float ldr_gl5537_voltage_to_lux(float voltage, float reference_voltage, uint32_t pull_down_resistor);

#endif /* _LDR_GL5537_H_ */


#endif /* MAIN_LDR_GL5537_H_ */
