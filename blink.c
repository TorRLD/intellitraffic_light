#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/bitmap.h" // Incluindo o arquivo de bitmaps
#include <stdio.h>
#include <string.h>

#include "ws2812.pio.h"

#define NUM_PIXELS 25
#define WS2812_PIN 7
#define IS_RGBW false

// Matriz de pixels
bool buffer_leds[NUM_PIXELS] = {false};

// Cores para a matriz WS2812
#define COR_WS2812_R 20
#define COR_WS2812_G 20
#define COR_WS2812_B 50

// Definição dos pinos
#define LED_VERMELHO 13
#define LED_AMARELO 12
#define LED_VERDE 11
#define BUZZER_PIN 10
#define BOTAO_A 5
#define BOTAO_B 6

// Definição dos pinos I2C para o display OLED
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDR 0x3C
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// Estados do semáforo no modo normal
typedef enum
{
    ESTADO_VERDE,
    ESTADO_AMARELO,
    ESTADO_VERMELHO
} EstadoSemaforo;

// Tempos em milissegundos
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
#define TEMPO_BEEP_NOTURNO 2000       // Tempo entre beeps no modo noturno (2s)
#define DURACAO_BEEP_NOTURNO 100      // Duração do beep no modo noturno
#define TEMPO_ANIMACAO 300            // Tempo entre frames da animação (300ms)
#define TEMPO_ALTERNANCIA_NOTURNO 500 // Tempo para alternar entre os bitmaps noturnos (500ms)

// Tempos das animações dos sinais - REDUZIDOS
#define TEMPO_EXIBICAO_SINAL 2000   // Tempo para exibir o bitmap de sinal (reduzido para 2s)
#define TEMPO_ALTERNANCIA_SINAL 250 // Tempo para alternar entre os sinais (reduzido para 250ms)

// Tempo para a animação de inicialização
#define TEMPO_ANIMACAO_INICIAL 500 // Tempo para cada frame da animação inicial (500ms)

// Flag global para controlar o modo
volatile bool modo_noturno = false;
volatile uint32_t last_button_time = 0;
#define DEBOUNCE_TIME 300                      // Debounce de 300ms
bool noturno_alternado = false;                // Flag para controlar qual bitmap está sendo mostrado no modo noturno
uint32_t tempo_ultimo_alternancia_noturno = 0; // Tempo da última alternância entre os bitmaps noturnos

// Estado atual do semáforo
EstadoSemaforo estado_semaforo = ESTADO_VERDE;

// Variáveis para controle de tempo
uint32_t tempo_atual = 0;
uint32_t tempo_ultimo_estado = 0;
uint32_t tempo_ultimo_beep = 0;
uint32_t tempo_ultimo_pisca = 0;
uint32_t tempo_ultimo_display = 0;
uint32_t tempo_ultimo_frame = 0;             // Controla o tempo do último frame mostrado
uint32_t tempo_inicio_sinal = 0;             // Controla o tempo de início da exibição do sinal
uint32_t tempo_ultimo_alternancia_sinal = 0; // Tempo da última alternância entre os sinais
bool estado_led_amarelo = false;
bool estado_buzzer = false;
bool exibindo_bitmap_sinal = false; // Flag para controlar a exibição do bitmap de sinal
bool sinal_alternado = false;       // Flag para controlar qual sinal está sendo mostrado

// Variável para controle da animação
uint8_t frame_atual = 0;
#define NUM_FRAMES 3 // Número de frames na animação

// Display OLED
ssd1306_t display;

// Flag para controlar se a tela inicial já foi concluída
volatile bool tela_inicial_concluida = false;

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t color = ((uint32_t)(g) << 16) | ((uint32_t)(r) << 8) | b;
    pio_sm_put_blocking(pio0, 0, color << 8u);
}

const bool padrao_semaforo[1][5][5] = {
    {

        {false, false, false, false, false},
        {false, true, true, true, false},
        {false, true, true, true, false},
        {false, true, true, true, false},
        {false, false, false, false, false}}};
// Função auxiliar para formatar cores para a matriz de LEDs
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

