/*
 * tests.c
 *
 * Dogrulama kosucularI. main.c'den ayrilmistir: uretim kodu bu dosyaya
 * bagimli degildir, cagrilari kaldirmak yeterlidir.
 */
#include <stddef.h>
#include <string.h>

#include "tests.h"
#include "crc16.h"
#include "frame.h"
#include "parser.h"
#include "uart_rx.h"

/* Sonuclar debugger'da okunur. Dis baglantili (static degil) olmalari
   derleyicinin bunlari atmasini engeller. */
uint8_t  test_sonuc[TEST_SONUC_ADET];
uint8_t  test_sayisi;
uint8_t  test_gecen;
uint8_t  test_kalan;
uint16_t test_beklenen;
uint16_t test_bulunan;

/* Ayristirici testlerinde bulunan cerceveleri biriktirir */
typedef struct
{
  uint8_t  adet;
  uint8_t  turler[4];
  uint16_t siralar[4];
  int16_t  son_x;
  int16_t  son_y;
} collector_t;

static void test_kaydet(uint16_t bulunan, uint16_t beklenen);
static void test_kaydet_bool(uint8_t dogru_mu);
static void crc16_testleri_kosur(void);
static void paket_testleri_kosur(void);
static void frame_collector(const frame_info_t *info, void *user_data);
static void frame_parser_testleri_kosur(void);


static void test_kaydet(uint16_t bulunan, uint16_t beklenen)
{
  if (test_sayisi >= (TEST_SONUC_ADET))
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
  static const uint8_t p_uzun[56] = {0};      /* FRAME_MAX_PAYLOAD + 1 */

  uint8_t cikti[FRAME_MAX_SIZE];
  uint8_t n;

  /* T1: joystick paketi, donus degeri 13 olmali */
  n = frame_build_joystick(cikti, (uint8_t)sizeof(cikti), 1000, -500, 1U);
  test_kaydet(n, 13U);

  /* T2: joystick paketi, baytlar birebir esit olmali */
  test_kaydet_bool((uint8_t)(memcmp(cikti, b_joystick, sizeof(b_joystick)) == 0));

  /* T3: joystick modu acik, TYPE 0x11, payload 1 bayt */
  n = frame_build(cikti, (uint8_t)sizeof(cikti),
                    FRAME_TYPE_JOYSTICK_MODE, 2U, p_mod, 1U);
  test_kaydet_bool((uint8_t)((n == 10U) &&
                   (memcmp(cikti, b_mod, sizeof(b_mod)) == 0)));

  /* T4: payload'siz paket - NULL + uzunluk 0 gecerli bir kullanimdir */
  n = frame_build(cikti, (uint8_t)sizeof(cikti),
                    FRAME_TYPE_JOYSTICK_MODE, 7U, NULL, 0U);
  test_kaydet_bool((uint8_t)((n == 9U) &&
                   (memcmp(cikti, b_bos, sizeof(b_bos)) == 0)));

  /* T5: payload icinde AA 55 - baslangic isareti veri olarak da gecebilir */
  n = frame_build(cikti, (uint8_t)sizeof(cikti),
                    FRAME_TYPE_SET_OUTPUT, 300U, p_aa55, 2U);
  test_kaydet_bool((uint8_t)((n == 11U) &&
                   (memcmp(cikti, b_aa55, sizeof(b_aa55)) == 0)));

  /* T6: payload cok uzun (56 > 55) -> reddedilmeli */
  test_kaydet(frame_build(cikti, (uint8_t)sizeof(cikti),
                            FRAME_TYPE_SET_OUTPUT, 1U, p_uzun, 56U), 0U);

  /* T7: hedef cok kucuk (13 gerekli, 12 verildi) -> reddedilmeli */
  test_kaydet(frame_build_joystick(cikti, 12U, 1000, -500, 1U), 0U);

  /* T8: hedef NULL -> reddedilmeli */
  test_kaydet(frame_build_joystick(NULL, 13U, 1000, -500, 1U), 0U);

  /* T9: tam sinir (13) kabul edilmeli */
  test_kaydet(frame_build_joystick(cikti, 13U, 1000, -500, 1U), 13U);
}


