/*
 * uart_rx.h
 *
 * UART alim altyapisi: circular DMA tamponu, okuma konumu takibi ve
 * ayristiriciya besleme. HAL callback'lerini bu modul sahiplenir.
 *
 * Kullanim:
 *   uart_rx_baslat(&huart2);          bir kez, alimi baslatir
 *   uart_rx_isle();                   dongude, yeni veri varsa tuketir
 */

#ifndef INC_UART_RX_H_
#define INC_UART_RX_H_

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "parser.h"

#define UART_RX_TAMPON_BOYU   256U

/* Alim olaylari. Kesme icinde yazilip main baglaminda okundugu icin
   volatile: derleyici bu degerleri register'da onbellekleyemez. */
typedef struct
{
    volatile uint16_t rx_olay;         /* toplam RxEvent sayisi */
    volatile uint16_t idle_olay;       /* IDLE kaynakli */
    volatile uint16_t ht_olay;         /* yarim tampon */
    volatile uint16_t tc_olay;         /* tam tampon */
    volatile uint16_t son_size;        /* callback'in bildirdigi Size (mutlak konum) */
    volatile uint16_t hata_olay;       /* UART hata callback sayisi */
    volatile uint32_t son_hata_kodu;   /* ORE/FE/NE/PE bit maskesi */
} uart_rx_istatistik_t;

/* Cozulmus paketlerden turetilen durum. Yalnizca main baglaminda
   guncellenir (uart_rx_tuket -> parser_besle -> paket_geldi), bu yuzden
   volatile gerekmez. */
typedef struct
{
    uint16_t paket_sayaci;
    uint16_t son_sira;
    uint16_t beklenen_sira;
    uint16_t sira_atlama;         /* beklenen SEQUENCE gelmedigi olay sayisi */
    uint8_t  sira_baslatildi;     /* ilk paket referans alindi mi */
    int16_t  son_x;
    int16_t  son_y;
} uart_rx_durum_t;

extern uart_rx_istatistik_t uart_rx_ist;
extern uart_rx_durum_t      uart_rx_durum;

/* Alimi baslatir: ayristiriciyi ve okuma konumunu sifirlar, circular DMA'yi
   IDLE olaylariyla kurar. Loopback testinde gonderimden ONCE cagrilmali. */
HAL_StatusTypeDef uart_rx_baslat(UART_HandleTypeDef *huart);

/* Yeni veri bildirimi varsa tuketir. Dongude cagrilir. */
void uart_rx_isle(void);

/* Kosulsuz tuketim. Bildirimi beklemeden tamponu bosaltir; loopback
   testinde gonderim dongusu icinden cagrilir. */
void uart_rx_tuket(void);

/* Ayristirici sayaclarina salt okunur erisim (hata ayiklama icin). */
const parser_t *uart_rx_parser(void);

#endif /* INC_UART_RX_H_ */
