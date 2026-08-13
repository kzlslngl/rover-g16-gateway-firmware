# Rover G16 Gateway Firmware

ESP32-ETH01 v1.4 üzerinde çalışan, SKYDROID GR01/G16 alıcısının SBUS
verisini kablolu Ethernet üzerinden PLC'ye salt-okunur Modbus TCP register
görüntüsü olarak sunan güvenlik odaklı gateway firmware'i.

Bu depo firmware'in ROS 2 uygulamasından bağımsız geliştirilmesi içindir.
Protokolün ana sözleşmesi `rover_core_ros2` deposundadır:

- Referans commit: `66f6bde8b457ff8ef04ed045184f519dc34e5ef2`
- Elektriksel profil: `rover_hardware/config/g16_gateway.yaml`
- Register otoritesi: `rover_hardware/config/plc_protocol_v1.yaml`
- Test vektörleri: `rover_hardware/config/plc_protocol_v1_test_vectors.yaml`
- Uygulama notu: `docs/ESP32_ETH01_G16_GATEWAY_UYGULAMA_NOTU.md`

> Register yerleşimi bu depoda bağımsız değiştirilmez. Değişiklik önce ana
> sözleşmede sürümlenir, ardından firmware'e taşınır.

## Güvenlik ve yetki sınırı

```text
GR01 SBUS RX
  -> UART byte alımı
  -> SBUS frame senkronizasyonu ve decode
  -> freshness/hata sayaçları
  -> tutarlı ve CRC'li register snapshot'ı
  -> kablolu Ethernet Modbus TCP FC03 sunucusu
  -> PLC poll
```

Firmware:

- hareket, yön veya mod kararı vermez;
- kanal eşleme, endpoint, deadband veya neutral kalibrasyonu yapmaz;
- aktüatör ya da PLC çıkışı sürmez;
- safety reset veya hareket komutu kabul etmez;
- genel amaçlı Modbus register yazma arayüzü sunmaz;
- manuel kontrol yolunda Wi-Fi kullanmaz;
- Ethernet bağlantısını SBUS freshness kanıtı saymaz.

Kanal anlamlandırma, kalibrasyon, manuel kontrol yetkisi ve nihai fail-safe
kararı PLC'ye aittir. ESP arızası veya stale veri güvenli tarafta geçersiz
veri üretmelidir.

Modbus TCP sunucusu varsayılan bench profilinde `192.168.2.166:502`, unit ID
`1` üzerinde yalnızca FC03
ile başlangıç adresi `320`, uzunluk `64` olan tam snapshot okumasını kabul eder.
Diğer adres/uzunluklar ve bütün yazma fonksiyonları exception ile reddedilir.
Ethernet link/IP olayları ile Modbus bağlantı, istek, exception, timeout ve
transport hata sayaçları 10 saniyelik sağlık logunda raporlanır. SBUS ana
işlem hattı task-watchdog tarafından izlenir; sessiz kalan TCP istemcisi iki
saniye sonra kapatılır.

## Donanım profili

| Bileşen | Değer |
|---|---|
| Gateway | ESP32-ETH01 v1.4 |
| Receiver | SKYDROID GR01 / G16 |
| SBUS RX | GPIO35, RX-only |
| UART | 100000 baud, 8E2 |
| SBUS frame | 25 byte |
| Kanal verisi | 16 x 11-bit ham değer |
| Ağ | LAN8720 RMII, build-time yapılandırılabilir; bench `192.168.2.166/24` |
| Ethernet PHY | adres 1, reset/power GPIO16 |
| Ethernet yönetim | MDC GPIO23, MDIO GPIO18 |
| RMII saat | GPIO0 input |
| Modbus unit ID | 1 |

