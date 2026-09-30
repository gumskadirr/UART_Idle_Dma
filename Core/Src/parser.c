/*
 * parser.c
 *
 * Kayan aday penceresi yaklasimi:
 *   gelen her bayti tamponun sonuna ekle, sonra tamponun BASINDAN
 *   paket cozmeyi dene. Cozulemezse SADECE BIR BAYT at ve tekrar dene.
 *   Bir bayt atmak, arkadaki gercek paketin kaybolmamasini saglar.
 */
#include <stddef.h>
#include <string.h>
#include "parser.h"
#include "crc16.h"

/* Cozme denemesinin sonucu */
typedef enum
{
    COZ_EKSIK = 0,   /* karar icin yeterli bayt yok, bekle */
    COZ_TAMAM,       /* tamponun basinda gecerli paket var */
    COZ_HATALI       /* bu aday paket degil, bir bayt ilerle */
} coz_sonuc_t;


void parser_sifirla(parser_t *p)
{
    /* TODO: p NULL ise hicbir sey yapma.
             yazilan ve butun sayaclari sifirla.
             tampon icerigini temizlemek gerekmiyor - neden? */
	if (p == NULL){
		return;
	}
	else{
		p->yazilan =              0;
		p->sayac_atilan_bayt =    0;
		p->sayac_crc_hata =       0;
		p->sayac_gecerli =        0;
		p->sayac_surum_hata =     0;
		p->sayac_uzunluk_hata =   0;
	}
}


/* Tamponun basindan n bayt siler, kalan baytlari basa kaydirir. */
static void tampondan_sil(parser_t *p, uint8_t n)
{
    /* TODO:
       - n, yazilan'dan buyukse yazilan kadar sil
       - kalan bayt varsa &p->tampon[n] adresinden p->tampon adresine tasi
         Ipucu: memmove(hedef, kaynak, adet)
         memcpy DEGIL: kaynak ve hedef ust uste biniyor, memcpy'de
         bu durumun davranisi tanimsiz.
       - yazilan'i guncelle */
	if (p==NULL){
		return;
	}
	if (n >= p->yazilan){
		memmove(&p->tampon[0],&p->tampon[p->yazilan], n - p->yazilan);
		p->yazilan = 0;
	}
	else if(p->yazilan >= n){
		memmove(&p->tampon[0], &p->tampon[n],p->yazilan -n);
		p->yazilan = p->yazilan -n;
	}

}


/* Tamponun basindan bir paket cozmeyi dener.
   COZ_TAMAM donerse *paket_boyu toplam paket boyutunu tasir.
   Hatali adaylarda ilgili sayaci artirir.

   DIKKAT: yazilan == 0 iken COZ_HATALI DONME. Donersen parser_besle
   icindeki dongu hicbir bayt silemez ve sonsuza kadar doner. */