// Função auxiliar para enviar um pixel para a matriz
static inline void enviar_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}
// Define os LEDs da matriz com base no buffer
void definir_leds(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t cor = urgb_u32(r, g, b);
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        if (buffer_leds[i])
            enviar_pixel(cor);
        else
            enviar_pixel(0);
    }
    sleep_us(60);
}
// Atualiza o buffer com um padrão de carinha
void atualizar_buffer_com_semaforo(int tipo)
{
    // tipo: 0 = neutra, 1 = feliz, 2 = triste
    for (int linha = 0; linha < 5; linha++)
    {
        for (int coluna = 0; coluna < 5; coluna++)
        {
            int indice = linha * 5 + coluna;
            buffer_leds[indice] = padrao_semaforo[tipo][linha][coluna];
        }
    }
}
// Corrija a função de atualização da matriz
void atualizar_matriz_rgb_invertida(int estado_semaforo)
{
    uint8_t r = 0, g = 0, b = 0;

    // Definir a cor com base no estado do semáforo
    if (!modo_noturno)
    {

        switch (estado_semaforo)
        {
        case ESTADO_VERMELHO: // Vermelho no semáforo => Verde na matriz
            r = 0;
            g = 80;
            b = 0;
            break;
        case ESTADO_AMARELO: // Amarelo => Amarelo
            r = 80;
            g = 255;
            b = 0;
            break;
        case ESTADO_VERDE: // Verde no semáforo => Vermelho na matriz
            r = 255;
            g = 0;
            b = 0;
            break;
        }
    }
    else
    {
        r = 255;
        g = 255;
        b = 0;
    }

    // Primeiro, atualize o buffer com o padrão do semáforo
    atualizar_buffer_com_semaforo(0);

    // Em seguida, envie todos os 25 pixels com as cores apropriadas
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        if (buffer_leds[i])
        {
            // Se o LED estiver ativo no buffer, envie a cor definida
            pio_sm_put_blocking(pio0, 0, urgb_u32(r, g, b) << 8u);
        }
        else
        {
            // Se o LED estiver inativo, envie preto (desligado)
            pio_sm_put_blocking(pio0, 0, 0);
        }
    }

    // Delay necessário após enviar os dados para os LEDs WS2812B
    sleep_us(60);
}

// Função para exibir um bitmap no display (com offset para deixar espaço para texto)
void ssd1306_display_bitmap_partial(ssd1306_t *ssd, const unsigned char *bitmap, int x_offset, int y_offset)
{
    // Limpar o display primeiro
    ssd1306_fill(ssd, 0);

    // Copiar os dados do bitmap para a parte da tela onde queremos o bitmap
    // Para um bitmap de 128x64, se quisermos mostrar na metade direita da tela com x_offset=64,
    // precisamos copiar os dados de maneira seletiva

    // Por simplicidade, vamos apenas copiar o bitmap completo
    // (Na implementação real, você pode precisar ajustar isso para o layout específico)
    for (int i = 0; i < (DISPLAY_HEIGHT / 8) * DISPLAY_WIDTH; i++)
    {
        ssd->ram_buffer[i + 1] = bitmap[i];
    }

    // Não enviamos os dados ainda, pois vamos adicionar texto depois
}

// Função de callback para interrupção do botão B (tela inicial)
void botao_b_callback(uint gpio, uint32_t events)
{
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    // Implementa debounce simples
    if (current_time - last_button_time > DEBOUNCE_TIME)
    {
        last_button_time = current_time;

        // Se estamos na tela inicial, permite prosseguir
        if (!tela_inicial_concluida)
        {
            tela_inicial_concluida = true;
            printf("Botão B pressionado, iniciando programa principal\n");
        }
    }
}

void tela_inicial()
{
    // Configurar o botão B para detectar quando for pressionado
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &botao_b_callback);

    // Exibir a sequência de animação inicial
    // Primeiro exibimos startOne, startTwo, startThree, startFour em sequência
    ssd1306_display_bitmap_partial(&display, epd_bitmap_startOne, 0, 0);
    ssd1306_send_data(&display);
    sleep_ms(TEMPO_ANIMACAO_INICIAL);

    ssd1306_display_bitmap_partial(&display, epd_bitmap_startTwo, 0, 0);
    ssd1306_send_data(&display);
    sleep_ms(TEMPO_ANIMACAO_INICIAL);

    ssd1306_display_bitmap_partial(&display, epd_bitmap_startThree, 0, 0);
    ssd1306_send_data(&display);
    sleep_ms(TEMPO_ANIMACAO_INICIAL);

    ssd1306_display_bitmap_partial(&display, epd_bitmap_startFour, 0, 0);
    ssd1306_send_data(&display);
    sleep_ms(TEMPO_ANIMACAO_INICIAL);

    // Agora exibimos o startPress e aguardamos o botão B ser pressionado
    ssd1306_display_bitmap_partial(&display, epd_bitmap_startPress, 0, 0);
    ssd1306_send_data(&display);

    // Aguardar até que o botão B seja pressionado
    printf("Aguardando pressionar botão B para iniciar...\n");
    while (!tela_inicial_concluida)
    {
        // Apenas esperar
        sleep_ms(100);
    }

    // Exibir uma mensagem de que está iniciando
    ssd1306_fill(&display, 0);
    ssd1306_draw_string(&display, "IntelliTraffic", 10, 20);
    ssd1306_draw_string(&display, "Light", 45, 35);
    ssd1306_send_data(&display);
    sleep_ms(2000);
}

