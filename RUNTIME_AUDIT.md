# Çalışma Yolu Kaynak ve Bloklama Denetimi

Son denetim: 13 Ağustos 2026.

Bu denetimin kapsamı SBUS -> freshness -> snapshot -> Modbus TCP çalışma
yolunda dinamik bellek tahsisi, sınırsız bekleme ve watchdog davranışıdır.

## Sonuç

- SBUS ana döngüsünde `malloc/calloc/realloc/free` veya C++ `new/delete` yoktur.
- Frame decode, freshness ve register image işlemleri stack/static bellekle
  çalışır.
- SBUS event beklemesi en fazla 20 ms'dir; UART byte okuması non-blocking
  timeout `0` ile yapılır.
- SBUS pipeline task-watchdog kullanıcısı her ana döngüde beslenir.
- Snapshot publish/copy kilidi yalnız 128 byte kopyalama süresince tutulan kısa
  kritik bölümdür.
- Ethernet durum kilidi heap tabanlı mutex ve `portMAX_DELAY` yerine kısa
  `portMUX` kritik bölümüdür.
- Modbus sunucusu ayrı FreeRTOS task'ında çalışır. Bir client için receive/send
  timeout'u 2 saniyedir; sessiz istemci kapatılır.
- `accept()` sınırsız bekleyebilir, ancak yalnız Modbus task'ını bekletir ve
  SBUS/snapshot üretimini engellemez.
- Socket, TCP/IP, UART driver, event queue, NVS, Ethernet driver ve FreeRTOS
  task tahsisleri başlangıç/bağlantı altyapısına aittir. Per-frame veya
  per-snapshot uygulama heap tahsisi yoktur.

## Kabul edilen sınırlar

TCP/IP stack socket/client yaşam döngüsünde kendi dahili kaynaklarını kullanır.
Firmware yalnız tek Modbus client'ı seri olarak işler. Her client işlemi 2
saniyelik socket receive/send timeout'u ile sınırlandırılır. Bu bekleme SBUS
pipeline task'ından ayrıdır.

Production ağında istemci ACL'si, PLC reconnect politikası ve response timeout
PLC/deployment değerleri kesinleşince ayrıca uygulanmalıdır.

## Kart doğrulaması

13 Ağustos 2026 tarihinde portMUX değişikliği ESP32-ETH01 kartına yüklenerek
doğrulandı. Normal boot sonrasında `192.168.2.166` adresine beş ping isteğinin
tamamı yanıtlandı. FC03 ile 64 register okundu; canlı SBUS için `age_ms=0`,
flags `0x0009`, CRC eşleşmesi ve begin/end sequence eşleşmesi gözlendi. FC06
yazma isteği yine illegal-function exception ile reddedildi.

## Her değişiklikte tekrar kontrol

```powershell
rg -n "malloc|calloc|realloc|free\(|new\b|delete\b|portMAX_DELAY|vTaskDelay|xQueueReceive|recv\(|send\(|accept\(" main components
pio run
.\scripts\test_host.ps1
git diff --check
```

Yeni bir sınırsız bekleme eklenirse hangi task'ı etkilediği ve SBUS pipeline'ını
engelleyip engellemediği belgelenmelidir. Per-frame/per-snapshot heap tahsisi
kabul edilmez.
