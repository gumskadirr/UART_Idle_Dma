/*
 * parser.h
 *
 * UART bayt akisindan dogrulanmis paket cikarma.
 * Bu modul HAL, DMA ve FreeRTOS'tan bagimsizdir: bayt alir, paket verir.
 */

#ifndef INC_PARSER_H_
#define INC_PARSER_H_

#include <stdint.h>
#include "packet.h"

/* Cozulmus paketin bilgileri.
   DIKKAT: payload isaretcisi yalnizca geri cagri suresince gecerlidir.
   Veriyi saklayacaksan kopyala; ayristirici tamponu sonra degisir. */
typedef struct
{
    uint8_t        tur;
    uint16_t       sira;
    uint8_t        uzunluk;
    const uint8_t *payload;    /* uzunluk 0 ise NULL */
} paket_bilgi_t;

/* Gecerli paket bulununca cagrilir.
   kullanici: cagirana ait serbest isaretci, ayristirici icine bakmaz. */
typedef void (*paket_geri_cagri_t)(const paket_bilgi_t *paket, void *kullanici);

typedef struct
{
    uint8_t  tampon[PAKET_MAX_BOYUT];   /* aday paket penceresi */
    uint8_t  yazilan;                   /* tamponda kac bayt var */

    uint16_t sayac_gecerli;
    uint16_t sayac_crc_hata;
    uint16_t sayac_uzunluk_hata;
    uint16_t sayac_surum_hata;
    uint16_t sayac_atilan_bayt;
} parser_t;

void parser_sifirla(parser_t *p);

void parser_besle(parser_t *p,
                  const uint8_t *veri, uint16_t uzunluk,
                  paket_geri_cagri_t geri_cagri, void *kullanici);

#endif /* INC_PARSER_H_ */
