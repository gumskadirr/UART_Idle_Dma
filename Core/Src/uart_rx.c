/*
 * uart_rx.c
 *
 * Dairesel DMA tamponunu, ayristiricinin bekledigi ARDISIK bayt
 * araliklarina cevirir. parser.c dairesel tampon nedir bilmez.
 *
 * Sahiplik:
 *   s_tampon   -> DMA yazar, bu modul okur
 *   s_read_pos -> yalnizca uart_rx_tuket yazar, DMA hic bakmaz
 * Iki taraf farkli degiskenlere sahip oldugu icin kilit gerekmez.
 */
#include <stddef.h>
#include "uart_rx.h"
#include "packet.h"

/* --- Modul ici durum --- */
static UART_HandleTypeDef *s_huart;                    /* uart_rx_baslat baglar */
static uint8_t             s_tampon[UART_RX_TAMPON_BOYU];  /* DMA hedefi */
static uint16_t            s_read_pos;                 /* okunmamis ilk bayt */
static parser_t            s_parser;                   /* yarim paket durumu */
static volatile uint8_t    s_yeni_veri;                /* kesme set eder */

uart_rx_istatistik_t uart_rx_ist;
uart_rx_durum_t      uart_rx_durum;

static void paket_geldi(const paket_bilgi_t *paket, void *kullanici);


HAL_StatusTypeDef uart_rx_baslat(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->hdmarx == NULL))
    {
        return HAL_ERROR;
    }

    s_huart     = huart;
    s_read_pos  = 0U;
    s_yeni_veri = 0U;

    parser_sifirla(&s_parser);

    return HAL_UARTEx_ReceiveToIdle_DMA(huart, s_tampon,
                                        (uint16_t)sizeof(s_tampon));
}


void uart_rx_isle(void)
{
    if (s_yeni_veri != 0U)
    {
        s_yeni_veri = 0U;
        uart_rx_tuket();
    }
}


void uart_rx_tuket(void)
{
    const uint16_t boyut = (uint16_t)sizeof(s_tampon);
    uint16_t write_pos;
    uint16_t adet;

    if (s_huart == NULL)
    {
        return;
    }

    for (;;)
    {
        /* NDTR kalan transfer sayisini tutar ve her baytta azalir:
              yazilan bayt sayisi = boyut - NDTR
           Modulo tek bir uc durum icin gerekli: DMA 256. bayti yazip NDTR'yi
           henuz yeniden yuklemediginde 0 okunur, boyut - 0 = 256 cikar ve bu
           gecersiz bir indekstir. Modulo onu 0'a cevirir. */
        write_pos = (uint16_t)((boyut -
                     (uint16_t)__HAL_DMA_GET_COUNTER(s_huart->hdmarx)) % boyut);

        if (write_pos == s_read_pos)
        {
            /* Yeni veri yok.
               DIKKAT: tam bir tur uzerine yazilmis olsa da konumlar boyle
               gorunur. Modulo aritmetigi tasmayi tespit edemez; koruma
               zamaninda tuketmektir (256 bayt / 11520 bayt/s ~ 22 ms). */
            break;
        }

        if (write_pos > s_read_pos)
        {
            /* Sarim yok: tek ardisik aralik */
            adet = (uint16_t)(write_pos - s_read_pos);
            parser_besle(&s_parser, &s_tampon[s_read_pos], adet,
                         paket_geldi, NULL);
            s_read_pos = write_pos;
        }
        else
        {
            /* Sarim var: once tampon SONUNA kadar besle. Kalani dongunun
               sonraki turu artik "sarim yok" durumu olarak halleder. */
            adet = (uint16_t)(boyut - s_read_pos);
            parser_besle(&s_parser, &s_tampon[s_read_pos], adet,
                         paket_geldi, NULL);
            s_read_pos = 0U;
        }
    }
}


const parser_t *uart_rx_parser(void)
{
    return &s_parser;
}


/* Dogrulanmis bir paket cozuldugunde parser_besle tarafindan cagrilir.
   main baglaminda calisir: uart_rx_isle -> uart_rx_tuket -> parser_besle.
   paket->payload YALNIZCA bu cagri suresince gecerli; saklanacaksa
   kopyalanmali. */
static void paket_geldi(const paket_bilgi_t *paket, void *kullanici)
{
    (void)kullanici;

    uart_rx_durum.paket_sayaci++;

    if (uart_rx_durum.sira_baslatildi == 0U)
    {
        /* Ilk paket: gonderenin hangi degerden basladigini bilemeyiz.
           Karsilastirma yapmadan referans aliyoruz. */
        uart_rx_durum.sira_baslatildi = 1U;
    }
    else if (paket->sira != uart_rx_durum.beklenen_sira)
    {
        uart_rx_durum.sira_atlama++;
    }
    else
    {
        /* Beklenen sira geldi */
    }

    uart_rx_durum.son_sira = paket->sira;

    /* Beklentiyi GELEN degerden turetiyoruz. "beklenen_sira++" yazsaydik tek
       bir kayiptan sonra kalici olarak bir geri kalir ve sonraki her paketi
       kayip sayardik. (uint16_t) cast'i 65535 -> 0 sarimini halleder. */
    uart_rx_durum.beklenen_sira = (uint16_t)(paket->sira + 1U);

    if ((paket->tur == PAKET_TUR_JOYSTICK) && (paket->uzunluk == 4U))
    {
        uart_rx_durum.son_x = (int16_t)((uint16_t)paket->payload[0] |
                                       ((uint16_t)paket->payload[1] << 8));
        uart_rx_durum.son_y = (int16_t)((uint16_t)paket->payload[2] |
                                       ((uint16_t)paket->payload[3] << 8));
    }
}


/* --- HAL callback'leri ---
   Bu iki fonksiyon HAL'de __weak tanimli ve butun UART'lar icin ortaktir;
   bu yuzden hangi UART oldugu kontrol edilir. */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if ((s_huart != NULL) && (huart->Instance == s_huart->Instance))
    {
        uart_rx_ist.rx_olay++;
        uart_rx_ist.son_size = Size;

        /* Size mutlak konum bildirir, "kac yeni bayt" degil. Tuketimde
           KULLANILMAZ; konumu uart_rx_tuket kendisi NDTR'den hesaplar. */
        s_yeni_veri = 1U;

        switch (HAL_UARTEx_GetRxEventType(huart))
        {
            case HAL_UART_RXEVENT_IDLE:
                uart_rx_ist.idle_olay++;
                break;

            case HAL_UART_RXEVENT_HT:
                uart_rx_ist.ht_olay++;
                break;

            case HAL_UART_RXEVENT_TC:
                uart_rx_ist.tc_olay++;
                break;

            default:
                break;
        }
    }
}


void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((s_huart != NULL) && (huart->Instance == s_huart->Instance))
    {
        uart_rx_ist.hata_olay++;
        uart_rx_ist.son_hata_kodu = huart->ErrorCode;
    }
}