// Função para inicializar o display OLED
void init_display()
{
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&display, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, DISPLAY_ADDR, I2C_PORT);
    ssd1306_config(&display);
}

// Função para adicionar o texto informativo no canto superior direito
void adicionar_texto_informativo()
{
    char linha1[16];
    char linha2[16];
    char linha3[16]; // Linha adicional
    char linha4[16]; // Linha extra para palavras longas

    // Configuramos para texto pequeno no canto superior direito
    int x_pos = 70;        // Posição X do texto (canto direito da tela)
    int y_pos = 0;         // Posição Y do texto (topo da tela)
    int altura_linha = 10; // Altura de cada linha de texto

    // Limpar as strings
    memset(linha1, 0, sizeof(linha1));
    memset(linha2, 0, sizeof(linha2));
    memset(linha3, 0, sizeof(linha3));
    memset(linha4, 0, sizeof(linha4));

    // Informações específicas do modo
    if (modo_noturno)
    {
        strcpy(linha1, "NOTURNO AMARELO");
        strcpy(linha2, "PISCANTE");
        // strcpy(linha3, "PISCANT");
        // strcpy(linha4, "E"); // Colocamos o 'E' na linha de baixo, a ser alinhado com 'T' de PISCANT
        ssd1306_draw_string(&display, linha1, x_pos - 70, y_pos);
        ssd1306_draw_string(&display, linha2, x_pos - 35, y_pos + altura_linha);

        // Se houver uma quarta linha, exiba
        if (linha4[0] != '\0')
        {
            if (modo_noturno && strlen(linha4) == 1)
            {

                ssd1306_draw_string(&display, linha1, x_pos - 60, y_pos);
                ssd1306_draw_string(&display, linha2, x_pos - 60, y_pos + altura_linha);
                // Posicionar o "E" diretamente abaixo do "T" de "PISCANT"
                ssd1306_draw_string(&display, linha4, x_pos + strlen(linha3) - 1, y_pos + 3 * altura_linha);
            }
            else
            {

                // Caso contrário, usamos o alinhamento normal
                ssd1306_draw_string(&display, linha4, x_pos - 60, y_pos * altura_linha);
            }
        }
    }

    else
    {

        switch (estado_semaforo)
        {
        case ESTADO_VERDE:
            strcpy(linha1, "NORMAL");
            strcpy(linha2, "VERDE");
            strcpy(linha3, "(SIGA)");
            break;
        case ESTADO_AMARELO:
            strcpy(linha1, "NORMAL");
            strcpy(linha2, "AMARELO");
            break;
        case ESTADO_VERMELHO:
            strcpy(linha1, "NORMAL");
            strcpy(linha2, "VERMELHO");
            // strcpy(linha3, "O"); // Colocamos o 'O' na linha de baixo, mas alinhado com VERMELH
            break;
        }

        // Desenhar o texto na posição especificada, linha por linha
        ssd1306_draw_string(&display, linha1, x_pos - 10, y_pos);
        ssd1306_draw_string(&display, linha2, x_pos - 10, y_pos + altura_linha);
        ssd1306_draw_string(&display, linha3, x_pos - 10, y_pos + 2 * altura_linha);
    }
}

// Função para iniciar a exibição do bitmap de sinal
void iniciar_exibicao_sinal()
{
    exibindo_bitmap_sinal = true;
    sinal_alternado = false; // Começamos com o primeiro bitmap
    tempo_inicio_sinal = to_ms_since_boot(get_absolute_time());
    tempo_ultimo_alternancia_sinal = tempo_inicio_sinal;
}