Elektriksel bağlantıdan önce GR01 çıkış voltajı ve idle polaritesi osiloskopla
ölçülmelidir. RX inversion ancak bu ölçümden sonra seçilir. Pasif Y ayırıcı
yasaktır. PLC ve Orin'e paralel izleme gerekiyorsa aktif çift çıkışlı buffer,
bağımsız seri dirençler ve giriş koruması kullanılır; galvanik izolasyon
tercih edilir.

## ESP -> PLC Modbus sözleşmesi

Protokol sürümü `1.1`'dir. ESP, holding-register offset `0x0140` (320)
başlangıcında 64 register sunar. PLC bloğu tek FC03 isteğiyle okur.

| Offset | Tip | Alan | Kural |
|---:|---|---|---|
| 320 | uint16 | `magic` | `0x4547` |
| 321-323 | uint16 | sürüm/uzunluk | `1`, `1`, `64` |
| 324-325 | uint32 | `begin_sequence` | snapshot ile ilerler |
| 326-327 | uint32 | `gateway_session_id` | her boot/reset sonrası değişir |
| 328-329 | uint32 | `gateway_heartbeat` | gateway çevrimiyle ilerler |
| 330-331 | uint32 | `sbus_frame_counter` | yalnız geçerli yeni frame ile ilerler |
| 332-333 | uint32 | `gateway_monotonic_ms` | monotonic uptime, düşük 32 bit |
| 334 | uint16 | `frame_age_ms` | saturate edilmiş frame yaşı |
| 335 | bitfield16 | `sbus_flags` | valid/lost/failsafe/alive/fault |
| 336 | uint16 | `channel_count` | geçerli profilde `16` |
| 337 | bitfield16 | `channel_valid_mask` | kanal başına geçerlilik |
| 338-353 | uint16[16] | `channel_raw` | normalize edilmemiş 11-bit değerler |
| 354-355 | uint32 | `frame_period_us` | geçerli ardışık frame farkı |
| 356-357 | uint32 | `invalid_frame_count` | yapısal/UART/frame hataları |
| 358-359 | uint32 | `frame_lost_count` | SBUS lost flag sayacı |
| 360-361 | uint32 | `failsafe_count` | SBUS failsafe flag sayacı |
| 362-379 | reserved | reserved | daima sıfır |
| 380-381 | uint32 | `crc32` | register 320-379 üzerinden |
| 382-383 | uint32 | `end_sequence` | begin ile aynı |

Register'lar 16-bit big-endian, çok register'lı alanlar high-word-first
kodlanır. CRC, register 320-379'un high-byte/low-byte akışı üzerinde
CRC-32/ISO-HDLC'dir. Kontrol vektörü
`"123456789" -> 0xCBF43926` değeridir.

Tutarlı snapshot yayımlama sırası:

1. Yeni sequence değerini seç.
2. `begin_sequence` dahil CRC kapsamındaki alanları ve reserved sıfırlarını
   staging görüntüsünde oluştur.
3. Register 320-379 üzerinden CRC hesaplayıp 380-381'e yaz.
4. Aynı sequence değerini CRC kapsamı dışındaki `end_sequence` alanına yaz.
5. Tamamlanmış 64-register görüntüsünü atomik biçimde active yap.

Her boot/watchdog resetinde yeni bir `gateway_session_id` üretilir; önceki
snapshot yeniden geçerli ilan edilmez.

## Önerilen kaynak yapısı

```text
rover_g16_gateway_firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── protocol/
│   ├── protocol_version.h
│   └── known_test_vectors.h
├── components/
│   ├── sbus_decoder/
│   ├── freshness/
│   ├── register_image/
│   └── crc32/
├── main/
│   ├── app_main.c
│   ├── sbus_transport.c
│   ├── ethernet.c
│   └── modbus_server.c
└── test/
```

Hedef framework ESP-IDF'dir. SBUS decoder, freshness, endian/CRC ve register
image donanımdan bağımsız C modülleri olmalı ve kart olmadan host üzerinde
test edilebilmelidir. UART, Ethernet ve Modbus katmanları bu saf çekirdeğin
adaptörleridir.

