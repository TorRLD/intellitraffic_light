#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/bitmap.h"
#include <stdio.h>
#include <string.h>
#include "ws2812.pio.h"
#include "FreeRTOS.h"
#include "task.h"

#define NUM_PIXELS 25
#define WS2812_PIN 7
#define IS_RGBW false

bool buffer_leds[NUM_PIXELS] = {false};

#define COR_WS2812_R 20
#define COR_WS2812_G 20
#define COR_WS2812_B 50

#define LED_VERMELHO 13
#define LED_AMARELO 12
#define LED_VERDE 11
#define BUZZER_PIN 10
#define BOTAO_A 5
#define BOTAO_B 6

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDR 0x3C
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

typedef enum {
    ESTADO_VERDE,
    ESTADO_AMARELO,
    ESTADO_VERMELHO
} EstadoSemaforo;

#define TEMPO_VERDE 5000
#define TEMPO_AMARELO 2000
#define TEMPO_VERMELHO 5000
#define TEMPO_PISCA 1000
#define TEMPO_BEEP_VERDE 100
#define TEMPO_BEEP_AMARELO 100
#define TEMPO_BEEP_VERMELHO 500
#define TEMPO_OFF_AMARELO 100
#define TEMPO_OFF_VERMELHO 1500
#define TEMPO_ATUALIZACAO_DISPLAY 500
#define TEMPO_BEEP_NOTURNO 2000
#define DURACAO_BEEP_NOTURNO 100
#define TEMPO_ANIMACAO 300
#define TEMPO_ALTERNANCIA_NOTURNO 500
#define TEMPO_EXIBICAO_SINAL 2000
#define TEMPO_ALTERNANCIA_SINAL 250
#define TEMPO_ANIMACAO_INICIAL 500

volatile bool modo_noturno = false;
volatile uint32_t last_button_time = 0;
#define DEBOUNCE_TIME 300
volatile bool noturno_alternado = false;
volatile uint32_t tempo_ultimo_alternancia_noturno = 0;

volatile EstadoSemaforo estado_semaforo = ESTADO_VERDE;
volatile uint32_t tempo_ultimo_estado = 0;
volatile uint32_t tempo_ultimo_beep = 0;
volatile uint32_t tempo_ultimo_pisca = 0;
volatile uint32_t tempo_ultimo_display = 0;
volatile uint32_t tempo_ultimo_frame = 0;
volatile uint32_t tempo_inicio_sinal = 0;
volatile uint32_t tempo_ultimo_alternancia_sinal = 0;
volatile bool estado_led_amarelo = false;
volatile bool estado_buzzer = false;
volatile bool exibindo_bitmap_sinal = false;
volatile bool sinal_alternado = false;
volatile uint8_t frame_atual = 0;
#define NUM_FRAMES 3

ssd1306_t display;
volatile bool tela_inicial_concluida = false;

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = ((uint32_t)(g) << 16) | ((uint32_t)(r) << 8) | b;
    pio_sm_put_blocking(pio0, 0, color << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

static inline void enviar_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

void definir_leds(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t cor = urgb_u32(r, g, b);
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (buffer_leds[i])
            enviar_pixel(cor);
        else
            enviar_pixel(0);
    }
    sleep_us(60);
}

const bool padrao_semaforo[1][5][5] = {{
    {false, false, false, false, false},
    {false, true, true, true, false},
    {false, true, true, true, false},
    {false, true, true, true, false},
    {false, false, false, false, false}
}};

void atualizar_buffer_com_semaforo(int tipo) {
    for (int linha = 0; linha < 5; linha++) {
        for (int coluna = 0; coluna < 5; coluna++) {
            int indice = linha * 5 + coluna;
            buffer_leds[indice] = padrao_semaforo[tipo][linha][coluna];
        }
    }
}

void atualizar_matriz_rgb_invertida(int estado_semaforo) {
    uint8_t r = 0, g = 0, b = 0;

    if (!modo_noturno) {
        switch (estado_semaforo) {
            case ESTADO_VERMELHO:
                r = 0; g = 50; b = 0;
                break;
            case ESTADO_AMARELO:
                r = 50; g = 50; b = 0;
                break;
            case ESTADO_VERDE:
                r = 50; g = 0; b = 0;
                break;
        }
    } else {
        r = 50; g = 50; b = 0;
    }

    atualizar_buffer_com_semaforo(0);
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (buffer_leds[i]) {
            pio_sm_put_blocking(pio0, 0, urgb_u32(r, g, b) << 8u);
        } else {
            pio_sm_put_blocking(pio0, 0, 0);
        }
    }
    sleep_us(60);
}

