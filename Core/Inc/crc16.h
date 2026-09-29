/*
 * crc16.h
 *
 *  Created on: Sep 29, 2026
 *      Author: kg083
 */

#ifndef INC_CRC16_H_
#define INC_CRC16_H_

#include <stdint.h>

/**
  * @brief  CRC-16/IBM-3740 (CRC-16/CCITT-FALSE) hesaplar.
  *         Polinom 0x1021, baslangic 0xFFFF, yansitma yok, cikis XOR yok.
  * @param  veri     Hesaba katilacak baytlar. Fonksiyon veriyi degistirmez.
  * @param  uzunluk  veri icindeki bayt sayisi. 0 olabilir.
  * @retval Hesaplanan CRC. uzunluk 0 veya veri NULL ise 0xFFFF doner.
  */
uint16_t crc16_ccitt(const uint8_t *veri, uint16_t uzunluk);

#endif /* INC_CRC16_H_ */