/* Gecerli paket bulununca cagrilir. kullanici -> collector_t */
static void frame_collector(const frame_info_t *info, void *user_data)
{
  collector_t *t = (collector_t *)user_data;

  if (t->adet < 4U)
  {
    t->turler[t->adet]  = info->type;
    t->siralar[t->adet] = info->seq;
  }

  if ((info->type == FRAME_TYPE_JOYSTICK) && (info->payload_len == 4U))
  {
    t->son_x = (int16_t)((uint16_t)info->payload[0] |
                        ((uint16_t)info->payload[1] << 8));
    t->son_y = (int16_t)((uint16_t)info->payload[2] |
                        ((uint16_t)info->payload[3] << 8));
  }

  t->adet++;
}


static void frame_parser_testleri_kosur(void)
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

  /* S13: LENGTH=55 diyen bir baslik (64 bayt bekler) + arkasinda gecerli
     cerceve. Toplam 20 bayt geldigi icin aday asla tamamlanmaz ve arkadaki
     gecerli cerceve de bekler. Zaman asimi bu tikanikligi acar. */
  static const uint8_t s_tikanik[20] =
    {0xAA,0x55,0x01,0x20,0x37,0x01,0x00,
     0xAA,0x55,0x01,0x10,0x04,0x01,0x00,0xE8,0x03,0x0C,0xFE,0x46,0x59};

  frame_parser_t    p;
  collector_t t;
  uint8_t     i;

  /* S1: tek tam paket -> 1 gecerli, X ve Y dogru */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_tek, (uint16_t)sizeof(s_tek), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000) &&
                             (t.son_y == -500) && (p.frames_ok == 1U)));

  /* S2: ayni paket iki parcaya bolunmus (5 + 8) */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, &s_tek[0], 5U, frame_collector, &t);
  frame_parser_feed(&p, &s_tek[5], 8U, frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000)));

  /* S3: bayt bayt beslenmis */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  for (i = 0U; i < (uint8_t)sizeof(s_tek); i++)
  {
    frame_parser_feed(&p, &s_tek[i], 1U, frame_collector, &t);
  }
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_y == -500)));

  /* S4: iki paket tek beslemede birlesik -> 2 gecerli, sira korunmus */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_ikili, (uint16_t)sizeof(s_ikili), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 2U) &&
                             (t.turler[0] == FRAME_TYPE_JOYSTICK) &&
                             (t.turler[1] == FRAME_TYPE_JOYSTICK_MODE)));

  /* S5: basta cop bayt -> paket yine bulunur, 4 bayt atilir */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_cop, (uint16_t)sizeof(s_cop), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.bytes_dropped == 4U)));

  /* S6: payload icinde AA 55 -> normal veri sayilmali */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_aa55, (uint16_t)sizeof(s_aa55), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) &&
                             (t.turler[0] == FRAME_TYPE_SET_OUTPUT) &&
                             (t.siralar[0] == 300U)));

  /* S7: CRC bozuk -> paket teslim edilmemeli */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_bozuk, (uint16_t)sizeof(s_bozuk), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 0U) && (p.err_crc == 1U) &&
                             (p.bytes_dropped == 13U)));

  /* S8: bozuk paket + arkasindan gecerli paket -> KURTARMA SINAVI */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_kurtarma, (uint16_t)sizeof(s_kurtarma), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (t.son_x == 1000) &&
                             (p.err_crc == 1U)));

  /* S9: LENGTH 56 (sinir disi) + gecerli paket */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_uzunluk, (uint16_t)sizeof(s_uzunluk), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.err_len == 1U) &&
                             (p.bytes_dropped == 7U)));

  /* S10: VERSION 2 + gecerli paket */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_surum, (uint16_t)sizeof(s_surum), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) && (p.err_version == 1U) &&
                             (p.bytes_dropped == 7U)));

  /* S11: yarim kalmis paket + tam mod paketi -> KURTARMA SINAVI */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_yarim_mod, (uint16_t)sizeof(s_yarim_mod), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) &&
                             (t.turler[0] == FRAME_TYPE_JOYSTICK_MODE) &&
                             (p.err_crc == 1U)));

  /* S12: yarim paket, devami hic gelmiyor -> teslim yok, cokme yok */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_yarim, (uint16_t)sizeof(s_yarim), frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 0U) && (p.frames_ok == 0U) &&
                             (p.err_crc == 0U)));

  /* S13: bozuk LENGTH tikanikligi ve zaman asimiyla acilmasi.
     Once beslemede hicbir cerceve teslim edilmemeli (aday 64 bayt bekliyor,
     elde 20 var). Sonra zaman asimi bir bayt atar, yeniden tarama arkadaki
     gecerli cerceveyi bulur. */
  frame_parser_init(&p);
  memset(&t, 0, sizeof(t));
  frame_parser_feed(&p, s_tikanik, (uint16_t)sizeof(s_tikanik),
                    frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 0U) && (p.len == 20U)));

  frame_parser_timeout(&p, frame_collector, &t);
  test_kaydet_bool((uint8_t)((t.adet == 1U) &&
                             (t.turler[0] == FRAME_TYPE_JOYSTICK) &&
                             (t.siralar[0] == 1U) &&
                             (t.son_x == 1000) && (t.son_y == -500) &&
                             (p.timeouts == 1U) &&
                             (p.bytes_dropped == 7U) &&
                             (p.len == 0U)));
}


