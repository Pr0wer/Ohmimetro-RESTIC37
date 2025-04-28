#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/ws2812b.h"
#define ADC_PIN 28 // GPIO para o voltímetro
#define Botao_A 5  // GPIO para botão A

int R_conhecido = 10000;   // Resistor conhecido
float R_x = 0.0f;           // Resistor desconhecido
float ADC_VREF = 3.31;     // Tensão de referência do ADC
int ADC_RESOLUTION = 4095; // Resolução do ADC (12 bits)

// Tabela de cores das faixas dos resistores, indexados de acordo com o digito que representam
static const char *RESISTOR_COLORS[] = {
  "Preto", "Marrom", "Vermelho",
  "Laranja", "Amarelo", "Verde",
  "Azul", "Violeta", "Cinza", "Branco"
};
static const Rgb RESISTOR_RGB_COLOR[] = {
  {0.0f, 0.0f, 0.0f}, // Preto
  {2.0f, 1.5f, 0.0f}, // Marrom
  {1.0f, 0.0f, 0.0f}, // Vermelho
  {6.0f, 3.0f, 0.0f}, // Laranja
  {1.0f, 1.0f, 0.0f}, // Amarelo
  {0.0f, 1.0f, 0.0f}, // Verde
  {0.0f, 0.0f, 1.0f}, // Azul
  {0.5f, 0.15f, 1.0f}, // Violeta
  {1.0f, 1.0f, 1.0f}, // Cinza
  {10.0f, 10.0f, 10.0f} // Branco
};

#define RESISTOR_VALUES_COUNT 54
static const float RESISTOR_VALUES[] = {
  510.0f, 560.0f, 620.0f, 680.0f, 750.0f, 820.0f,
  910.0f, 1000.0f, 1100.0f, 1200.0f, 1300.0f, 1500.0f,
  1800.0f, 2000.0f, 2200.0f, 2400.0f, 2700.0f, 3000.0f,
  3300.0f, 3600.0f, 3900.0f, 4300.0f, 4700.0f, 5100.0f,
  5600.0f, 6200.0f, 6800.0f, 7500.0f, 8200.0f, 9100.0f,
  10000.0f, 11000.0f, 12000.0f, 13000.0f, 15000.0f,
  18000.0f, 20000.0f, 22000.0f, 24000.0f, 27000.0f,
  30000.0f, 33000.0f, 36000.0f, 39000.0f, 43000.0f,
  47000.0f, 51000.0f, 56000.0f, 62000.0f, 68000.0f,
  75000.0f, 82000.0f, 91000.0f, 100000.0f
};

Rgb corLigamento = {1.0f, 1.0f, 1.0f};

// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
  reset_usb_boot(0, 0);
}

int16_t buscarValorProx(float valor);
float valorComercial(float r);