static coz_sonuc_t coz_dene(parser_t *p, uint8_t *paket_boyu)
{
	/* Kontrol sirasi (her adim oncekilerin gectigini varsayabilir):

	       1.  yazilan < 1                    -> COZ_EKSIK
	       2.  tampon[0] != PAKET_BAS1        -> COZ_HATALI
	       3.  yazilan < 2                    -> COZ_EKSIK
	       4.  tampon[1] != PAKET_BAS2        -> COZ_HATALI
	       5.  yazilan < 7 (baslik tam degil) -> COZ_EKSIK
	       6.  tampon[2] != PAKET_SURUM       -> sayac_surum_hata++,   COZ_HATALI
	       7.  tampon[4] > PAKET_MAX_PAYLOAD  -> sayac_uzunluk_hata++, COZ_HATALI
	       8.  toplam = PAKET_EK_BOYU + tampon[4]
	           yazilan < toplam               -> COZ_EKSIK
	       9.  gelen CRC = tampon[toplam-2] | (tampon[toplam-1] << 8)
	           hesap CRC = crc16_ccitt(&tampon[2], toplam - 4)
	           esit degilse                   -> sayac_crc_hata++,     COZ_HATALI
	       10. *paket_boyu = toplam           -> COZ_TAMAM

	       9. adimdaki "toplam - 4" nereden geliyor? Kagida bir paket ciz:
	       CRC indeks 2'den baslar ve CRC alaninin hemen oncesinde biter. */
    uint16_t toplam;
    uint16_t gelen_crc;
    uint16_t hesap_crc;

    if (p->yazilan < 1U){
        return COZ_EKSIK;
    }
    if (p->tampon[0] != PAKET_BAS1){
        return COZ_HATALI;
    }
    if (p->yazilan < 2U){
        return COZ_EKSIK;
    }
    if (p->tampon[1] != PAKET_BAS2){
        return COZ_HATALI;
    }
    if (p->yazilan < 7U){                    /* baslik henuz tam degil */
        return COZ_EKSIK;
    }
    if (p->tampon[2] != PAKET_SURUM){
        p->sayac_surum_hata++;
        return COZ_HATALI;
    }
    if (p->tampon[4] > PAKET_MAX_PAYLOAD){
        p->sayac_uzunluk_hata++;
        return COZ_HATALI;
    }

    /* LENGTH dogrulandi: artik toplam boyut guvenle hesaplanabilir */
    toplam = (uint16_t)PAKET_EK_BOYU + (uint16_t)p->tampon[4];

    if ((uint16_t)p->yazilan < toplam) {
        return COZ_EKSIK;
    }

    gelen_crc = (uint16_t)p->tampon[toplam - 2U] |
                ((uint16_t)p->tampon[toplam - 1U] << 8);
    hesap_crc = crc16_ccitt(&p->tampon[2], (uint16_t)(toplam - 4U));

    if (gelen_crc != hesap_crc){
        p->sayac_crc_hata++;
        return COZ_HATALI;
    }

    *paket_boyu = (uint8_t)toplam;
    return COZ_TAMAM;
}

void parser_besle(parser_t *p,
                  const uint8_t *veri, uint16_t uzunluk,
                  paket_geri_cagri_t geri_cagri, void *kullanici)
{
    uint16_t    i;
    uint8_t     paket_boyu;
    coz_sonuc_t sonuc;

    if ((p == NULL) || ((veri == NULL) && (uzunluk > 0U)))
    {
        return;
    }

    for (i = 0U; i < uzunluk; i++)
    {
        /* Guvenlik: tampon doluysa en eski bayti at. Dogru calisan bir
           coz_dene ile buraya normalde hic girilmez. */
        if (p->yazilan >= (uint8_t)sizeof(p->tampon))
        {
            tampondan_sil(p, 1U);
            p->sayac_atilan_bayt++;
        }

        p->tampon[p->yazilan] = veri[i];
        p->yazilan++;

        /* Yeni bayt geldi: cozebildigimiz kadar coz */
        for (;;)
        {
            paket_boyu = 0U;
            sonuc = coz_dene(p, &paket_boyu);

            if (sonuc == COZ_EKSIK)
            {
                break;                        /* daha fazla bayt lazim */
            }

            if (sonuc == COZ_TAMAM)
            {
                paket_bilgi_t bilgi;

                p->sayac_gecerli++;

                bilgi.tur     = p->tampon[3];
                bilgi.uzunluk = p->tampon[4];
                bilgi.sira    = (uint16_t)p->tampon[5] |
                                ((uint16_t)p->tampon[6] << 8);
                bilgi.payload = (bilgi.uzunluk > 0U) ? &p->tampon[7] : NULL;

                if (geri_cagri != NULL)
                {
                    geri_cagri(&bilgi, kullanici);
                }

                /* Silme geri cagridan SONRA: payload tampona isaret ediyor */
                tampondan_sil(p, paket_boyu);
            }
            else   /* COZ_HATALI */
            {
                tampondan_sil(p, 1U);         /* sadece bir bayt: yeniden tarama */
                p->sayac_atilan_bayt++;
            }
        }
    }
}
