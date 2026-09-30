# UART alım zinciri — mimari ve kritik noktalar

Proje: `UART_IDLE_DMA` · STM32F407VG · STM32CubeIDE 1.19 · HAL
Durum tarihi: 30 Eylül 2026 · Kapsanan modüller: M2–M6

Bu belge, koda dokunmadan önce bilinmesi gerekenleri anlatır. Geliştirme planı
ve yapılacaklar listesi ayrı belgededir: [UART_GELISTIRME_PLANI.md](UART_GELISTIRME_PLANI.md).

---

## 1. Ne yapıyor

PC'den UART üzerinden gelen bayt akışından, doğrulanmış protokol paketlerini
çıkarır. Şu an joystick verisi (X/Y) ve sıra numarası takibi uygulanmış
durumda; komut uygulama ve yanıt gönderme henüz yok.

Alım zinciri baştan sona çalışır ve donanım üzerinde doğrulanmıştır.

---

## 2. Katmanlar

```
TEL                   gerilim seviyeleri, 8N1 çerçeveleme
                              ↓
USART2 donanımı       start/stop bitlerini soyar, DR'ye bir bayt koyar
                              ↓
DMA1 Stream5          DR'den RxData[] içine yazar (circular), CPU karışmaz
                              ↓
RxData[256]           ham bayt akışı, dairesel tampon
                              ↓
rx_tuket()            read_pos/write_pos ile ardışık aralıkları çıkarır
                              ↓
parser_besle()        sınır bulur, VERSION/LENGTH/CRC doğrular
                              ↓
paket_geldi()         sıra takibi, X/Y çözümü
```

Her katman yalnızca altındakinin çıktısını görür. `parser.c` UART, DMA veya
dairesel tampon nedir bilmez; girdisi düz bir bayt dizisidir. Bu, M4'te yazılan
27 birim testinin donanım olmadan koşabilmesinin ve M5–M6'da `parser.c`'ye tek
satır dokunulmamasının sebebidir.

---

## 3. Protokol

```
AA 55 | VERSION | TYPE | LENGTH | SEQUENCE | PAYLOAD | CRC16
  2        1       1      1         2          N        2     bayt
```

- `LENGTH` **yalnızca payload boyutudur**. Toplam paket = `9 + LENGTH`.
- Çok baytlı alanlar **little-endian** (düşük bayt önce).
- `VERSION` şu an `1`.
- Maksimum payload 55, maksimum toplam 64 bayt.

### CRC sözleşmesi

CRC-16/IBM-3740 (CRC-16/CCITT-FALSE):

| Parametre | Değer |
|---|---|
| Polinom | `0x1021` |
| Başlangıç | `0xFFFF` |
| Yansıtma (in/out) | yok |
| Çıkış XOR | `0x0000` |

CRC, **`VERSION` alanından payload sonuna kadar** hesaplanır. Başlangıç baytları
ve CRC alanının kendisi hesaba katılmaz. Hatta düşük bayt önce gider.

"CRC-16" tek bir algoritma değildir; bu parametreler iki tarafta birebir aynı
olmadan hiçbir paket doğrulanmaz.

### Mesaj türleri

| TYPE | Anlam | Payload |
|---|---|---|
| `0x10` | Joystick verisi | X: int16, Y: int16 (4 bayt) |
| `0x11` | Joystick modu | 0 kapalı / 1 açık (1 bayt) |
| `0x20` | Çıkış ayarla | çıkış no + durum (2 bayt) |
| `0x80` | Komut yanıtı | TYPE + sonuç kodu (2 bayt) — henüz uygulanmadı |

### Referans vektörler

```
CRC("123456789")                        = 0x29B1
X=+1000, Y=-500, SEQ=1  →  AA 55 01 10 04 01 00 E8 03 0C FE 46 59   (13 bayt)
TYPE 0x11, payload {01}, SEQ=2          →  10 bayt, CRC 0xED4E
TYPE 0x11, payload yok, SEQ=7           →   9 bayt, CRC 0x4FD9
TYPE 0x20, payload {AA,55}, SEQ=300     →  11 bayt, CRC 0x8F8D
```

---

## 4. Modüller

