/*
 * packet.c
 *
 * Protokol paketi olusturma. Bayt duzeni:
 *   AA 55 | VERSION | TYPE | LENGTH | SEQUENCE(2) | PAYLOAD | CRC16(2)
 * LENGTH yalnizca payload boyutudur, toplam boyut 9 + LENGTH olur.
 * Cok baytli alanlar little-endian, CRC VERSION'dan payload sonuna kadar.
 */
#include <stddef.h>
#include "packet.h"
#include "crc16.h"

/* 16 bit degeri little-endian yazar (dusuk bayt once).
   Donus: yazilan bayt sayisi, her zaman 2. */
static uint8_t u16_yaz(uint8_t *hedef, uint16_t deger)
{
    hedef[0] = (uint8_t)(deger & 0xFFU);
    hedef[1] = (uint8_t)((deger >> 8) & 0xFFU);
    return 2U;
}

uint8_t paket_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                      uint8_t tur, uint16_t sira,
                      const uint8_t *payload, uint8_t payload_uzunluk)
{
    uint8_t  i = 0U;     /* yazma konumu: siradaki bos indeks */
    uint8_t  j;          /* payload kopyalama sayaci */
    uint16_t crc;

    /* --- 1) Kontroller: tek bayt yazmadan once hepsi --- */
    if (hedef == NULL){
        return 0U;
    }

    if ((payload == NULL) && (payload_uzunluk > 0U)){
        return 0U;
    }

    if (payload_uzunluk > PAKET_MAX_PAYLOAD){
        return 0U;
    }

    /* Toplami 16 bitte hesapla: 8 bit aritmetikte tasma riski olmasin */
    if ((uint16_t)hedef_boyut < ((uint16_t)PAKET_EK_BOYU + (uint16_t)payload_uzunluk)){
        return 0U;
    }

    /* --- 2) Baslik --- */
    hedef[i++] = PAKET_BAS1;
    hedef[i++] = PAKET_BAS2;
    hedef[i++] = PAKET_SURUM;
    hedef[i++] = tur;
    hedef[i++] = payload_uzunluk;      /* LENGTH: sadece payload boyutu */

    i += u16_yaz(&hedef[i], sira);     /* SEQUENCE, iki bayt */

    /* --- 3) Payload --- */
    for (j = 0U; j < payload_uzunluk; j++)
    {
        hedef[i++] = payload[j];
    }

    /* --- 4) CRC --- */
    /* Bu noktada i = 7 + payload_uzunluk.
       CRC indeks 2'den indeks i-1'e kadar, yani (i - 2) bayt uzerinden. */
    crc = crc16_ccitt(&hedef[2], (uint16_t)(i - 2U));

    i += u16_yaz(&hedef[i], crc);

    return i;   /* toplam yazilan bayt sayisi */
}

uint8_t paket_joystick_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                               int16_t x, int16_t y, uint16_t sira)
{
    uint8_t gecici[4];

    /* Isaretli degerleri once uint16_t'ye cevir: bit islemleri
       isaretsiz tiplerde yapilir, isaret yorumu karsi tarafa kalir. */
    (void)u16_yaz(&gecici[0], (uint16_t)x);
    (void)u16_yaz(&gecici[2], (uint16_t)y);

    return paket_olustur(hedef, hedef_boyut,
                         PAKET_TUR_JOYSTICK, sira,
                         gecici, 4U);
}
