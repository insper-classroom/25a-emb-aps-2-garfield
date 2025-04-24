/*
 * main.c
 * —————————————————————————
 * • Leitura multiplexada (CD4051) de 4 canais analógicos:
 *     ch0 = mouse X
 *     ch1 = mouse Y
 *     ch2 = mov X (“A/D”)
 *     ch3 = mov Y (“W/S”)
 * • Leitura de 6 botões digitais:
 *     btn0 → Q, btn1 → E, btn2 → R,
 *     btn3 → SPACE, btn4 → F, btn5 → M
 * • Filtragem (média móvel), escala e “dead-zone” nos ADCs.
 * • Evento via fila FreeRTOS (axis 0–9, val int16).
 * • UART TX: [axis][LSB][MSB][0xFF].
 */

 #include <stdio.h>
 #include <stdbool.h>
 #include "pico/stdlib.h"
 #include "hardware/adc.h"
 #include "hardware/gpio.h"
 #include "FreeRTOS.h"
 #include "task.h"
 #include "queue.h"
 
 // ─── CD4051 ───────────────────────────────────────────
 #define SEL_A_4051 13
 #define SEL_B_4051 12
 #define SEL_C_4051 11
 #define INH_4051   10
 
 // ─── ADC ──────────────────────────────────────────────
 #define NUM_CHANNELS 4
 #define BUFFER_SIZE   5
 
 // ─── Botões ───────────────────────────────────────────
 #define BTN_COUNT  6
 // GPIOs usados para os botões, com pull-up interno
 static const uint BTN_PIN[BTN_COUNT] = {2, 3, 4, 5, 6, 7};
 // axis enviados: 4→btn0(Q),5→btn1(E),6→btn2(R),7→btn3(SPACE),8→btn4(F),9→btn5(M)
 
 // ─── Estrutura e fila ─────────────────────────────────
 typedef struct {
     uint8_t axis;   // 0..9
     int16_t val;    // joystick: –255..+255 / botão: 0 ou 1
 } adc_t;
 
 static QueueHandle_t xQueueADC;
 
 // ─── Buffers para média móvel ────────────────────────
 static int buffers[NUM_CHANNELS][BUFFER_SIZE];
 static int buf_index[NUM_CHANNELS];
 
 // ─── Estado anterior dos botões ──────────────────────
 static bool btn_prev[BTN_COUNT];
 
 // ─── Funções auxiliares CD4051 ───────────────────────
 void polling_adc_init(void) {
     gpio_init(SEL_A_4051); gpio_set_dir(SEL_A_4051, GPIO_OUT);
     gpio_init(SEL_B_4051); gpio_set_dir(SEL_B_4051, GPIO_OUT);
     gpio_init(SEL_C_4051); gpio_set_dir(SEL_C_4051, GPIO_OUT);
     gpio_init(INH_4051);   gpio_set_dir(INH_4051, GPIO_OUT);
     // começa desabilitado
     gpio_put(INH_4051, 1);
 }
 
 void select_4051_channel(uint channel) {
     gpio_put(SEL_A_4051, (channel >> 0) & 1);
     gpio_put(SEL_B_4051, (channel >> 1) & 1);
     gpio_put(SEL_C_4051, (channel >> 2) & 1);
 }
 
 // ─── Média móvel simples ─────────────────────────────
 int media_movel(int new_value, int *buffer, int *index) {
     int sum = 0;
     for (int i = 0; i < BUFFER_SIZE; i++)
         sum += buffer[i];
     sum -= buffer[*index];
     buffer[*index] = new_value;
     sum += new_value;
     int out = sum / BUFFER_SIZE;
     *index = (*index + 1) % BUFFER_SIZE;
     return out;
 }
 
 // ─── Task: polling ADC via CD4051 ────────────────────
 void polling_task(void *pv) {
     adc_t item;
     while (1) {
         for (uint ch = 0; ch < NUM_CHANNELS; ch++) {
             // desabilita, escolhe canal, habilita
             gpio_put(INH_4051, 1);
             select_4051_channel(ch);
             vTaskDelay(pdMS_TO_TICKS(2));
             gpio_put(INH_4051, 0);
             vTaskDelay(pdMS_TO_TICKS(2));
 
             // leitura, filtra, escala e dead-zone
             uint16_t raw = adc_read();
             int filt = media_movel(raw, buffers[ch], &buf_index[ch]);
             int scaled = (filt - 2047) / 8;
             if (scaled > -30 && scaled < 30) scaled = 0;
 
             // envia só se houver movimento
             if (scaled != 0) {
                 item.axis = ch;        // 0..3
                 item.val  = (int16_t)scaled;
                 xQueueSend(xQueueADC, &item, pdMS_TO_TICKS(10));
             }
 
             gpio_put(INH_4051, 1);
         }
         vTaskDelay(pdMS_TO_TICKS(50));
     }
 }
 
 // ─── Task: leitura de botões ─────────────────────────
 void button_task(void *pv) {
     adc_t item;
     for (int i = 0; i < BTN_COUNT; i++) {
         btn_prev[i] = false;  // assume “não pressionado”
     }
 
     while (1) {
         for (int i = 0; i < BTN_COUNT; i++) {
             bool pressed = (gpio_get(BTN_PIN[i]) == 0);
             if (pressed != btn_prev[i]) {
                 // mudou de estado → envia evento
                 item.axis = 4 + i;         // 4..9
                 item.val  = pressed ? 1 : 0;
                 xQueueSend(xQueueADC, &item, pdMS_TO_TICKS(10));
                 btn_prev[i] = pressed;
             }
         }
         vTaskDelay(pdMS_TO_TICKS(50));
     }
 }
 
 // ─── Task: UART TX ───────────────────────────────────
 void uart_task(void *pv) {
     adc_t recv;
     while (1) {
         if (xQueueReceive(xQueueADC, &recv, portMAX_DELAY)) {
             putchar_raw(recv.axis & 0xFF);
             putchar_raw(recv.val   & 0xFF);
             putchar_raw((recv.val >> 8) & 0xFF);
             putchar_raw(0xFF);
         }
     }
 }
 
 // ─── main() ──────────────────────────────────────────
 int main() {
     stdio_init_all();
 
     // ADC: COM do 4051 em GPIO27 (ADC1)
     adc_init();
     adc_gpio_init(27);
     adc_select_input(1);
 
     // CD4051 e buffers
     polling_adc_init();
     for (int ch = 0; ch < NUM_CHANNELS; ch++) {
         buf_index[ch] = 0;
         for (int j = 0; j < BUFFER_SIZE; j++)
             buffers[ch][j] = 2047;
     }
 
     // Botões: pull-up interno
     for (int i = 0; i < BTN_COUNT; i++) {
         gpio_init(BTN_PIN[i]);
         gpio_set_dir(BTN_PIN[i], GPIO_IN);
         gpio_pull_up(BTN_PIN[i]);
     }
 
     // Fila FreeRTOS
     xQueueADC = xQueueCreate(20, sizeof(adc_t));
 
     // Cria tasks
     xTaskCreate(polling_task, "PollADC",   4096, NULL, 1, NULL);
     xTaskCreate(button_task,  "Buttons",   2048, NULL, 1, NULL);
     xTaskCreate(uart_task,    "UARTTx",    4096, NULL, 1, NULL);
 
     // Inicia scheduler
     vTaskStartScheduler();
     while (1) { tight_loop_contents(); }
     return 0;
 }
 