| Dosya | Sorumluluk | Bağımlılık |
|---|---|---|
| `Core/Src/crc16.c` | CRC-16/CCITT-FALSE hesabı | yok (saf C) |
| `Core/Src/packet.c` | Paket **oluşturma** (gönderme yönü) | `crc16` |
| `Core/Src/parser.c` | Paket **ayrıştırma** (alma yönü) | `crc16`, `packet.h` (sabitler) |
| `Core/Src/main.c` | Donanım kurulumu, tüketim, uygulama | HAL, yukarıdakiler |
| `tools/protokol.py` | Bağımsız referans uygulaması | Python 3 |

`packet.c` ve `parser.c` birbirinin tersidir: biri veriden bayt üretir, diğeri
baytlardan veri çıkarır.

### Arayüzler

```c
uint16_t crc16_ccitt(const uint8_t *veri, uint16_t uzunluk);

uint8_t  paket_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                       uint8_t tur, uint16_t sira,
                       const uint8_t *payload, uint8_t payload_uzunluk);
uint8_t  paket_joystick_olustur(uint8_t *hedef, uint8_t hedef_boyut,
                                int16_t x, int16_t y, uint16_t sira);

void parser_sifirla(parser_t *p);
void parser_besle(parser_t *p, const uint8_t *veri, uint16_t uzunluk,
                  paket_geri_cagri_t geri_cagri, void *kullanici);
```

`paket_olustur` hata durumunda `0` döndürür (geçerli paket en az 9 bayt
olduğundan karışma ihtimali yoktur). Dönüş değeri kontrol edilmeden
gönderilmemelidir.

---

## 5. Kritik noktalar

Bu bölüm, yanlış anlaşıldığında sessiz hataya yol açan noktaları listeler.

### 5.1 IDLE, USART kesmesine bağlıdır — DMA yetmez

DMA veriyi kaybetmeden taşır ama **haber vermez**. DMA'nın ürettiği olaylar
yalnızca HT (yarım tampon) ve TC (tam tampon), yani sabit bayt sayılarıdır.

100 Hz'de 13 baytlık joystick paketi = 1300 bayt/s. 256 baytlık tamponda:

| Olay | Tetiklenme aralığı |
|---|---|
| HT (128. bayt) | ~98 ms |
| TC (256. bayt) | ~197 ms |
| IDLE | son bayttan ~87 µs sonra |

IDLE olmadan bir paketin geldiğini öğrenmek 98 ms sürebilir. IDLE bayrağı
USART2'nin durum register'ındadır ve CPU'ya ulaşmasının tek yolu USART2
kesmesidir:

```
IDLE bayrağı → USART2_IRQHandler → HAL_UART_IRQHandler → HAL_UARTEx_RxEventCallback
```

`USART2_IRQHandler` yoksa `HAL_UARTEx_ReceiveToIdle_DMA()` çalışır, DMA veri
toplar, ama IDLE callback'i **hiç tetiklenmez**.

IDLE ayrıca tampon boyutu ile gecikme arasındaki bağı koparır: tamponu taşma
payına göre seçersin, gecikmeyi IDLE halleder.

### 5.2 IDLE paket sınırı belirlemez

IDLE'ın tek işi "tamponda yeni veri var, bak" demektir. Paket sınırını **CRC
doğrulaması** belirler.

PC iki paketi birleştirip tek seferde gönderirse tek IDLE gelir ve ayrıştırıcı
ikisini de çözer. Bir paket ikiye bölünürse iki IDLE gelir ve ayrıştırıcı yarım
paketi saklayarak tamamlar. Her ikisi de test edilmiştir (S4, S2).

### 5.3 Callback'in `Size` parametresi mutlak konumdur

```c
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
```

`Size`, **"kaç yeni bayt geldi" değildir**; tamponun başından itibaren dolu
bayt sayısıdır.

| Olay | HAL'in verdiği `Size` |
|---|---|
| HT | her zaman `tampon_boyu / 2` |
| TC | her zaman `tampon_boyu` |
| IDLE | `tampon_boyu - NDTR` (o anki konum) |

Sonuçları: ardışık iki olay aynı `Size`'ı bildirebilir, sarımda değer küçülür,
HT gerçekte kaç yeni bayt geldiğine bakmaz. `Size`'ı yeni bayt sayısı sanıp
`parser_besle(&p, RxData, Size, ...)` yazmak aynı baytların tekrar tekrar
ayrıştırılmasına yol açar.

Kodda `Size` yalnızca `son_size` değişkenine gözlem amacıyla kopyalanır;
tüketimde kullanılmaz.

### 5.4 Yazma konumu NDTR'den türetilir