// Função para atualizar o display, escolhendo o bitmap correto e adicionando texto
void atualizar_display()
{
    if (modo_noturno)
    {
        // No modo noturno, alternamos entre dois bitmaps específicos para o modo noturno

        // Verificar se é hora de alternar o bitmap
        if (tempo_atual - tempo_ultimo_alternancia_noturno >= TEMPO_ALTERNANCIA_NOTURNO)
        {
            noturno_alternado = !noturno_alternado; // Alterna o bitmap
            tempo_ultimo_alternancia_noturno = tempo_atual;
        }

        // Selecionar o bitmap conforme a alternância
        const unsigned char *bitmap_noturno = noturno_alternado ? epd_bitmap_noturnoTwo : epd_bitmap_noturnoOne;

        // Exibir o bitmap selecionado
        ssd1306_display_bitmap_partial(&display, bitmap_noturno, 0, 0);

        // Adiciona o texto informativo sobre a imagem
        adicionar_texto_informativo();

        // Envia os dados para o display
        ssd1306_send_data(&display);
    }
    else
    {
        // Verificar se estamos no período de exibição do bitmap de sinal
        if (exibindo_bitmap_sinal)
        {
            // Verificar se já passou o tempo de exibição do sinal
            if (tempo_atual - tempo_inicio_sinal >= TEMPO_EXIBICAO_SINAL)
            {
                exibindo_bitmap_sinal = false;
            }
            else
            {
                // Verificar se é hora de alternar o bitmap do sinal
                if (tempo_atual - tempo_ultimo_alternancia_sinal >= TEMPO_ALTERNANCIA_SINAL)
                {
                    sinal_alternado = !sinal_alternado; // Alterna o sinal
                    tempo_ultimo_alternancia_sinal = tempo_atual;
                }

                // Selecionar o bitmap correto para o estado atual e alternância
                const unsigned char *bitmap_sinal = NULL;

                if (estado_semaforo == ESTADO_VERDE)
                {
                    // No verde, alternamos entre sinal e sinalPass
                    bitmap_sinal = sinal_alternado ? epd_bitmap_sinalPass : epd_bitmap_sinal;
                }
                else if (estado_semaforo == ESTADO_VERMELHO)
                {
                    // No vermelho, alternamos entre sinal e sinalSStop
                    bitmap_sinal = sinal_alternado ? epd_bitmap_sinalSStop : epd_bitmap_sinal;
                }
                else
                {
                    // No amarelo, usamos apenas o sinal padrão
                    bitmap_sinal = epd_bitmap_sinal;
                }

                // Exibir o bitmap selecionado
                ssd1306_display_bitmap_partial(&display, bitmap_sinal, 0, 0);
                adicionar_texto_informativo();
                ssd1306_send_data(&display);
                return;
            }
        }

        // Exibição normal após o período do sinal (ou se não estiver exibindo o sinal)
        switch (estado_semaforo)
        {
        case ESTADO_VERDE:
            // No verde, mostramos a animação completa (ciclando todos os frames)
            if (tempo_atual - tempo_ultimo_frame >= TEMPO_ANIMACAO)
            {
                frame_atual = (frame_atual + 1) % NUM_FRAMES; // Avança para o próximo frame
                tempo_ultimo_frame = tempo_atual;
            }

            // Seleciona o frame atual para mostrar
            switch (frame_atual)
            {
            case 0:
                ssd1306_display_bitmap_partial(&display, epd_bitmap_blindOne, 0, 0);
                break;
            case 1:
                ssd1306_display_bitmap_partial(&display, epd_bitmap_blindTwo, 0, 0);
                break;
            case 2:
                ssd1306_display_bitmap_partial(&display, epd_bitmap_blindThree, 0, 0);
                break;
            }
            break;

        case ESTADO_AMARELO:
            // No amarelo, mostramos apenas o blindThree (parado)
            ssd1306_display_bitmap_partial(&display, epd_bitmap_blindThree, 0, 0);
            break;

        case ESTADO_VERMELHO:
            // No vermelho, mostramos apenas o blindOne (parado)
            ssd1306_display_bitmap_partial(&display, epd_bitmap_blindOne, 0, 0);
            break;
        }

        // Adiciona o texto informativo sobre a imagem
        adicionar_texto_informativo();

        // Envia os dados para o display
        ssd1306_send_data(&display);
    }
}

