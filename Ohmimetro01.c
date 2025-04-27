#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define ADC_PIN 28 // GPIO para o voltímetro
#define Botao_A 5  // GPIO para botão A

int R_conhecido = 10000;   // Resistor de 10k ohm
float R_x = 9200.0;           // Resistor desconhecido
float ADC_VREF = 3.31;     // Tensão de referência do ADC
int ADC_RESOLUTION = 4095; // Resolução do ADC (12 bits)

// Tabela de cores das faixas dos resistores, indexados de acordo com o digito que representam
static const char *RESISTOR_COLORS[] = {
  "Preto", "Marrom", "Vermelho",
  "Laranja", "Amarelo", "Verde",
  "Azul", "Violeta", "Cinza", "Branco"
};

#define RESISTOR_VALUES_COUNT 54
static const float RESISTOR_VALUES[] = {
  510.0, 560.0, 620.0, 680.0, 750.0, 820.0,
  910.0, 1000.0, 1100.0, 1200.0, 1300.0, 1500.0,
  1800.0, 2000.0, 2200.0, 2400.0, 2700.0, 3000.0,
  3300.0, 3600.0, 3900.0, 4300.0, 4700.0, 5100.0,
  5600.0, 6200.0, 6800.0, 7500.0, 8200.0, 9100.0,
  10000.0, 11000.0, 12000.0, 13000.0, 15000.0,
  18000.0, 20000.0, 22000.0, 24000.0, 27000.0,
  30000.0, 33000.0, 36000.0, 39000.0, 43000.0,
  47000.0, 51000.0, 56000.0, 62000.0, 68000.0,
  75000.0, 82000.0, 91000.0, 100000.0
};

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

  // I2C Initialisation. Using it at 400Khz.
  i2c_init(I2C_PORT, 400 * 1000);

  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_pull_up(I2C_SDA);                                        // Pull up the data line
  gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
  ssd1306_t ssd;                                                // Inicializa a estrutura do display
  ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
  ssd1306_config(&ssd);                                         // Configura o display
  ssd1306_send_data(&ssd);                                      // Envia os dados para o display

  // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);

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

    // cor = !cor;
    //  Atualiza o conteúdo do display
    ssd1306_fill(&ssd, !cor);                          // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);      // Desenha um retângulo
    ssd1306_line(&ssd, 3, 37, 123, 37, cor);           // Desenha uma linha
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[faixa_1], 8, 6);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[faixa_2], 8, 16);  // Desenha a cor da segunda faixa do resistor
    ssd1306_draw_string(&ssd, RESISTOR_COLORS[expMultiplicador], 8, 26);  // Desenha a cor da terceira faixa do resistor
    ssd1306_draw_string(&ssd, "Comerc", 75, 16);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, str_z, 75, 26);  // Desenha a cor da primeira faixa do resistor
    ssd1306_draw_string(&ssd, "ADC", 13, 41);          // Desenha uma string
    ssd1306_draw_string(&ssd, "Resisten.", 50, 41);    // Desenha uma string
    ssd1306_line(&ssd, 44, 37, 44, 60, cor);           // Desenha uma linha vertical
    ssd1306_draw_string(&ssd, str_x, 8, 52);           // Desenha uma string
    ssd1306_draw_string(&ssd, str_y, 59, 52);          // Desenha uma string
    ssd1306_send_data(&ssd);                           // Atualiza o display
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
  if (abs(valor - RESISTOR_VALUES[primeiroMaior]) < abs(valor - RESISTOR_VALUES[primeiroMenor]))
  {
    return primeiroMaior; // O maior valor é o mais próximo
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