```c
write_pos = (boyut - __HAL_DMA_GET_COUNTER(&hdma_usart2_rx)) % boyut;
```

NDTR kalan transfer sayısını tutar ve her baytta **azalır**. Yazılan bayt
sayısı `boyut - NDTR`'dir.

Modulo tek bir uç durum için gereklidir: DMA 256. baytı yazıp NDTR'yi henüz
yeniden yüklemediğinde `0` okunur, `256 - 0 = 256` çıkar ve bu geçersiz bir
indekstir. Modulo onu `0`'a çevirir. Bu satır olmadan dizi dışına erişilir.

`write_pos` bir durum değişkeni değil, **anlık fotoğraftır** — saklanmaz, her
turda yeniden okunur. DMA kod çalışırken yazmaya devam ettiği için saklanan
değer anında bayatlar.

### 5.5 Sahiplik ayrımı yarışı önler

```
write_pos  →  DMA yazar (NDTR üzerinden), tüketici yalnızca okur
read_pos   →  yalnızca tüketici yazar, DMA hiç bakmaz
```

İki taraf farklı değişkenlere sahiptir. Bu yüzden kilit, kritik bölüm veya
atomik işlem gerekmez. Tasarımın en önemli özelliği budur ve korunmalıdır:
`read_pos`'a başka bir bağlamdan yazılırsa bu garanti kaybolur.

### 5.6 Konumların eşitliği iki şey anlamına gelir

`read_pos == write_pos` "yeni veri yok" demektir. Ama **tam bir tur üzerine
yazılmışsa da** aynı görünür. 256 baytın tamamı ezilmişse konumlar yine eşit
çıkar ve kayıp fark edilmez.

Modüler aritmetik tam tur taşmasını tespit edemez. Mevcut koruma, **zamanında
tüketmektir** — başka bir mekanizma yoktur. Taşma tespiti gerekirse toplam
üretim/tüketim sayacı veya tur sayacı tasarlanmalıdır.

Somut sınır: 115200 baud'da tampon 256 bayt / 11520 bayt/s ≈ **22 ms** içinde
tüketilmelidir (kesintisiz tam hızlı trafikte). HT/TC ile her yarım tamponun
ezilmeden tüketilmesi hedeflenirse bütçe ~11 ms olur.

### 5.7 Sarımda iki aralık, tek kod yolu

Sarım olduğunda yeni baytlar bellekte ardışık değildir. `rx_tuket` her turda
**tek** ardışık aralık tüketir:

- Sarım yoksa: `read_pos … write_pos-1`, sonra `read_pos = write_pos`
- Sarım varsa: `read_pos … boyut-1`, sonra `read_pos = 0`

İkinci durumda kalan kısmı döngünün sonraki turu "sarım yok" hâli olarak
halleder. Böylece sarım için ayrı bir kod yolu yazılmaz.

Sarım sınırında bölünen paket, `rx_parser`'ın yarım paketi saklaması sayesinde
kaybolmaz — M4'teki S2 senaryosunun aynısıdır.

### 5.8 `rx_parser` çağrılar arasında yaşamalıdır

`rx_tuket` içinde yerel tanımlanırsa her çağrıda sıfırlanır ve yarım paketler
kaybolur. Sarımda önce 6 bayt, sonra 14 bayt beslenir; ayrıştırıcının ilk 6
baytı hatırlaması zorunludur.

### 5.9 Hatalı adayda tam bir bayt atılır

`AA 55` kanıt değil, **aday** işaretidir — payload içinde de geçebilir
(TYPE `0x20` örnek paketi tam olarak bunu içerir). Kanıt ancak uzunluk ve CRC
doğrulanınca oluşur.

Aday çürüdüğünde tamponun tamamı değil **yalnızca bir bayt** atılır. Sebebi:
`tampon[0]`'ın paket başlangıcı olmadığı kanıtlanmıştır, `tampon[1]` hakkında
hiçbir şey bilinmez. Bozuk adayın içinde gerçek bir paketin başlangıcı olabilir.

Bedeli: bozuk bir paket 13 baytlık bir eleme turuna yol açar ve bu sırada
`sayac_surum_hata` gibi "gürültü" sayaçları artabilir. Bu bir hata değil, yeniden
tarama davranışının doğal sonucudur. Asıl kanıt `sayac_gecerli`'dir.

### 5.10 `volatile` neyi çözer, neyi çözmez

`volatile`, derleyiciye "bu değeri register'da önbelleklemeden her seferinde
bellekten oku" der. **Atomiklik sağlamaz.**

