/*
 * tests.h
 *
 * Gelistirme sirasinda kullanilan dogrulama kosucularI. Uretim kodu bunlara
 * bagimli degildir; cagrilari main.c'den kaldirmak yeterlidir.
 *
 * Sonuclar debugger'da Live Expressions ile okunur:
 *   test_gecen, test_kalan, test_sonuc[]
 * Bir test basarisizsa test_sonuc dizisinde 2 olan indekse bakilir
 *   0-5   : CRC testleri
 *   6-14  : cerceve olusturucu testleri
 *   15-28 : ayristirici senaryolari (S1..S13; S13 iki kontrol)
 */

#ifndef INC_TESTS_H_
#define INC_TESTS_H_

#include <stdint.h>
#include "stm32f4xx_hal.h"

#define TEST_SONUC_ADET   32U

extern uint8_t  test_sonuc[TEST_SONUC_ADET];   /* 0 kosulmadi, 1 PASS, 2 FAIL */
extern uint8_t  test_sayisi;
extern uint8_t  test_gecen;
extern uint8_t  test_kalan;
extern uint16_t test_beklenen;                 /* son basarisiz testin beklentisi */
extern uint16_t test_bulunan;                  /* son basarisiz testin sonucu */

/* 29 birim testi: CRC, cerceve olusturucu, ayristirici. Donanim gerektirmez. */
void birim_testleri_kosur(void);

/* Loopback testi: PA2-PA3 arasi jumper kablo gerektirir.
   Tuketim uart_rx_service() ile yapilir; bildirim zinciri de sinanir.
   40 joystick paketi gonderir (520 bayt), 256 baytlik tamponda sarim iki kez
   gerceklesir. Sonuclar uart_rx_state uzerinden okunur.
   ONKOSUL: uart_rx_start() cagrilmis olmali. */
void loopback_testi_kosur(UART_HandleTypeDef *huart);

#endif /* INC_TESTS_H_ */
