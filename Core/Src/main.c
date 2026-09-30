/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "crc16.h"
#include "packet.h"
#include "parser.h"

#include "string.h"

#include "stdio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Ayristirici testlerinde bulunan paketleri biriktirir */
typedef struct
{
  uint8_t  adet;
  uint8_t  turler[4];
  uint16_t siralar[4];
  int16_t  son_x;
  int16_t  son_y;
} toplayici_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;

/* USER CODE BEGIN PV */
/* --- M6: okuma konumu ve tuketim --- */
parser_t rx_parser;          /* tek ayristirici; cagrilar arasi YASAMALI,
                                yoksa sarimda bolunen paket kaybolur */
uint16_t read_pos;           /* sadece rx_tuket yazar, DMA hic bakmaz */

/* Kesme set eder, while(1) temizler -> volatile ZORUNLU */
volatile uint8_t yeni_veri_var;

/* Uygulama seviyesi dogrulama. Bunlar main baglaminda yazilip okunuyor
   (while(1) -> rx_tuket -> parser_besle -> paket_geldi), kesme icinde
   degil. Bu yuzden volatile gerekmiyor. */
uint16_t paket_sayaci;
uint16_t son_sira;
int16_t  son_x;
int16_t  son_y;
uint16_t sira_atlama;        /* beklenen SEQUENCE gelmedigi olay sayisi */
uint16_t beklenen_sira;      /* bir sonraki paketten beklenen SEQUENCE */
uint8_t  sira_baslatildi;    /* ilk paket referans alindi mi */


/* --- M5: DMA alim altyapisi --- */
uint8_t RxData[256];                 /* DMA bu diziye yazar */
uint8_t TxData[PAKET_MAX_BOYUT];     /* gonderilecek paket burada kurulur */
uint8_t tx_boyu;                     /* son uretilen paketin boyutu */

/* Asagidakiler kesme icinde yazilip main/debugger tarafindan okunuyor.
   volatile ZORUNLU: derleyici bu degiskenleri register'da onbellekleyemez. */
volatile uint16_t count;             /* toplam RxEvent sayisi */
volatile uint16_t errorcount;
volatile uint16_t idle_sayaci;       /* IDLE kaynakli olay sayisi */
volatile uint16_t ht_sayaci;         /* yarim tampon */
volatile uint16_t tc_sayaci;         /* tam tampon */
volatile uint16_t son_size;          /* callback'in bildirdigi Size */
volatile uint32_t son_hata_kodu;     /* huart2.ErrorCode kopyasi */