| Değişken | `volatile`? | Sebep |
|---|---|---|
| `yeni_veri_var`, `count`, `idle_sayaci`, `errorcount`, `son_size`, `son_hata_kodu` | evet | Kesme yazar, main okur — iki farklı çalışma bağlamı |
| `paket_sayaci`, `son_sira`, `son_x`, `son_y`, `sira_atlama`, `read_pos` | hayır | Yalnızca main bağlamında yazılıp okunur |

`paket_geldi` bir kesme içinde çalışmaz: `while(1) → rx_tuket → parser_besle →
paket_geldi` zinciri main bağlamındadır.

`volatile`'ın çözmediği şeyler: `sayac++` üç işlemdir (oku, artır, yaz) ve
bölünebilir; iki ayrı değişken birlikte tutarlı okunamaz. İki bağlam da aynı
değişkene yazmaya başlarsa kritik bölüm veya RTOS kuyruğu gerekir.

### 5.11 `payload` işaretçisinin ömrü

```c
typedef struct {
    uint8_t        tur;
    uint16_t       sira;
    uint8_t        uzunluk;
    const uint8_t *payload;
} paket_bilgi_t;
```

`payload`, ayrıştırıcının iç tamponuna işaret eder ve **yalnızca geri çağrı
süresince geçerlidir**. Geri çağrı döndükten sonra o baytlar silinir/üzerine
yazılır. Saklanacaksa kopyalanmalıdır.

### 5.12 Tampon boyutu tek kaynaktan gelmeli

```c
HAL_UARTEx_ReceiveToIdle_DMA(&huart2, RxData, sizeof(RxData));
```

Üçüncü parametre DMA'ya "şu kadar yerin var" der. Dizi boyutundan farklı bir
sayı yazılırsa DMA komşu değişkenlerin üzerine yazar; donanım bunu yapar ve
hiçbir uyarı vermez. `sizeof` kullanmak iki sayının ayrışmasını engeller.

### 5.13 DMA tamponu CCM belleğinde olmamalı

STM32F407'de DMA1, CCM RAM bölgesine (`0x10000000`) erişemez. Normal global
diziler SRAM'e gider, özel bir şey yapmak gerekmez — ancak linker script'i
değiştirilirse bu kısıt hatırlanmalıdır.

### 5.14 Loopback testinde RX, TX'ten önce başlatılmalı

`PA2`–`PA3` jumper'ı ile yapılan loopback testinde `HAL_UART_Transmit`
çağrıldığı anda baytlar RX pininde görünür. DMA hazır değilse baytlar kaybolur
veya ORE hatası oluşur. Kodda `HAL_UARTEx_ReceiveToIdle_DMA` çağrısı gönderimden
öncedir.

### 5.15 Sıra beklentisi gelen değerden türetilir

```c
beklenen_sira = (uint16_t)(paket->sira + 1U);   /* doğru */
beklenen_sira++;                                 /* yanlış */
```

`beklenen_sira++` kullanılırsa tek bir kayıptan sonra kalıcı olarak bir geri
kalınır ve sonraki her paket kayıp sayılır. Gelen değerden türetmek kaybı bir
kez raporlayıp senkronizasyonu geri getirir.

`(uint16_t)` cast'i `65535 → 0` sarımını kendiliğinden halleder; ayrı bir
kontrol gerekmez.

İlk pakette karşılaştırılacak bir beklenti yoktur (gönderenin hangi değerden
başladığı bilinemez). `sira_baslatildi` bayrağı ilk paketi referans olarak alır.

**Varsayım:** gönderen tüm mesaj türleri için **tek ortak sayaç** kullanır. PC
tarafı tür başına ayrı sayaç kullanırsa bu mantık yanlış kayıp raporlar; planın
1. adımında netleştirilecek açık bir karardır.

---

## 6. Doğrulama durumu

### Birim testleri (kartta, `testleri_kosur()`)

| Grup | Adet | İçerik |
|---|---|---|
| CRC | 6 | Standart vektör `0x29B1`, boş girdi, tek bayt, protokol paketi |
| Paketleyici | 9 | Referans baytlar, sınır kontrolleri, 12/13 bayt sınır testi |
| Ayrıştırıcı | 12 | Bölünmüş, birleşik, çöplü, bozuk CRC, yarım, `AA 55` payload, kurtarma |
| **Toplam** | **27** | `test_gecen == 27`, `test_kalan == 0` |

