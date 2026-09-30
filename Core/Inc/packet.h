/*
 * packet.h
 */

#ifndef INC_PACKET_H_
#define INC_PACKET_H_

#include <stdint.h>

/* Paket yapisi: AA 55 | VERSION | TYPE | LENGTH | SEQ(2) | PAYLOAD | CRC16(2) */

#define PAKET_BAS1              0xAAU
#define PAKET_BAS2              0x55U
#define PAKET_SURUM             0x01U

#define PAKET_EK_BOYU           9U    /* baslik (7) + CRC (2), payload haric */
#define PAKET_MAX_PAYLOAD       55U
#define PAKET_MAX_BOYUT         64U

#define PAKET_TUR_JOYSTICK      0x10U
#define PAKET_TUR_JOYSTICK_MOD  0x11U
#define PAKET_TUR_CIKIS_AYARLA  0x20U
#define PAKET_TUR_YANIT         0x80U

/* Donus: yazilan toplam bayt sayisi, hata durumunda 0 */
uint8_t paket_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                      uint8_t tur, uint16_t sira,
                      const uint8_t *payload, uint8_t payload_uzunluk);

uint8_t paket_joystick_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                               int16_t x, int16_t y, uint16_t sira);

#endif /* INC_PACKET_H_ */