void ssd1306_display_bitmap_partial(ssd1306_t *ssd, const unsigned char *bitmap, int x_offset, int y_offset) {
    ssd1306_fill(ssd, 0);
    for (int i = 0; i < (DISPLAY_HEIGHT / 8) * DISPLAY_WIDTH; i++) {
        ssd->ram_buffer[i + 1] = bitmap[i];
    }
}

void adicionar_texto_informativo() {
    char linha1[16], linha2[16], linha3[16], linha4[16];
    memset(linha1, 0, sizeof(linha1));
    memset(linha2, 0, sizeof(linha2));
    memset(linha3, 0, sizeof(linha3));
    memset(linha4, 0, sizeof(linha4));

    int x_pos = 70, y_pos = 0, altura_linha = 10;

    if (modo_noturno) {
        strcpy(linha1, "NOTURNO AMARELO");
        strcpy(linha2, "PISCANTE");
        ssd1306_draw_string(&display, linha1, x_pos - 70, y_pos);
        ssd1306_draw_string(&display, linha2, x_pos - 35, y_pos + altura_linha);
    } else {
        switch (estado_semaforo) {
            case ESTADO_VERDE:
                strcpy(linha1, "NORMAL"); strcpy(linha2, "VERDE"); strcpy(linha3, "(SIGA)");
                break;
            case ESTADO_AMARELO:
                strcpy(linha1, "NORMAL"); strcpy(linha2, "AMARELO");
                break;
            case ESTADO_VERMELHO:
                strcpy(linha1, "NORMAL"); strcpy(linha2, "VERMELHO");
                break;
        }
        ssd1306_draw_string(&display, linha1, x_pos - 10, y_pos);
        ssd1306_draw_string(&display, linha2, x_pos - 10, y_pos + altura_linha);
        ssd1306_draw_string(&display, linha3, x_pos - 10, y_pos + 2 * altura_linha);
    }
}

void iniciar_exibicao_sinal() {
    exibindo_bitmap_sinal = true;
    sinal_alternado = false;
    tempo_inicio_sinal = to_ms_since_boot(get_absolute_time());
    tempo_ultimo_alternancia_sinal = tempo_inicio_sinal;
}

void atualizar_display() {
    if (modo_noturno) {
        if (to_ms_since_boot(get_absolute_time()) - tempo_ultimo_alternancia_noturno >= TEMPO_ALTERNANCIA_NOTURNO) {
            noturno_alternado = !noturno_alternado;
            tempo_ultimo_alternancia_noturno = to_ms_since_boot(get_absolute_time());
        }
        const unsigned char *bitmap_noturno = noturno_alternado ? epd_bitmap_noturnoTwo : epd_bitmap_noturnoOne;
        ssd1306_display_bitmap_partial(&display, bitmap_noturno, 0, 0);
    } else {
        if (exibindo_bitmap_sinal) {
            if (to_ms_since_boot(get_absolute_time()) - tempo_inicio_sinal >= TEMPO_EXIBICAO_SINAL) {
                exibindo_bitmap_sinal = false;
            } else {
                if (to_ms_since_boot(get_absolute_time()) - tempo_ultimo_alternancia_sinal >= TEMPO_ALTERNANCIA_SINAL) {
                    sinal_alternado = !sinal_alternado;
                    tempo_ultimo_alternancia_sinal = to_ms_since_boot(get_absolute_time());
                }
                const unsigned char *bitmap_sinal = NULL;
                if (estado_semaforo == ESTADO_VERDE) {
                    bitmap_sinal = sinal_alternado ? epd_bitmap_sinalPass : epd_bitmap_sinal;
                } else if (estado_semaforo == ESTADO_VERMELHO) {
                    bitmap_sinal = sinal_alternado ? epd_bitmap_sinalSStop : epd_bitmap_sinal;
                } else {
                    bitmap_sinal = epd_bitmap_sinal;
                }
                ssd1306_display_bitmap_partial(&display, bitmap_sinal, 0, 0);
            }
        } else {
            switch (estado_semaforo) {
                case ESTADO_VERDE:
                    if (to_ms_since_boot(get_absolute_time()) - tempo_ultimo_frame >= TEMPO_ANIMACAO) {
                        frame_atual = (frame_atual + 1) % NUM_FRAMES;
                        tempo_ultimo_frame = to_ms_since_boot(get_absolute_time());
                    }
                    switch (frame_atual) {
                        case 0: ssd1306_display_bitmap_partial(&display, epd_bitmap_blindOne, 0, 0); break;
                        case 1: ssd1306_display_bitmap_partial(&display, epd_bitmap_blindTwo, 0, 0); break;
                        case 2: ssd1306_display_bitmap_partial(&display, epd_bitmap_blindThree, 0, 0); break;
                    }
                    break;
                case ESTADO_AMARELO:
                    ssd1306_display_bitmap_partial(&display, epd_bitmap_blindThree, 0, 0);
                    break;
                case ESTADO_VERMELHO:
                    ssd1306_display_bitmap_partial(&display, epd_bitmap_blindOne, 0, 0);
                    break;
            }
        }
    }
    adicionar_texto_informativo();
    ssd1306_send_data(&display);
}