Sonuçlar `test_sonuc[]`, `test_gecen`, `test_kalan` üzerinden debugger'da
okunur (`Live Expressions`).

### Donanım doğrulamaları

PA2–PA3 loopback, STM32F4DISCOVERY, ST-LINK üzerinden GDB ile okundu
(30 Eylül 2026).

| Ne | Ölçüm | Durum |
|---|---|---|
| Birim testleri | `test_gecen=27`, `test_kalan=0` | Doğrulandı |
| DMA yazımı | `RxData` beklenen baytları taşıyor | Doğrulandı |
| Uçtan uca alım | `son_x=1000`, `son_y=-500` | Doğrulandı |
| Sarım | 40 paket / 520 bayt → `paket_sayaci=40`, `son_sira=40`, `sira_atlama=0` | Doğrulandı |
| Okuma konumu | `read_pos=8` (520 mod 256) | Doğrulandı |
| Ayrıştırıcı temizliği | `crc_hata=0`, `atilan_bayt=0`, `yazilan=0` | Doğrulandı |
| HT/TC olayları | `ht_sayaci=2`, `tc_sayaci=2` (520 bayt için beklenen) | Doğrulandı |
| IDLE olayı | `idle_sayaci=1`, `son_size=8` | Doğrulandı |

`son_size=8` ölçümü, 5.3'teki tespitin doğrudan kanıtıdır: 520 bayt alınmış
olmasına rağmen `Size` **mutlak konumu** (8) bildiriyor, gelen bayt sayısını
değil.

### Doğrulanmamış olanlar

- **Kesme güdümlü tüketim yolu sınanmadı.** T2 testinde `rx_tuket()` gönderim
  döngüsü içinden **eşzamanlı** çağrılıyor; 40 paketi teslim eden yol bu.
  `while(1)` içindeki `yeni_veri_var` yolunun bağlı olduğu doğrulandı (IDLE
  tetikleniyor, bayrak kalkıyor) ama testin sonucu ona dayanmıyor.
- Sürekli tam hızlı trafik altında en kötü gecikme **ölçülmedi**
- Kayıpsızlık iddia **edilemez**: taşma tespiti yok (bkz. 5.6)
- PC'den gerçek veri ile test edilmedi; yalnızca loopback
- FreeRTOS altında hiç çalıştırılmadı

---

## 7. Araç: `tools/protokol.py`

Protokolün Python'daki bağımsız referans uygulaması. C kodundan ayrı yazılmıştır;
iki uygulamanın aynı sonucu vermesi doğruluk kanıtı sayılır.

```bash
python tools/protokol.py test               # kendi testlerini koşar (9/9)
python tools/protokol.py vektor             # C için hazır test dizileri
python tools/protokol.py akis               # ayrıştırıcı test akışları + beklenen sayaçlar
python tools/protokol.py coz "AA 55 01 ..." # bayt dizisini alan alan çözer, CRC doğrular
```

`coz` komutu hata ayıklamada kullanılır: debugger'dan alınan baytlar
(`*RxData@13`, Number Format → Hex) yapıştırıldığında hangi alanın bozuk
olduğunu söyler.

---

## 8. Bilinen eksikler

| # | Konu | Nerede ele alınacak |
|---|---|---|
| 1 | USART2 ve DMA kesme önceliği `0` | M7 — FreeRTOS `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` kuralı |
| 2 | `NVIC_PRIORITYGROUP_0` | M7 — Cortex-M4 + FreeRTOS için `PRIORITYGROUP_4` |
| 3 | Bare-metal bayrak yerine task bildirimi | M7 |
| 4 | Kısmi paket zaman aşımı | M9 — yarım paket sonsuza kadar tamponda bekliyor |
| 5 | Taşma/kayıp tespiti | M9 — sayaç veya tur takibi |
| 6 | TX yolu, komut yanıtı (`0x80`), tekrar ayıklama | M9 |
| 7 | Joystick modu (`0x11`) davranışı, komut uygulama | M8 |
| 8 | Ortak/ayrı sıra sayacı kararı | PC arayüzü ile birlikte (plan Adım 1) |

---

## 9. Sürüm işaretleri

| Etiket | İçerik |
|---|---|
| `crc` | M2 — CRC modülü ve test tezgâhı |
| `m5-dma` | M3+M4+M5 — paketleyici, ayrıştırıcı, DMA/IDLE altyapısı |