void birim_testleri_kosur(void)
{
  test_sayisi = 0U;
  test_gecen  = 0U;
  test_kalan  = 0U;

  crc16_testleri_kosur();
  paket_testleri_kosur();
  frame_parser_testleri_kosur();
}


void loopback_testi_kosur(UART_HandleTypeDef *huart)
{
  uint8_t  tx[FRAME_MAX_SIZE];
  uint8_t  n;
  uint16_t sira;

  if (huart == NULL)
  {
    return;
  }

  /* T1: tek paket. X = +1000, Y = -500, SEQUENCE = 1
     Beklenen 13 bayt: AA 55 01 10 04 01 00 E8 03 0C FE 46 59 */
  n = frame_build_joystick(tx, (uint8_t)sizeof(tx), 1000, -500, 1U);
  if (n == 0U)
  {
    return;
  }

  if (HAL_UART_Transmit(huart, tx, n, 100U) != HAL_OK)
  {
    return;
  }

  /* uart_rx_drain() DEGIL uart_rx_service(): boylece
     callback -> s_rx_pending -> service zinciri de sinanir. IDLE son bayttan
     ~87 us sonra tetiklendigi icin 1 ms beklemek yeterli. */
  HAL_Delay(1U);
  uart_rx_service();

  /* T2 - sarim testi: SEQUENCE 2..40 ile 39 paket daha.
     Toplam 40 x 13 = 520 bayt; tampon 256 bayt oldugundan sarim iki kez
     gerceklesir.

     Her gonderimden sonra tuketmek ZORUNLU. Tuketmezsen yaklasik 20.
     cercevede DMA okunmamis veriyi ezmeye baslar ve konumlar esit gorunerek
     kaybi gizler. Bu davranisi gormek icin asagidaki uart_rx_service()
     cagrisini gecici olarak yorum satiri yapabilirsin.

     Tuketim uart_rx_service() ile yapiliyor: bildirim yolu (callback ->
     s_rx_pending -> service) de bu testin kapsaminda. */
  for (sira = 2U; sira <= 40U; sira++)
  {
    n = frame_build_joystick(tx, (uint8_t)sizeof(tx), 1000, -500, sira);
    if (n == 0U)
    {
      return;
    }

    if (HAL_UART_Transmit(huart, tx, n, 100U) != HAL_OK)
    {
      return;
    }

    HAL_Delay(1U);
    uart_rx_service();
  }
}