// Função para configurar o buzzer para começar a tocar
void iniciar_buzzer(uint freq)
{
    // Configurar PWM
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint chan = pwm_gpio_to_channel(BUZZER_PIN);

    // Definir frequência
    uint32_t clock = 125000000;
    uint32_t divider = 100;
    uint32_t wrap = clock / (divider * freq);
    pwm_set_clkdiv(slice_num, divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, chan, wrap / 2); // 50% duty cycle

    // Iniciar PWM
    pwm_set_enabled(slice_num, true);
    estado_buzzer = true;
}

// Função para parar o buzzer
void parar_buzzer()
{
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);

    // Desligar PWM
    pwm_set_enabled(slice_num, false);

    // Voltar para modo GPIO normal
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);
    estado_buzzer = false;
}

// Função de callback para interrupção do botão A (para trocar de modo)
void botao_a_callback(uint gpio, uint32_t events)
{
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    // Implementa debounce simples
    if (current_time - last_button_time > DEBOUNCE_TIME)
    {
        modo_noturno = !modo_noturno;
        last_button_time = current_time;

        // Reiniciar tempos ao mudar de modo
        tempo_ultimo_estado = current_time;
        tempo_ultimo_beep = current_time;
        tempo_ultimo_pisca = current_time;
        tempo_ultimo_frame = current_time;

        // Reiniciar as variáveis de controle dos bitmaps noturnos
        tempo_ultimo_alternancia_noturno = current_time;
        noturno_alternado = false;

        // Limpar estado do buzzer ao mudar de modo
        parar_buzzer();

        // Reset dos LEDs no caso de mudança para modo noturno
        if (modo_noturno)
        {
            gpio_put(LED_VERMELHO, 0);
            gpio_put(LED_VERDE, 0);
            gpio_put(LED_AMARELO, 0);
            estado_led_amarelo = false;
            exibindo_bitmap_sinal = false; // Cancela exibição do sinal se estiver acontecendo
        }
        else
        {
            // Reinicia para estado verde no modo normal
            estado_semaforo = ESTADO_VERDE;
            gpio_put(LED_VERMELHO, 0);
            gpio_put(LED_AMARELO, 0);
            gpio_put(LED_VERDE, 1);
            frame_atual = 0; // Reinicia a animação

            // Inicia a exibição do bitmap de sinal para o verde
            iniciar_exibicao_sinal();
        }

        // Atualiza o display imediatamente ao mudar de modo
        atualizar_display();

        printf("Modo alterado: %s\n", modo_noturno ? "Noturno" : "Normal");
    }
}

// Função para verificar se é hora de mudar de estado
bool verificar_troca_estado(uint32_t duracao)
{
    return (tempo_atual - tempo_ultimo_estado >= duracao);
}