void iniciar_buzzer(uint freq) {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint chan = pwm_gpio_to_channel(BUZZER_PIN);
    uint32_t clock = 125000000;
    uint32_t divider = 100;
    uint32_t wrap = clock / (divider * freq);
    pwm_set_clkdiv(slice_num, divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, chan, wrap / 2);
    pwm_set_enabled(slice_num, true);
    estado_buzzer = true;
}

void parar_buzzer() {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice_num, false);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
    gpio_put(BUZZER_PIN, 0);
    estado_buzzer = false;
}

void init_display() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&display, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, DISPLAY_ADDR, I2C_PORT);
    ssd1306_config(&display);
}

bool botao_pressionado(uint gpio) {
    static uint32_t last_time[2] = {0};
    uint idx = (gpio == BOTAO_A) ? 0 : 1;
    
    if (!gpio_get(gpio)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_time[idx] > DEBOUNCE_TIME) {
            last_time[idx] = now;
            return true;
        }
    }
    return false;
}

void vButtonTask(void *pvParameters) {
    while (1) {
        if (botao_pressionado(BOTAO_A)) {
            modo_noturno = !modo_noturno;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            tempo_ultimo_estado = now;
            tempo_ultimo_beep = now;
            tempo_ultimo_pisca = now;
            tempo_ultimo_alternancia_noturno = now;
            noturno_alternado = false;
            parar_buzzer();

            if (modo_noturno) {
                gpio_put(LED_VERMELHO, 0);
                gpio_put(LED_VERDE, 0);
                estado_led_amarelo = false;
                exibindo_bitmap_sinal = false;
            } else {
                estado_semaforo = ESTADO_VERDE;
                gpio_put(LED_VERMELHO, 0);
                gpio_put(LED_AMARELO, 0);
                gpio_put(LED_VERDE, 1);
                frame_atual = 0;
                iniciar_exibicao_sinal();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vTrafficLightTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10);

    while (1) {
        uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

        if (modo_noturno) {
            if (tempo_atual - tempo_ultimo_pisca >= TEMPO_PISCA) {
                estado_led_amarelo = !estado_led_amarelo;
                gpio_put(LED_AMARELO, estado_led_amarelo);
                tempo_ultimo_pisca = tempo_atual;
            }

            if (tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_NOTURNO) {
                iniciar_buzzer(1500);
                tempo_ultimo_beep = tempo_atual;
            }

            if (estado_buzzer && tempo_atual - tempo_ultimo_beep >= DURACAO_BEEP_NOTURNO) {
                parar_buzzer();
            }
        } else {
            switch (estado_semaforo) {
                case ESTADO_VERDE:
                    if (!estado_buzzer && tempo_atual - tempo_ultimo_beep >= 1000) {
                        iniciar_buzzer(2000);
                        tempo_ultimo_beep = tempo_atual;
                    }
                    if (estado_buzzer && tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_VERDE) {
                        parar_buzzer();
                    }
                    if (tempo_atual - tempo_ultimo_estado >= TEMPO_VERDE) {
                        estado_semaforo = ESTADO_AMARELO;
                        gpio_put(LED_VERDE, 0);
                        gpio_put(LED_AMARELO, 1);
                        tempo_ultimo_estado = tempo_atual;
                    }
                    break;

                case ESTADO_AMARELO:
                    if (tempo_atual - tempo_ultimo_beep >= (estado_buzzer ? TEMPO_BEEP_AMARELO : TEMPO_OFF_AMARELO)) {
                        if (estado_buzzer) parar_buzzer();
                        else iniciar_buzzer(3000);
                        tempo_ultimo_beep = tempo_atual;
                    }
                    if (tempo_atual - tempo_ultimo_estado >= TEMPO_AMARELO) {
                        estado_semaforo = ESTADO_VERMELHO;
                        gpio_put(LED_AMARELO, 0);
                        gpio_put(LED_VERMELHO, 1);
                        tempo_ultimo_estado = tempo_atual;
                        parar_buzzer();
                        iniciar_exibicao_sinal();
                    }
                    break;

                case ESTADO_VERMELHO:
                    if (estado_buzzer) {
                        if (tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_VERMELHO) {
                            parar_buzzer();
                            tempo_ultimo_beep = tempo_atual;
                        }
                    } else {
                        if (tempo_atual - tempo_ultimo_beep >= TEMPO_OFF_VERMELHO) {
                            iniciar_buzzer(1000);
                            tempo_ultimo_beep = tempo_atual;
                        }
                    }
                    if (tempo_atual - tempo_ultimo_estado >= TEMPO_VERMELHO) {
                        estado_semaforo = ESTADO_VERDE;
                        gpio_put(LED_VERMELHO, 0);
                        gpio_put(LED_VERDE, 1);
                        frame_atual = 0;
                        tempo_ultimo_estado = tempo_atual;
                        parar_buzzer();
                        iniciar_exibicao_sinal();
                    }
                    break;
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void vDisplayTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(TEMPO_ATUALIZACAO_DISPLAY);

    while (1) {
        atualizar_display();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void vLEDMatrixTask(void *pvParameters) {
    while (1) {
        atualizar_matriz_rgb_invertida(estado_semaforo);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void tela_inicial() {
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);

    const unsigned char *bitmaps[] = {epd_bitmap_startOne, epd_bitmap_startTwo, epd_bitmap_startThree, epd_bitmap_startFour};
    for (int i = 0; i < 4; i++) {
        ssd1306_display_bitmap_partial(&display, bitmaps[i], 0, 0);
        ssd1306_send_data(&display);
        uint32_t start = to_ms_since_boot(get_absolute_time());
        while (to_ms_since_boot(get_absolute_time()) - start < TEMPO_ANIMACAO_INICIAL) {
            if (!gpio_get(BOTAO_B) && (to_ms_since_boot(get_absolute_time()) - last_button_time > DEBOUNCE_TIME)) {
                tela_inicial_concluida = true;
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ssd1306_display_bitmap_partial(&display, epd_bitmap_startPress, 0, 0);
    ssd1306_send_data(&display);

    while (!tela_inicial_concluida) {
        if (!gpio_get(BOTAO_B) && (to_ms_since_boot(get_absolute_time()) - last_button_time > DEBOUNCE_TIME)) {
            tela_inicial_concluida = true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "IntelliTraffic", 10, 20);
    ssd1306_draw_string(&display, "Light", 45, 35);
    ssd1306_send_data(&display);
    sleep_ms(2000);
}

void vStartupTask(void *pvParameters) {
    tela_inicial();

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    xTaskCreate(vTrafficLightTask, "Traffic", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(vDisplayTask, "Display", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(vButtonTask, "Button", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(vLEDMatrixTask, "LEDMatrix", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    gpio_init(LED_VERMELHO);
    gpio_init(LED_AMARELO);
    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERMELHO, GPIO_OUT);
    gpio_set_dir(LED_AMARELO, GPIO_OUT);
    gpio_set_dir(LED_VERDE, GPIO_OUT);

    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    init_display();

    xTaskCreate(vStartupTask, "Startup", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    vTaskStartScheduler();

    while (1);
}