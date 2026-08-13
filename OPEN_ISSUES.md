# Açık Sözleşme Konuları

Bu dosya ESP Firmware Codex'i tarafından, ana ROS/PLC sözleşmesinde uygulamayı
etkileyen bir çelişki veya eksik test otoritesi görüldüğünde Ana Proje
Codex'inin incelemesi için tutulur.

## OI-001 — CRC ve `begin_sequence` yazım sırası

**Durum:** Ana proje kararı verildi; çözüldü.

`begin_sequence`, absolute register 324–325'te bulunur ve CRC kapsamındaki
absolute register 320–379 bloğunun içindedir. `DECISIONS.md` bölüm 4'ün
önceki sürümü CRC hesaplanmasından sonra begin/end sequence yazılacağı izlenimi
veriyordu. CRC hesaplandıktan sonra `begin_sequence` değiştirilirse yayımlanan
CRC register 320–379 içeriğiyle eşleşmez.

Ana Proje Codex'i firmware çekirdeğinde uygulanan aşağıdaki sırayı protokol
v1.1 için onaylar:

1. yeni sequence seçilir;
2. `begin_sequence` dahil CRC kapsamındaki bütün alanlar yazılır;
3. register 320–379 üzerinden CRC hesaplanıp 380–381'e yazılır;
4. aynı sequence değeri CRC kapsamı dışındaki `end_sequence` 382–383'e yazılır.

`begin_sequence` CRC hesaplandıktan sonra değiştirilmez. `end_sequence`
CRC kapsamı dışında olduğu için CRC yazıldıktan sonra aynı sequence değeriyle
yazılır. `README.md` ve `DECISIONS.md` bu karara göre düzeltilmiştir.

Ana ROS insan-okunur sözleşmesi bir sonraki protokol dokümantasyon
güncellemesinde aynı sırayı açıkça yazmalıdır.

## OI-002 — Tam ESP snapshot known-result vektörü

**Durum:** Ana proje known-result vektörü verildi; upstream YAML senkronu
bekleniyor.

Ana Proje Codex'i aşağıdaki vektörü firmware kodunu çağırmadan, register
high-byte/low-byte akışı üzerinde bağımsız CRC-32/ISO-HDLC hesabıyla
oluşturdu. Bit-adımı ve tablo tabanlı iki bağımsız hesap aynı sonucu verdi.

Giriş alanları:

| Alan | Değer |
|---|---:|
| sequence | `0x01020304` |
| gateway session | `0xA1B2C3D4` |
| gateway heartbeat | `0x00000010` |
| SBUS frame counter | `0x00000020` |
| gateway monotonic ms | `0x00123456` |
| frame age ms | `25` |
| SBUS flags | `0x0009` — `FRAME_VALID | DECODER_ALIVE` |
| channel count / mask | `16 / 0xFFFF` |
| channels | `[0, 1, 172, 992, 1811, 2047, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000]` |
| frame period us | `14000` |
| invalid / lost / failsafe count | `2 / 3 / 4` |

Beklenen 64 register, absolute offset 320'den başlayarak:

```text
0x4547 0x0001 0x0001 0x0040 0x0102 0x0304 0xA1B2 0xC3D4
0x0000 0x0010 0x0000 0x0020 0x0012 0x3456 0x0019 0x0009
0x0010 0xFFFF 0x0000 0x0001 0x00AC 0x03E0 0x0713 0x07FF
0x0064 0x00C8 0x012C 0x0190 0x01F4 0x0258 0x02BC 0x0320
0x0384 0x03E8 0x0000 0x36B0 0x0000 0x0002 0x0000 0x0003
0x0000 0x0004 0x0000 0x0000 0x0000 0x0000 0x0000 0x0000
0x0000 0x0000 0x0000 0x0000 0x0000 0x0000 0x0000 0x0000
0x0000 0x0000 0x0000 0x0000 0x1274 0x9618 0x0102 0x0304
```

Beklenen CRC: `0x12749618`.

Kapsam:

- CRC girdisi ilk 60 register, absolute 320-379'dur;
- CRC register'ları `0x1274, 0x9618` olarak 380-381'dedir;
- end sequence `0x0102, 0x0304` olarak 382-383'tedir;
- reserved 362-379 sıfırdır.

Bu vektör firmware ve PLC testlerinde aynen kullanılabilir. Kalıcı otorite
olması için daha sonra
`rover_hardware/config/plc_protocol_v1_test_vectors.yaml` dosyasına
`esp_snapshot` olarak taşınmalıdır.

## OI-003 — Session ID entegrasyon kanıtı

**Durum:** Çözüldü; kart/NVS entegrasyonu doğrulandı.

Donanımdan bağımsız session türetme fonksiyonu cihaz kimliği, kalıcı boot
counter ve random değeri birleştirecek şekilde tanımlanmıştır. Ancak production
uygunluğu için ESP adaptörünün benzersiz cihaz kimliğini okuduğu, NVS boot
counter'ı boot başına yalnız bir kez atomik artırdığı ve donanım random kaynağı
başarısızlığını decoder fault'a çevirdiği kart üzerinde doğrulanmalıdır.

13 Ağustos 2026 kart testinde eFuse MAC kimliği, NVS'de atomik artırılan boot
sayacı ve donanım rastgele değeri session türetimine bağlandı. Ardışık EN
resetlerinde `boot=2 / session=1ce9fb56` ve ardından
`boot=3 / session=6c1cf188` gözlendi; snapshot sequence her iki boot'ta da
1'den başladı. NVS okunamadığında veya commit edilemediğinde firmware sessizce
devam etmek yerine fail-fast davranır.

## OI-004 — FC03 alt-aralık okuma politikası