int main()
{
    stdio_init_all();

    // Inicializa os LEDs
    gpio_init(LED_VERMELHO);
    gpio_init(LED_AMARELO);
    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERMELHO, GPIO_OUT);
    gpio_set_dir(LED_AMARELO, GPIO_OUT);
    gpio_set_dir(LED_VERDE, GPIO_OUT);

    // Inicializa o buzzer como GPIO de saída inicialmente
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    // Inicializa o display OLED
    init_display();

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
    // Mostra a tela inicial e aguarda o botão B ser pressionado
    tela_inicial();

    // Após a tela inicial, configura o botão A para alternar entre os modos
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &botao_a_callback);

    // Estado inicial: verde
    gpio_put(LED_VERDE, 1);
    tempo_ultimo_estado = to_ms_since_boot(get_absolute_time());

    // Inicia exibindo o bitmap de sinal para o verde
    iniciar_exibicao_sinal();

    // Mostra informações iniciais no display
    atualizar_display();

    while (true)
    {

        atualizar_matriz_rgb_invertida(estado_semaforo);

        tempo_atual = to_ms_since_boot(get_absolute_time());

        // Atualiza o display periodicamente
        if (tempo_atual - tempo_ultimo_display >= TEMPO_ATUALIZACAO_DISPLAY)
        {
            atualizar_display();
            atualizar_matriz_rgb_invertida(estado_semaforo);
            tempo_ultimo_display = tempo_atual;
        }

        if (modo_noturno)
        {
            // ----- MODO NOTURNO -----

            // Verificar se é hora de piscar o LED amarelo
            if (tempo_atual - tempo_ultimo_pisca >= TEMPO_PISCA)
            {
                estado_led_amarelo = !estado_led_amarelo;
                gpio_put(LED_AMARELO, estado_led_amarelo);
                atualizar_matriz_rgb_invertida(estado_semaforo);
                tempo_ultimo_pisca = tempo_atual;
            }

            // Implementar beep lento a cada 2s no modo noturno
            if (tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_NOTURNO)
            {
                iniciar_buzzer(1500); // 1.5kHz
                tempo_ultimo_beep = tempo_atual;
            }

            // Desligar o buzzer após 100ms
            if (estado_buzzer && tempo_atual - tempo_ultimo_beep >= DURACAO_BEEP_NOTURNO)
            {
                parar_buzzer();
            }
        }
        else
        {
            // ----- MODO NORMAL -----

            switch (estado_semaforo)
            {
            case ESTADO_VERDE:
                // Emitir um beep curto a cada segundo quando estiver verde
                if (!estado_buzzer && tempo_atual - tempo_ultimo_beep >= 1000)
                {                         // A cada 1 segundo
                    iniciar_buzzer(2000); // 2kHz
                    tempo_ultimo_beep = tempo_atual;
                }

                // Verifica se é hora de desligar o beep
                if (estado_buzzer && tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_VERDE)
                {
                    parar_buzzer();
                }

                // Verifica se é hora de mudar para amarelo
                if (verificar_troca_estado(TEMPO_VERDE))
                {
                    estado_semaforo = ESTADO_AMARELO;
                    gpio_put(LED_VERDE, 0);
                    gpio_put(LED_AMARELO, 1);
                    atualizar_matriz_rgb_invertida(estado_semaforo);
                    tempo_ultimo_estado = tempo_atual;
                    tempo_ultimo_beep = tempo_atual;
                }
                break;

            case ESTADO_AMARELO:
                // Beeps intermitentes no amarelo (liga/desliga a cada 100ms)
                if (tempo_atual - tempo_ultimo_beep >= (estado_buzzer ? TEMPO_BEEP_AMARELO : TEMPO_OFF_AMARELO))
                {
                    if (estado_buzzer)
                    {
                        parar_buzzer();
                    }
                    else
                    {
                        iniciar_buzzer(3000); // 3kHz
                    }
                    tempo_ultimo_beep = tempo_atual;
                }

                // Verifica se é hora de mudar para vermelho
                if (verificar_troca_estado(TEMPO_AMARELO))
                {
                    estado_semaforo = ESTADO_VERMELHO;
                    gpio_put(LED_AMARELO, 0);
                    gpio_put(LED_VERMELHO, 1);
                    atualizar_matriz_rgb_invertida(estado_semaforo);
                    tempo_ultimo_estado = tempo_atual;
                    tempo_ultimo_beep = tempo_atual;
                    // Garante que o buzzer esteja desligado ao mudar de estado
                    parar_buzzer();

                    // Inicia a exibição do bitmap de sinal ao entrar no vermelho
                    iniciar_exibicao_sinal();
                }
                break;

            case ESTADO_VERMELHO:
                // Beeps no vermelho (500ms ligado, 1.5s desligado)
                if (estado_buzzer)
                {
                    if (tempo_atual - tempo_ultimo_beep >= TEMPO_BEEP_VERMELHO)
                    {
                        parar_buzzer();
                        tempo_ultimo_beep = tempo_atual;
                    }
                }
                else
                {
                    if (tempo_atual - tempo_ultimo_beep >= TEMPO_OFF_VERMELHO)
                    {
                        iniciar_buzzer(1000); // 1kHz
                        tempo_ultimo_beep = tempo_atual;
                    }
                }

                // Verifica se é hora de mudar para verde
                if (verificar_troca_estado(TEMPO_VERMELHO))
                {
                    estado_semaforo = ESTADO_VERDE;
                    gpio_put(LED_VERMELHO, 0);
                    gpio_put(LED_VERDE, 1);
                    atualizar_matriz_rgb_invertida(estado_semaforo);
                    tempo_ultimo_estado = tempo_atual;
                    tempo_ultimo_beep = tempo_atual;
                    // Garante que o buzzer esteja desligado ao mudar de estado
                    parar_buzzer();
                    // Reinicia a animação
                    frame_atual = 0;
                    tempo_ultimo_frame = tempo_atual;

                    // Inicia a exibição do bitmap de sinal ao entrar no verde
                    iniciar_exibicao_sinal();
                }
                break;
            }
        }

        // Pequeno delay para não sobrecarregar o processador
        sleep_ms(10);
    }

    return 0;
}