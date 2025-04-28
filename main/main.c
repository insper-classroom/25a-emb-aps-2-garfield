#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#define UART_ID        uart0
#define BAUD_RATE      115200
 
QueueHandle_t xQueueAdc;
const int PWM_X_PIN = 27;
const int PWM_Y_PIN = 26;
typedef struct adc {
    int axis;
    int val;
} adc_t;
 
int abs(int val){
    if(val>0){
        return val;
    }
    return -1*val;
}
void x_task(void *p) {  
    adc_gpio_init(PWM_X_PIN);
    uint16_t buff[5] = {0, 0, 0, 0, 0};
    uint16_t div;
    adc_t adc_data;
    int i = 0;
    while (1) {
        adc_select_input(1); 
        uint16_t result = adc_read();

        buff[i] = result;
        i = (i + 1) % 5;
        uint16_t sum = 0;
        for(int j = 0; j<5; j++){
            sum += buff[j];
        }
        div = sum/5;
        if(abs(((div - 2047) * 255) / 2047) >= 30){
            adc_data.axis = 0; 
            adc_data.val  = ((div - 2047) * 255) / 2047; 
            xQueueSend(xQueueAdc, &adc_data, 0);
        }            
        vTaskDelay(pdMS_TO_TICKS(50));

    }
}
void y_task(void *p) {
    adc_gpio_init(PWM_Y_PIN);
    uint16_t buff[5] = {0, 0, 0, 0, 0};
    uint16_t div;
    adc_t adc_data;
    int i = 0;
    while (1) {
        adc_select_input(0); 
        uint16_t result = adc_read();

        buff[i] = result;
        i = (i + 1) % 5;
        uint16_t sum = 0;
        for(int j = 0; j<5; j++){
            sum += buff[j];
        }
        div = sum/5;
        if(abs(((div - 2047) * 255) / 2047) >= 30){
            adc_data.axis = 1; 
            adc_data.val  = ((div - 2047) * 255) / 2047; 
            xQueueSend(xQueueAdc, &adc_data, 0);
        }            
        vTaskDelay(pdMS_TO_TICKS(50));

    }
}

void uart_task(void *p) {
    adc_t adc_data;
    while (1) {
        if (
        xQueueReceive(xQueueAdc, &adc_data, portMAX_DELAY) == pdTRUE) {
            uint16_t val = (uint16_t)(adc_data.val & 0xFFFF);
            uint8_t val_1 = (val >> 8) & 0xFF;  
            uint8_t val_0 = val & 0xFF; 
            uart_putc_raw(UART_ID,  (uint8_t) adc_data.axis);
            uart_putc_raw(UART_ID, val_1);
            uart_putc_raw(UART_ID, val_0);
            uart_putc_raw(UART_ID, 0xFF);
    }
    }
}
int main() {
    stdio_init_all();
    adc_init();
    uart_init(UART_ID, BAUD_RATE);
    xTaskCreate(x_task, "x_ task", 4095, NULL, 1, NULL);
    xTaskCreate(y_task, "y_ task", 4095, NULL, 1, NULL);
    xTaskCreate(uart_task, "uart_ task", 4095, NULL, 1, NULL);
    xQueueAdc = xQueueCreate(32, sizeof(adc_t));

    // Inicia scheduler
    vTaskStartScheduler();
    while (true)
        ;
    return 0;
}
