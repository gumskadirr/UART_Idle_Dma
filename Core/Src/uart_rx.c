/*
 * uart_rx.c
 *
 * Dairesel DMA tamponunu, ayristiricinin bekledigi ARDISIK bayt
 * araliklarina cevirir. parser.c dairesel tampon nedir bilmez.
 *
 * Sahiplik:
 *   s_dma_buf   -> DMA yazar, bu modul okur
 *   s_read_pos  -> yalnizca uart_rx_drain yazar, DMA hic bakmaz
 * Iki taraf farkli degiskenlere sahip oldugu icin kilit gerekmez.
 *
 * Not: klasik ring buffer literaturu head/tail der, ama bu iki terimin
 * anlami kaynaga gore ters cevrilir (Linux kfifo bu yuzden in/out kullanir).
 * read_pos/write_pos belirsizlik birakmadigi icin tercih edildi. Ayrica
 * write_pos burada saklanan bir durum degil, NDTR'den turetilen anlik deger.
 */
#include <stddef.h>
#include "uart_rx.h"
#include "frame.h"

/* --- Modul ici durum --- */
static UART_HandleTypeDef *s_huart;                  /* uart_rx_start baglar */
static uint8_t             s_dma_buf[UART_RX_BUF_SIZE];
static uint16_t            s_read_pos;               /* okunmamis ilk bayt */
static frame_parser_t      s_parser;                 /* yarim cerceve durumu */
static volatile uint8_t    s_rx_pending;             /* kesme set eder */

uart_rx_stats_t uart_rx_stats;
uart_rx_state_t uart_rx_state;

static void frame_received(const frame_info_t *info, void *user_data);


HAL_StatusTypeDef uart_rx_start(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->hdmarx == NULL))
    {
        return HAL_ERROR;
    }

    s_huart      = huart;
    s_read_pos   = 0U;
    s_rx_pending = 0U;

    frame_parser_init(&s_parser);

    return HAL_UARTEx_ReceiveToIdle_DMA(huart, s_dma_buf,
                                        (uint16_t)sizeof(s_dma_buf));
}


void uart_rx_service(void)
{
    if (s_rx_pending != 0U)
    {
        s_rx_pending = 0U;
        uart_rx_drain();
    }
}


void uart_rx_drain(void)
{
    const uint16_t buf_size = (uint16_t)sizeof(s_dma_buf);
    uint16_t write_pos;
    uint16_t chunk_len;

    if (s_huart == NULL)
    {
        return;
    }

    for (;;)
    {
        /* NDTR kalan transfer sayisini tutar ve her baytta azalir:
              yazilan bayt sayisi = buf_size - NDTR
           Modulo tek bir uc durum icin gerekli: DMA son bayti yazip NDTR'yi
           henuz yeniden yuklemediginde 0 okunur, buf_size - 0 = buf_size cikar
           ve bu gecersiz bir indekstir. Modulo onu 0'a cevirir. */
        write_pos = (uint16_t)((buf_size -
                     (uint16_t)__HAL_DMA_GET_COUNTER(s_huart->hdmarx)) % buf_size);

        if (write_pos == s_read_pos)
        {
            /* Bekleyen veri yok.
               DIKKAT: tam bir tur uzerine yazilmis olsa da konumlar boyle
               gorunur. Modulo aritmetigi tasmayi tespit edemez; koruma
               zamaninda tuketmektir (256 bayt / 11520 bayt/s ~ 22 ms). */
            break;
        }

        if (write_pos > s_read_pos)
        {
            /* Sarim yok: tek ardisik aralik */
            chunk_len = (uint16_t)(write_pos - s_read_pos);
            frame_parser_feed(&s_parser, &s_dma_buf[s_read_pos], chunk_len,
                              frame_received, NULL);
            s_read_pos = write_pos;
        }
        else
        {
            /* Sarim var: once tampon SONUNA kadar besle. Kalani dongunun
               sonraki turu artik "sarim yok" durumu olarak halleder. */
            chunk_len = (uint16_t)(buf_size - s_read_pos);
            frame_parser_feed(&s_parser, &s_dma_buf[s_read_pos], chunk_len,
                              frame_received, NULL);
            s_read_pos = 0U;
        }
    }
}


const frame_parser_t *uart_rx_get_parser(void)
{
    return &s_parser;
}


/* Dogrulanmis bir cerceve cozuldugunde frame_parser_feed tarafindan cagrilir.
   main baglaminda calisir: uart_rx_service -> uart_rx_drain ->
   frame_parser_feed -> buraya.
   info->payload YALNIZCA bu cagri suresince gecerli; saklanacaksa
   kopyalanmali.

   Cerceve sayisi burada tutulmuyor: s_parser.frames_ok zaten ayni bilgiyi
   veriyor, iki yerde tutmak tutarsizlik riski demek. */
static void frame_received(const frame_info_t *info, void *user_data)
{
    (void)user_data;

    if (uart_rx_state.seq_synced == 0U)
    {
        /* Ilk cerceve: gonderenin hangi degerden basladigini bilemeyiz.
           Karsilastirma yapmadan referans aliyoruz (RTP alicisi da boyle
           yapar: ilk pakette sira numarasina senkronize olur). */
        uart_rx_state.seq_synced = 1U;
    }
    else if (info->seq != uart_rx_state.next_seq)
    {
        uart_rx_state.seq_gaps++;
    }
    else
    {
        /* Beklenen sira geldi */
    }

    uart_rx_state.last_seq = info->seq;

    /* Beklentiyi GELEN degerden turetiyoruz. "next_seq++" yazsaydik tek bir
       kayiptan sonra kalici olarak bir geri kalir ve sonraki her cerceveyi
       kayip sayardik. (uint16_t) cast'i 65535 -> 0 sarimini halleder. */
    uart_rx_state.next_seq = (uint16_t)(info->seq + 1U);

    if ((info->type == FRAME_TYPE_JOYSTICK) && (info->payload_len == 4U))
    {
        uart_rx_state.joy_x = (int16_t)((uint16_t)info->payload[0] |
                                       ((uint16_t)info->payload[1] << 8));
        uart_rx_state.joy_y = (int16_t)((uint16_t)info->payload[2] |
                                       ((uint16_t)info->payload[3] << 8));
    }
}


/* --- HAL callback'leri ---
   Bu iki fonksiyon HAL'de __weak tanimli ve butun UART'lar icin ortaktir;
   bu yuzden hangi UART oldugu kontrol edilir. */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if ((s_huart != NULL) && (huart->Instance == s_huart->Instance))
    {
        uart_rx_stats.rx_events++;
        uart_rx_stats.last_size = Size;

        /* Size mutlak konum bildirir, "kac yeni bayt" degil. Tuketimde
           KULLANILMAZ; konumu uart_rx_drain kendisi NDTR'den hesaplar. */
        s_rx_pending = 1U;

        switch (HAL_UARTEx_GetRxEventType(huart))
        {
            case HAL_UART_RXEVENT_IDLE:
                uart_rx_stats.idle_events++;
                break;

            case HAL_UART_RXEVENT_HT:
                uart_rx_stats.ht_events++;
                break;

            case HAL_UART_RXEVENT_TC:
                uart_rx_stats.tc_events++;
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
        uart_rx_stats.error_events++;
        uart_rx_stats.last_error = huart->ErrorCode;
    }
}