int main()
{ 
  stdio_init_all();

  // Para ser utilizado o modo BOOTSEL com botão B
  gpio_init(botaoB);
  gpio_set_dir(botaoB, GPIO_IN);
  gpio_pull_up(botaoB);
  gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
  // Aqui termina o trecho para modo BOOTSEL com botão B

  gpio_init(Botao_A);
  gpio_set_dir(Botao_A, GPIO_IN);
  gpio_pull_up(Botao_A);

  // Inicializar matriz de LEDs
  inicializarMatriz();

  // Inicializar display SSD1306
  ssd1306_t ssd;
  ssd1306_i2c_init(&ssd);

  adc_init();
  adc_gpio_init(ADC_PIN); // GPIO 28 como entrada analógica

  // float tensao;
  char str_x[5]; // Buffer para armazenar a string do valor do ADC
  char str_y[8]; // Buffer para armazenar a string da resistência lida
  char str_z[8]; // Buffer para armazenar a string do valor comercial de resistência mais próximo

  bool cor = true;
  while (true)
  {
    adc_select_input(2); // Seleciona o ADC para eixo X. O pino 28 como entrada analógica

    float soma = 0.0f;
    for (int i = 0; i < 500; i++)
    {
      soma += adc_read();
      sleep_ms(1);
    }
    float media = soma / 500.0f;

    // Fórmula simplificada: R_x = R_conhecido * ADC_encontrado /(ADC_RESOLUTION - adc_encontrado)
    R_x = (R_conhecido * media) / (ADC_RESOLUTION - media);

    sprintf(str_x, "%1.0f", media); // Converte o inteiro em string

    // Índíces no vetor de cores para as cores da faixa do resistor
    uint8_t faixa_1 = 0;
    uint8_t faixa_2 = 0;
    uint8_t expMultiplicador = 0;

    // Obter o valor comercial mais próximo dado o valor lido
    float R_comercial = valorComercial(R_x);

    // Itera pelos digitos do valor de resistência para obter os dois primeiros digitos
    uint8_t digitoAtual = 0;
    uint8_t digitoAnterior = 0;
    int R_buffer = round(R_comercial);
    while (R_buffer > 0)
    {
      digitoAnterior = digitoAtual; // Garante que o digito anterior ao atual seja armazenado
      digitoAtual = R_buffer % 10; // Obtém último digito do número
      R_buffer /= 10; // "Remove" o último digito do buffer

      // Incrementa o expoente do multiplicador para cada 0
      if (digitoAtual == 0)
      {
        expMultiplicador++;
      }
    }

    // Associa os respectivos digitos à primeira e segunda faixa do resistor
    faixa_1 = digitoAtual;
    faixa_2 = digitoAnterior;

    // Desconsidera o 0 presente na segunda faixa do expoente do multiplicador
    if (faixa_2 == 0 && expMultiplicador > 0)
    {
      expMultiplicador--;
    }

    sprintf(str_y, "%1.0f", R_x);   // Converte o float em string
    sprintf(str_z, "%1.0f", R_comercial); 

    //  Atualiza o conteúdo do display
    ssd1306_fill(&ssd, !cor);                          // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);      // Desenha um retângulo
    ssd1306_line(&ssd, 3, 37, 123, 37, cor);           // Desenha uma linha
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[faixa_1], 8, 6);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[faixa_2], 8, 16);  // Desenha a cor da segunda faixa do resistor
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[expMultiplicador], 8, 26);  // Desenha a cor da terceira faixa do resistor
    ssd1306_draw_string(&ssd, "Comer", 77, 6);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, "cial:", 77, 16);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, str_z, 77, 26);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, "ADC", 13, 41);          // Desenha uma string
    ssd1306_draw_string(&ssd, "Resisten.", 50, 41);    // Desenha uma string
    ssd1306_line(&ssd, 44, 37, 44, 60, cor);           // Desenha uma linha vertical
    ssd1306_line(&ssd, 73, 37, 73, 3, cor);           // Desenha uma linha vertical
    ssd1306_draw_string(&ssd, str_x, 8, 52);           // Desenha uma string
    ssd1306_draw_string(&ssd, str_y, 59, 52);          // Desenha uma string
    ssd1306_send_data(&ssd);                           // Atualiza o display

    // Atualiza o conteúdo da matriz de LEDs
    limparMatriz();
    desenharColuna(1, 0, 3, RESISTOR_RGB_COLOR[faixa_1]);
    desenharColuna(2, 1, 1, corLigamento);
    desenharColuna(1, 2, 3, RESISTOR_RGB_COLOR[faixa_2]);
    desenharColuna(2, 3, 1, corLigamento);
    desenharColuna(1, 4, 3, RESISTOR_RGB_COLOR[expMultiplicador]);
    atualizarMatriz();

    sleep_ms(700);
  }
}

int16_t buscarValorProx(float valor)
{ 
  // Aplica uma busca binário pelo vetor
  int16_t inicio = 0;
  int16_t fim = RESISTOR_VALUES_COUNT - 1;
  int16_t media;
  while (inicio <= fim)
  {
    // Calcular indíce e obter valor médio desse intervalo
    media = inicio + (fim - inicio) / 2;

    if (valor == RESISTOR_VALUES[media])
    {
      return media;
    }
    else if (valor > RESISTOR_VALUES[media])
    {
      inicio = media + 1;
    }
    else if (valor < RESISTOR_VALUES[media])
    {
      fim = media - 1;
    }
  }

  // Caso a função saia do loop, não existe um valor exato na lista
  // O indíce início fica com o primeiro valor maior que o especificado, enquanto fim fica com o primeiro menor
  int16_t primeiroMaior = inicio;
  int16_t primeiroMenor = fim;

  // Tratar caso dos valores fora do limite do array
  if (primeiroMaior > RESISTOR_VALUES_COUNT - 1)
  {
    return RESISTOR_VALUES_COUNT - 1; // Considerar como maior valor possível
  }
  else if (primeiroMenor < 0)
  {
    return 0; // Considerar como menor valor possível
  }

  // Comparar a diferença dos valores para retornar o mais próximo
  if (abs(valor - RESISTOR_VALUES[primeiroMaior]) <= abs(valor - RESISTOR_VALUES[primeiroMenor]))
  {
    return primeiroMaior; // O maior valor é o mais próximo. Distâncias iguais se considera o maior valor
  }
  else
  {
    return primeiroMenor; // O menor valor é o mais próximo
  }
}

float valorComercial(float r)
{
  int16_t indice = buscarValorProx(r);
  return RESISTOR_VALUES[indice];
}