**Durum:** Ana proje kararı verildi; çözüldü.

Protokol v1.1 ESP endpoint'i yalnız absolute başlangıç `320`, quantity `64`
olan tek FC03 isteğini kabul eder. Kısmi FC03 okumaları ve blok dışına taşan
okumalar `Illegal Data Address` ile reddedilir.

Bu karar PLC'nin her poll'da header, begin/end, CRC ve bütün payload'ı aynı
snapshot içinde değerlendirmesini zorunlu tutar. Diagnostics için kısmi
register okuması ayrı bir v1.1 davranışı değildir.

## OI-005 — Production deployment ve PLC zaman değerleri

**Durum:** Sözleşme referansı çözüldü; production sayısal değerleri
deployment/HIL kapısında.

Ana Proje Codex'inin 13 Ağustos 2026 tarihinde doğruladığı güncel sözleşme
referansı:

`rover-core-ros2/main@a464881182189243e27e413fd4b0d136ed3a5322`

Bu commit'teki dört ESP/PLC protokol kaynağı ilk referans
`66f6bde8b457ff8ef04ed045184f519dc34e5ef2` ile wire açısından aynıdır.

Değer sahipliği:

| Konu | Karar |
|---|---|
| ESP `192.168.144.166/24` | yalnız doğrulanmış masa profili; production değeri değildir |
| geliştirici PC `192.168.144.10/24` | yalnız masa client/gateway profili; production PLC adresi değildir |
| production PLC/ESP IP, subnet ve VLAN | deployment ağı kurulurken tek ortak config'te atanır |
| izin verilen PLC client IP | production PLC IP'si kesinleşince ESP/network ACL'ye uygulanır |
| PLC poll ve response timeout | PLC scan/ağ yükü ölçümü ve HIL ile PLC projesinde seçilir |
| SBUS stale timeout `100 ms` | mevcut masa/HIL adayıdır; fren/manual testinden önce production-final değildir |
| session değişimi neutral/re-arm | PLC authority state machine sahibidir; ESP'de süre tutulmaz |
| Modbus sessiz-client timeout `2 s` | socket kaynak korumasıdır; hareket freshness timeout'u değildir |
| task watchdog `5 s` | mevcut build/masa değeridir; worst-case runtime ölçümüyle production-final yapılır |
| production log seviyesi | varsayılan `WARN`; `INFO` yalnız commissioning/diagnostics profilinde |

Production değerleri kesinleşene kadar mevcut hard-coded ağ profili yalnız
bench firmware olarak etiketlenir; production release kabul edilmez. Bu
sayısal değerlerin açık kalması wire geliştirmeyi engellemez, fakat gerçek PLC
ve aktüatör entegrasyon kapısını kapalı tutar.

## OI-006 — SBUS kesildiğinde stale snapshot yayımlanması

**Durum:** Kritik uyumluluk bulgusu; firmware düzeltmesi ve hata enjeksiyon
kanıtı gerekli.

Güncel runtime yalnız yeni bir yapısal SBUS frame geldiğinde freshness view ve
64-register snapshot üretir. SBUS byte akışı tamamen durursa son Modbus
snapshot'ı düşük `frame_age_ms`, `FRAME_VALID = 1` ve
`channel_valid_mask = 0xFFFF` ile active buffer'da kalabilir.

PLC, yerel frame-counter freeze kontrolüyle bu veriyi ayrıca reddetmek
zorundadır; ancak bu savunma ESP'nin kendi stale/valid sözleşmesini ortadan
kaldırmaz.

Gerekli firmware davranışı:

- SBUS frame gelmese de monotonic periyodik snapshot yayımı devam eder;
- `gateway_heartbeat` ve begin/end sequence her yeni periyodik snapshot'ta
  ilerler;
- `sbus_frame_counter` yeni kullanılabilir frame yokken sabit kalır;
- `frame_age_ms` monotonic olarak ilerleyip `0xFFFF` değerinde saturate
  olur;
- configured stale timeout aşılınca `FRAME_VALID = 0` ve
  `channel_valid_mask = 0` yayımlanır;
- son ham kanal bellekte tutulsa bile valid mask olmadan kullanılamaz.

Kabul kanıtı:

1. geçerli SBUS akışında valid snapshot görülür;
2. receiver hattı kesilir;
3. en geç `stale_timeout + bir snapshot yayın periyodu` içinde FC03
   sonucunda valid bit ve mask kapanır;
4. gateway heartbeat ilerler, SBUS frame counter sabit kalır;
5. PLC aynı veriyi MANUAL adayı olarak reddeder.

## OI-007 — UART parity/framing/overflow hata görünürlüğü

**Durum:** Önemli uyumluluk bulgusu; firmware tanı entegrasyonu gerekli.

Güncel UART driver event queue olmadan kurulmuştur. Parser yapısal
header/footer/gap reddini sayar; fakat UART parity error, framing error,
buffer full ve FIFO overflow olayları `invalid_frame_count` veya
`DECODER_FAULT` durumuna bağlanmamıştır.

Gerekli davranış:

- UART hata olayları ayrı olarak gözlemlenir;
- parity/framing ile bozulan frame adayı kullanılabilir sayılmaz ve
  `invalid_frame_count` olay başına tanımlı biçimde artar;
- FIFO/buffer overflow parser'ı sıfırlar, kanal mask'ini kapatır ve decoder
  fault/tanı üretir;
- recovery eski kısmi frame'i veya eski kanalı yeni frame gibi yayınlamaz;
- bir fiziksel hata olayı birden çok sayaç artışı üretmeyecek şekilde
  belgelenir.

Kabul kanıtı UART hata enjeksiyonu veya eşdeğer driver-event testiyle
sunulmalıdır.