Çalışma yolunda dinamik bellek ayırma ve uzun bloklayan çağrılar
kullanılmamalı; watchdog ve sayaç taşmaları açıkça ele alınmalıdır.

## Uygulama sırası

### Aşama 1 - Kart gerektirmeyen güvenli çekirdek

1. Minimal ESP-IDF proje iskeletini oluştur.
2. Protokol sabitlerini tek başlıkta tanımla ve compile-time boyut kontrolleri
   ekle.
3. 25-byte SBUS frame decoder'ını saf fonksiyon olarak yaz.
4. Gürültü, eksik frame, footer hatası ve byte-gap sonrası resync testlerini
   ekle.
5. CRC-32/ISO-HDLC ve big-endian yardımcılarını test vektörleriyle doğrula.
6. Freshness durumu ile 64-register snapshot builder'ı saf modüller olarak
   yazıp test et.

### Aşama 2 - ESP32 çevre birimleri

1. GPIO35 RX-only UART `100000 8E2` transport adaptörünü ekle.
2. Ölçülmüş elektriksel profile göre RX inversion'ı yapılandırılabilir tut.
3. Kartın kesin PHY/pin varyantına göre kablolu Ethernet'i ekle.
4. Yalnız gerekli FC03 okumasını sağlayan Modbus TCP sunucusunu ekle.
5. Ethernet, client, invalid/lost/failsafe ve watchdog tanılarını ekle.

### Aşama 3 - Masa ve entegrasyon testleri

1. Bilinen frame'lerden 16 kanal decode testi.
2. Lost, failsafe, frozen-frame ve sayaç anomalisi enjeksiyonu.
3. Reboot/session değişimi ve eski snapshot'ın reddi.
4. CRC/endian sonuçlarının ROS sözleşmesiyle birebir karşılaştırılması.
5. Ethernet kopması ve PLC poll kesilmesi.
6. Orin kapalıyken geçerli G16 -> ESP -> PLC manuel yol testi.
7. ESP arızasında stale verinin hiçbir aktüatöre uygulanmadığının doğrulanması.

## Donanım kodundan önce kesinleşmesi gerekenler

- GR01 SBUS high/low voltajı ve idle polaritesi
- Aktif çift çıkışlı buffer/izolatör şeması
- ESP32-ETH01 v1.4 kartının kesin PHY ve pin varyantı
- PLC IP, ESP IP, subnet, Modbus poll periyodu ve timeout değerleri
- G16 kanal eşleme ve neutral kalibrasyon sürümü (PLC tarafı)

Bu bilgiler gelmeden Aşama 1 tamamlanabilir. Aşama 2'de varsayılan pin veya
elektriksel polarite kalıcı kabul edilerek donanıma bağlanmamalıdır.

## İlk geliştirme görevi

İlk geliştirme değişikliği Aşama 1 iskeletiyle başlamalıdır: ESP-IDF proje
yapısı, protokol sabitleri, saf CRC/endian yardımcıları ve host testleri.
UART/Ethernet/Modbus kodu bu çekirdek testleri geçmeden eklenmemelidir.

Her değişiklikte ilgili host testleri, biçim/statik kontroller ve
`git diff --check` çalıştırılmalı; donanım gerektiren testler ayrı
raporlanmalıdır.

Güncel uygulama durumu ve sıradaki işler için [`ROADMAP.md`](ROADMAP.md)
dosyasına bakın.

PLC programlama ve HIL hazırlığı için gerekli register kabul sırası, fail-safe
kuralları ve beklenen saha bilgileri [`PLC_INTEGRATION_GUIDE.md`](PLC_INTEGRATION_GUIDE.md)
dosyasında toplanmıştır.

Heap, bloklama ve watchdog denetim sonucu [`RUNTIME_AUDIT.md`](RUNTIME_AUDIT.md)
dosyasında tutulur.