/* --- M2 test tezgahi: sonuclar debugger'da Live Expressions ile okunur --- */
uint8_t  test_sonuc[32];   /* 0 = kosulmadi, 1 = PASS, 2 = FAIL */
uint8_t  test_sayisi;      /* kosulan test sayisi */
uint8_t  test_gecen;       /* PASS sayisi */
uint8_t  test_kalan;       /* FAIL sayisi */
uint16_t test_beklenen;    /* son basarisiz testin beklenen degeri */
uint16_t test_bulunan;     /* son basarisiz testin gercek degeri */


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void test_kaydet(uint16_t bulunan, uint16_t beklenen);
static void test_kaydet_bool(uint8_t dogru_mu);
static void crc16_testleri_kosur(void);
static void paket_testleri_kosur(void);
static void paket_toplayici(const paket_bilgi_t *paket, void *kullanici);
static void parser_testleri_kosur(void);
static void testleri_kosur(void);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint16_t sira;               /* T2 sarim testinin dongu sayaci */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  testleri_kosur();
  /* Ayristirici ve okuma konumu, alim baslamadan once temiz olmali */
  parser_sifirla(&rx_parser);
  read_pos = 0U;

  /* RX'i TX'ten ONCE baslat: loopback'te gonderilen baytlar hemen geri doner,
     alim hazir degilse kaybolurlar. */
  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, RxData, sizeof(RxData)) != HAL_OK)
  {
    Error_Handler();
  }
  //boyut_testi(tampon);

  /* Test paketi: X = +1000, Y = -500, SEQUENCE = 1
     Beklenen 13 bayt: AA 55 01 10 04 01 00 E8 03 0C FE 46 59 */
  tx_boyu = paket_joystick_olustur(TxData, (uint8_t)sizeof(TxData),
                                   1000, -500, 1U);

  if (tx_boyu == 0U)
  {
    Error_Handler();          /* paket uretilemedi: parametreler hatali */
  }

  /* Bloklayan gonderim: 13 bayt @115200 yaklasik 1,13 ms surer.
     Bu sirada DMA arka planda almaya devam eder. */
  if (HAL_UART_Transmit(&huart2, TxData, tx_boyu, 100U) != HAL_OK)
  {
    Error_Handler();
  }

  /* T1: tek paketi tuket */
  rx_tuket();

  /* T2 - sarim testi: SEQUENCE 2..40 ile 39 paket daha.
     Toplam 40 paket x 13 bayt = 520 bayt; tampon 256 bayt oldugundan
     sarim iki kez gerceklesir.

     Her gonderimden sonra tuketmek ZORUNLU. Tuketmezsen yaklasik 20.
     pakette DMA okunmamis veriyi ezmeye baslar ve konumlar esit gorunerek
     kaybi gizler. Bu davranisi gormek istersen asagidaki rx_tuket()
     cagrisini gecici olarak yorum satiri yap. */
  for (sira = 2U; sira <= 40U; sira++)
  {
    tx_boyu = paket_joystick_olustur(TxData, (uint8_t)sizeof(TxData),
                                     1000, -500, sira);
    if (tx_boyu == 0U)
    {
      Error_Handler();
    }

    if (HAL_UART_Transmit(&huart2, TxData, tx_boyu, 100U) != HAL_OK)
    {
      Error_Handler();
    }

    rx_tuket();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    if (yeni_veri_var != 0U)
	    {
	        yeni_veri_var = 0U;
	        rx_tuket();
	    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART2)
  {
    count++;
    son_size = Size;

    /* Tuketiciyi uyandir. Size'i tuketim icin KULLANMIYORUZ: mutlak konum
       bildirir, "kac yeni bayt" degil. Konumu rx_tuket kendisi hesaplar. */
    yeni_veri_var = 1U;

    /* Hangi olay tetikledi? M5'te sadece dogrulama icin ayiriyoruz. */
    switch (HAL_UARTEx_GetRxEventType(huart))
    {
      case HAL_UART_RXEVENT_IDLE:
        idle_sayaci++;
        break;

      case HAL_UART_RXEVENT_HT:
        ht_sayaci++;
        break;

      case HAL_UART_RXEVENT_TC:
        tc_sayaci++;
        break;

      default:
        break;
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    errorcount++;
    son_hata_kodu = huart->ErrorCode;   /* ORE / FE / NE / PE bit maskesi */
  }
}


/* Dogrulanmis bir paket cozuldugunde parser_besle tarafindan cagrilir.
   main baglaminda calisir: while(1) -> rx_tuket -> parser_besle -> buraya.
   paket->payload YALNIZCA bu cagri suresince gecerli; saklanacaksa kopyalanmali. */
static void paket_geldi(const paket_bilgi_t *paket, void *kullanici)
{
    (void)kullanici;

    paket_sayaci++;

    if (sira_baslatildi == 0U)
    {
        /* Ilk paket: gonderenin hangi degerden basladigini bilemeyiz.
           Karsilastirma yapmadan referans aliyoruz. */
        sira_baslatildi = 1U;
    }
    else if (paket->sira != beklenen_sira)
    {
        sira_atlama++;
    }
    else
    {
        /* Beklenen sira geldi */
    }

    son_sira = paket->sira;

    /* Beklentiyi GELEN degerden turetiyoruz. "beklenen_sira++" yazsaydik tek
       bir kayiptan sonra kalici olarak bir geri kalir ve sonraki her paketi
       kayip sayardik. Boylece kayip bir kez raporlanip senkron geri geliyor.
       (uint16_t) cast'i 65535 -> 0 sarimini kendiliginden hallediyor. */
    beklenen_sira = (uint16_t)(paket->sira + 1U);

    if ((paket->tur == PAKET_TUR_JOYSTICK) && (paket->uzunluk == 4U))
    {
        son_x = (int16_t)((uint16_t)paket->payload[0] |
                         ((uint16_t)paket->payload[1] << 8));
        son_y = (int16_t)((uint16_t)paket->payload[2] |
                         ((uint16_t)paket->payload[3] << 8));
    }
}

/* DMA tamponundaki yeni baytlari ayristiriciya verir.
   Isi: dairesel tamponu, parser_besle'nin bekledigi ARDISIK bayt
   araliklarina cevirmek. parser.c dairesel tampon nedir bilmez. */
static void rx_tuket(void)
{
    const uint16_t boyut = (uint16_t)sizeof(RxData);
    uint16_t write_pos;
    uint16_t adet;

    for (;;)
    {
        /* NDTR kalan transfer sayisini tutar ve her baytta azalir:
              yazilan bayt sayisi = boyut - NDTR
           Modulo tek bir uc durum icin gerekli: DMA 256. bayti yazip NDTR'yi
           henuz yeniden yuklemediginde 0 okunur, boyut - 0 = 256 cikar ve bu
           gecersiz bir indekstir. Modulo onu 0'a cevirir. */
        write_pos = (uint16_t)((boyut - (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_usart2_rx))
                               % boyut);

        if (write_pos == read_pos)
        {
            /* Yeni veri yok.
               DIKKAT: tam bir tur uzerine yazilmis olsa da konumlar boyle
               gorunur. Modulo aritmetigi tasmayi tespit edemez; koruma
               zamaninda tuketmektir. */
            break;
        }

        if (write_pos > read_pos)
        {
            /* Sarim yok: tek ardisik aralik */
            adet = (uint16_t)(write_pos - read_pos);
            parser_besle(&rx_parser, &RxData[read_pos], adet, paket_geldi, NULL);
            read_pos = write_pos;
        }
        else
        {
            /* Sarim var: once tampon SONUNA kadar besle. Kalani dongunun
               sonraki turu artik "sarim yok" durumu olarak halleder. */
            adet = (uint16_t)(boyut - read_pos);
            parser_besle(&rx_parser, &RxData[read_pos], adet, paket_geldi, NULL);
            read_pos = 0U;
        }
    }
}

static void test_kaydet(uint16_t bulunan, uint16_t beklenen)
{
  if (test_sayisi >= (sizeof(test_sonuc) / sizeof(test_sonuc[0])))
  {
    return;                       /* dizi doldu, sessizce birak */
  }

  if (bulunan == beklenen)
  {
    test_sonuc[test_sayisi] = 1U;
    test_gecen++;
  }
  else
  {
    test_sonuc[test_sayisi] = 2U;
    test_kalan++;
    test_beklenen = beklenen;     /* hatayi incelemek icin sakla */
    test_bulunan  = bulunan;
  }

  test_sayisi++;
}

static void test_kaydet_bool(uint8_t dogru_mu)
{
  test_kaydet((dogru_mu != 0U) ? 1U : 0U, 1U);
}

static void crc16_testleri_kosur(void)
{
  static const uint8_t v_ascii[9]    = {'1','2','3','4','5','6','7','8','9'};
  static const uint8_t v_sifir[1]    = {0x00};
  static const uint8_t v_ff[1]       = {0xFF};
  static const uint8_t v_aa55[2]     = {0xAA, 0x55};
  static const uint8_t v_joystick[9] = {0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE};

  test_kaydet(crc16_ccitt(v_ascii,    9U), 0x29B1U);   /* "123456789" */
  test_kaydet(crc16_ccitt(v_ascii,    0U), 0xFFFFU);   /* uzunluk 0   */
  test_kaydet(crc16_ccitt(v_sifir,    1U), 0xE1F0U);
  test_kaydet(crc16_ccitt(v_ff,       1U), 0xFF00U);
  test_kaydet(crc16_ccitt(v_aa55,     2U), 0xE5EAU);
  test_kaydet(crc16_ccitt(v_joystick, 9U), 0x5946U);   /* plandaki joystick paketi */
}




static void paket_testleri_kosur(void)
{
  /* Beklenen bayt dizileri: bagimsiz bir uygulamayla uretildi */
  static const uint8_t b_joystick[13] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t b_mod[10] =
    {0xAA,0x55,0x01,0x11,0x01,0x02,0x00,0x01,0x4E,0xED};
  static const uint8_t b_bos[9] =
    {0xAA,0x55,0x01,0x11,0x00,0x07,0x00,0xD9,0x4F};
  static const uint8_t b_aa55[11] =
    {0xAA,0x55,0x01,0x20,0x02,0x2C,0x01,0xAA,0x55,0x8D,0x8F};

  static const uint8_t p_mod[1]   = {0x01};
  static const uint8_t p_aa55[2]  = {0xAA, 0x55};
  static const uint8_t p_uzun[56] = {0};      /* PAKET_MAX_PAYLOAD + 1 */

  uint8_t cikti[PAKET_MAX_BOYUT];
  uint8_t n;

  /* T1: joystick paketi, donus degeri 13 olmali */
  n = paket_joystick_olustur(cikti, (uint8_t)sizeof(cikti), 1000, -500, 1U);
  test_kaydet(n, 13U);

  /* T2: joystick paketi, baytlar birebir esit olmali */
  test_kaydet_bool((uint8_t)(memcmp(cikti, b_joystick, sizeof(b_joystick)) == 0));

  /* T3: joystick modu acik, TYPE 0x11, payload 1 bayt */
  n = paket_olustur(cikti, (uint8_t)sizeof(cikti),
                    PAKET_TUR_JOYSTICK_MOD, 2U, p_mod, 1U);
  test_kaydet_bool((uint8_t)((n == 10U) &&
                   (memcmp(cikti, b_mod, sizeof(b_mod)) == 0)));

  /* T4: payload'siz paket - NULL + uzunluk 0 gecerli bir kullanimdir */
  n = paket_olustur(cikti, (uint8_t)sizeof(cikti),
                    PAKET_TUR_JOYSTICK_MOD, 7U, NULL, 0U);
  test_kaydet_bool((uint8_t)((n == 9U) &&
                   (memcmp(cikti, b_bos, sizeof(b_bos)) == 0)));

  /* T5: payload icinde AA 55 - baslangic isareti veri olarak da gecebilir */
  n = paket_olustur(cikti, (uint8_t)sizeof(cikti),
                    PAKET_TUR_CIKIS_AYARLA, 300U, p_aa55, 2U);
  test_kaydet_bool((uint8_t)((n == 11U) &&
                   (memcmp(cikti, b_aa55, sizeof(b_aa55)) == 0)));

  /* T6: payload cok uzun (56 > 55) -> reddedilmeli */
  test_kaydet(paket_olustur(cikti, (uint8_t)sizeof(cikti),
                            PAKET_TUR_CIKIS_AYARLA, 1U, p_uzun, 56U), 0U);

  /* T7: hedef cok kucuk (13 gerekli, 12 verildi) -> reddedilmeli */
  test_kaydet(paket_joystick_olustur(cikti, 12U, 1000, -500, 1U), 0U);

  /* T8: hedef NULL -> reddedilmeli */
  test_kaydet(paket_joystick_olustur(NULL, 13U, 1000, -500, 1U), 0U);

  /* T9: tam sinir (13) kabul edilmeli */
  test_kaydet(paket_joystick_olustur(cikti, 13U, 1000, -500, 1U), 13U);
}


/* Gecerli paket bulununca cagrilir. kullanici -> toplayici_t */
static void paket_toplayici(const paket_bilgi_t *paket, void *kullanici)
{
  toplayici_t *t = (toplayici_t *)kullanici;

  if (t->adet < 4U)
  {
    t->turler[t->adet]  = paket->tur;
    t->siralar[t->adet] = paket->sira;
  }

  if ((paket->tur == PAKET_TUR_JOYSTICK) && (paket->uzunluk == 4U))
  {
    t->son_x = (int16_t)((uint16_t)paket->payload[0] |
                        ((uint16_t)paket->payload[1] << 8));
    t->son_y = (int16_t)((uint16_t)paket->payload[2] |
                        ((uint16_t)paket->payload[3] << 8));
  }

  t->adet++;
}

static void parser_testleri_kosur(void)
{
  /* Akislar ve beklenen sayaclar protokol.py akis ile uretildi */
  static const uint8_t s_tek[13] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t s_ikili[23] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59,
     0xAA,0x55,0x01,0x11,0x01,0x02,0x00,0x01,0x4E,0xED};
  static const uint8_t s_cop[17] =
    {0x00,0xFF,0xAA,0x13,
     0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t s_aa55[11] =
    {0xAA,0x55,0x01,0x20,0x02,0x2C,0x01,0xAA,0x55,0x8D,0x8F};
  static const uint8_t s_bozuk[13] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x47,0x59};
  static const uint8_t s_kurtarma[24] =
    {0xAA,0x55,0x01,0x20,0x02,0x2C,0x01,0xAA,0x55,0x8D,0x8E,
     0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t s_uzunluk[20] =
    {0xAA,0x55,0x01,0x20,0x38,0x01,0x00,
     0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t s_surum[20] =
    {0xAA,0x55,0x02,0x10,0x04,0x01,0x00,
     0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};
  static const uint8_t s_yarim_mod[16] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,
     0xAA,0x55,0x01,0x11,0x01,0x02,0x00,0x01,0x4E,0xED};
  static const uint8_t s_yarim[8] =
    {0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8};

  parser_t    p;
  toplayici_t t;
  uint8_t     i;

  /* S1: tek tam paket -> 1 gecerli, X ve Y dogru */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_tek, (uint16_t)sizeof(s_tek), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000) &&
                             (t.son_y == -500) && (p.sayac_gecerli == 1U)));

  /* S2: ayni paket iki parcaya bolunmus (5 + 8) */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, &s_tek[0], 5U, paket_toplayici, &t);
  parser_besle(&p, &s_tek[5], 8U, paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000)));

  /* S3: bayt bayt beslenmis */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  for (i = 0U; i < (uint8_t)sizeof(s_tek); i++)
  {
    parser_besle(&p, &s_tek[i], 1U, paket_toplayici, &t);
  }
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_y == -500)));

  /* S4: iki paket tek beslemede birlesik -> 2 gecerli, sira korunmus */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_ikili, (uint16_t)sizeof(s_ikili), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 2U) &&
                             (t.turler[0] == PAKET_TUR_JOYSTICK) &&
                             (t.turler[1] == PAKET_TUR_JOYSTICK_MOD)));

  /* S5: basta cop bayt -> paket yine bulunur, 4 bayt atilir */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_cop, (uint16_t)sizeof(s_cop), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.sayac_atilan_bayt == 4U)));

  /* S6: payload icinde AA 55 -> normal veri sayilmali */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_aa55, (uint16_t)sizeof(s_aa55), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) &&
                             (t.turler[0] == PAKET_TUR_CIKIS_AYARLA) &&
                             (t.siralar[0] == 300U)));

  /* S7: CRC bozuk -> paket teslim edilmemeli */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_bozuk, (uint16_t)sizeof(s_bozuk), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 0U) && (p.sayac_crc_hata == 1U) &&
                             (p.sayac_atilan_bayt == 13U)));

  /* S8: bozuk paket + arkasindan gecerli paket -> KURTARMA SINAVI */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_kurtarma, (uint16_t)sizeof(s_kurtarma), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000) &&
                             (p.sayac_crc_hata == 1U)));

  /* S9: LENGTH 56 (sinir disi) + gecerli paket */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_uzunluk, (uint16_t)sizeof(s_uzunluk), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.sayac_uzunluk_hata == 1U) &&
                             (p.sayac_atilan_bayt == 7U)));

  /* S10: VERSION 2 + gecerli paket */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_surum, (uint16_t)sizeof(s_surum), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.sayac_surum_hata == 1U) &&
                             (p.sayac_atilan_bayt == 7U)));

  /* S11: yarim kalmis paket + tam mod paketi -> KURTARMA SINAVI */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_yarim_mod, (uint16_t)sizeof(s_yarim_mod), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) &&
                             (t.turler[0] == PAKET_TUR_JOYSTICK_MOD) &&
                             (p.sayac_crc_hata == 1U)));

  /* S12: yarim paket, devami hic gelmiyor -> teslim yok, cokme yok */
  parser_sifirla(&p);
  memset(&t, 0, sizeof(t));
  parser_besle(&p, s_yarim, (uint16_t)sizeof(s_yarim), paket_toplayici, &t);
  test_kaydet_bool((uint8_t)((t.adet == 0U) && (p.sayac_gecerli == 0U) &&
                             (p.sayac_crc_hata == 0U)));
}

static void testleri_kosur(void)
{
  test_sayisi = 0U;
  test_gecen  = 0U;
  test_kalan  = 0U;

  crc16_testleri_kosur();
  paket_testleri_kosur();
  parser_testleri_kosur();
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
