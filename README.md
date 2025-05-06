
# IntelliTraffic Light

## Sistema de Semáforo Inteligente com FreeRTOS para Raspberry Pi Pico

---

### 📚 Visão Geral

O **IntelliTraffic Light** é um sistema de controle de tráfego inteligente baseado no **Raspberry Pi Pico** que implementa um semáforo adaptativo com modo noturno. Utilizando **FreeRTOS** para gerenciamento de tarefas em tempo real, o dispositivo controla LEDs, matrizes RGB e display OLED, oferecendo uma solução educacional para estudos de sistemas embarcados e IoT.

---

### 🔎 Descrição Detalhada

Este projeto combina:

- Controle de múltiplos periféricos (LEDs, matriz WS2812B, display OLED)
- Sistema operacional em tempo real (FreeRTOS)
- Interface homem-máquina (botões e buzzer)
- Gerenciamento de energia eficiente (modo noturno)

Criando assim uma plataforma completa para aprendizado de sistemas embarcados críticos.

---

### ✨ Características Principais

- **Dual Mode Operation:** Modo normal (ciclo completo) e noturno (piscante)
- **LED Matrix Control:** Matriz 5x5 WS2812B com padrões personalizados
- **Real-Time Feedback:** Display OLED com informações de estado
- **FreeRTOS Integration:** 4 tarefas com prioridades diferentes
- **Audio Feedback:** Buzzer com sons distintos para cada estado
- **User Interaction:** Dois botões para controle de modo
- **Energy Efficient:** Diminuição de brilho no modo noturno
- **Easy Deployment:** Sistema plug-and-play via USB

---

### 🛠️ Hardware Utilizado

| Componente                       | Descrição                                  |
| :------------------------------- | :------------------------------------------- |
| **Placa Principal**        | Raspberry Pi Pico (RP2040)                   |
| **Display**                | OLED SSD1306 128x64 (I2C)                    |
| **LED Matrix**             | WS2812B 5x5 (PIO)                            |
| **Botões**                | GPIO 5 (Modo), GPIO 6 (Inicialização)      |
| **Buzzer**                 | GPIO 10 (PWM)                                |
| **LEDs de Tráfego**       | GPIO 11 (Verde), 12 (Amarelo), 13 (Vermelho) |
| **I2C**                    | GPIO 14 (SDA), GPIO 15 (SCL)                 |
| **Fonte de Alimentação** | USB 5V ou Bateria 3.7V                       |

---

### ⚙️ Princípio de Funcionamento

1. **Máquina de Estados Principal:**

   - Verde (5s) → Amarelo (2s) → Vermelho (5s)
   - Transições controladas por temporizadores FreeRTOS
2. **Modo Noturno:**

   - Piscar contínuo do LED amarelo (1Hz)
   - Matriz RGB em padrão estático amarelo
   - Bips periódicos no buzzer (2s intervalos)
3. **Sistema de Feedback:**

   - Atualização periódica do display OLED (500ms)
   - Geração de tons via PWM para o buzzer
   - Controle de brilho da matriz RGB

---

### 🔄 Fluxo de Operação

1. Inicialização com animação de boot no display
2. Aguardar pressionamento do Botão B para iniciar
3. Modo Normal:
   - Ciclo completo de semáforo
   - Atualização contínua da matriz RGB
   - Feedback sonoro em transições
4. Modo Noturno (ativado por Botão A):
   - Piscar do LED amarelo
   - Padrão estático na matriz
   - Bips periódicos
5. Troca entre modos via Botão A

---

### ⚡ Aspectos Técnicos Importantes

#### 1. Integração FreeRTOS

- **Tarefas:**
  - TrafficLightTask: Gerencia estados do semáforo (Prioridade 3)
  - DisplayTask: Atualização do OLED (Prioridade 2)
  - ButtonTask: Leitura de botões (Prioridade 2)
  - LEDMatrixTask: Controle da matriz RGB (Prioridade 1)

#### 2. Controle da Matriz WS2812B

- Protocolo PIO personalizado
- Mapeamento de coordenadas para endereço linear
- Controle de brilho via PWM

#### 3. Gestão de Energia

- Redução de 50% do brilho no modo noturno
- Desativação de periféricos não essenciais
- Sleep mode entre atualizações

#### 4. Sistema de Debounce

- Filtragem digital de 300ms para botões
- Verificação por polling em tarefa dedicada
- Proteção contra múltiplos acionamentos

---

### 🧩 Estrutura do Código

| Arquivo                    | Função                             |
| :------------------------- | :----------------------------------- |
| **intellitraffic.c** | Lógica principal e tarefas FreeRTOS |
| **ssd1306.h/c**      | Driver para display OLED             |
| **bitmap.h/c**       | Armazenamento de imagens e fontes    |
| **FreeRTOSConfig.h** | Configuração do kernel RTOS        |
| **ws2812.pio**       | Protocolo PIO para matriz LED        |

---

### 🚀 Instalação e Configuração

#### Pré-requisitos

- SDK do Raspberry Pi Pico
- CMake 3.13+
- Toolchain ARM GCC
- FreeRTOS-Kernel (v10.4.3+)

#### 🔧 Como Compilar

```bash
git clone https://github.com/TorRLD/intellitraffic_light.git
cd intellitraffic_light
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4

# Conecte o Pico em modo bootloader (BOOTSEL)
cp intellitraffic.uf2 /path/to/PICO_DRIVE
```


### 📄 Licença

Este projeto está licenciado sob a [Licença MIT](https://license/).
*FreeRTOS é licenciado sob a MIT Open Source License*
