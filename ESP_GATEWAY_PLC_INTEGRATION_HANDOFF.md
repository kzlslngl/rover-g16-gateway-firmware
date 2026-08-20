# ESP G16 Gateway - PLC Integration Handoff

Son güncelleme: 2026-08-20

Bu belge, `rover-g16-gateway-firmware` ile S7-1200 PLC arasındaki doğrulanmış
entegrasyon profilini ve bekleyen ortak testleri kaydeder. ESP gateway ham G16
verisini ve haberleşme sağlık bilgisini yayımlar. Nihai hareket izni, authority,
controlled-stop ve safety kararları PLC'ye aittir.

## Bench ağı ve Modbus profili

| Alan | Değer |
|---|---|
| PLC | `192.168.2.100/24` |
| ESP gateway | `192.168.2.166/24` |
| Firmware'de kayıtlı IPv4 gateway | `192.168.2.241` |
| TCP port | `502` |
| Modbus Unit ID | `1` |
| PLC işlemi | `FC03` read holding registers |
| ESP register offset | `320` |
| Register sayısı | `64` |
| Siemens `MB_DATA_ADDR` | `40321` |

Bu IP adresleri bench değerleridir; production IP planı değildir. Firmware'de
kayıtlı `192.168.2.241`, önceki PC bench adresidir; PLC adresi veya doğrulanmış
gerçek ağ geçidi değildir. PLC ile ESP aynı `/24` subnet içinde olduğu için
mevcut haberleşme gateway kullanmadan doğrudan gerçekleşir ve bu eski değer
bench iletişimini bozmaz. Production ağının gerçek default gateway değeri ayrıca
belirlenmelidir.

PLC `MB_CLIENT.REQ` girişini sürekli `TRUE` tutar. Sabit periyotlu ayrı bir poll
timer'ı yoktur; bir işlem tamamlandıkça sıradaki istek başlar. Canlı testte
`FrameAgeMs` çoğunlukla `0 ms`, zaman zaman `4 ms` ölçülmüştür.

PLC doğrulama parametreleri:

```text
MaxFrameAgeMs            = 100 ms
PollTimeout              = T#500ms
RequiredHealthySnapshots = 3
```

`RequiredHealthySnapshots` sayılırken üç farklı ve ilerleyen snapshot sequence
değeri görülmelidir. PLC, ESP'nin yaklaşık `20 ms` yayın periyodundan daha hızlı
poll yaparsa aynı snapshot birden fazla kez okunabilir; aynı sequence değerinin
tekrar okunması yeni sağlıklı snapshot olarak sayılmaz.

PLC authority zamanları:

```text
ManualNeutralHoldTime = T#500ms
AutoG16GraceTime      = T#2s
```

## Canlı doğrulanan protokol davranışı

```text
Magic                  = 16#4547
Protocol               = 1.1
CRC                    = geçerli
Begin/end sequence     = eşleşiyor
Session ID             = okunuyor
Heartbeat/sequence     = ilerliyor
Canlı SBUS flags       = 16#0009
FC06 write             = reddediliyor
```

ESP her yeniden başlangıçta yeni bir `SessionId` üretmelidir. Aynı session
içinde gateway heartbeat ve snapshot sequence, SBUS frame gelmese bile yaklaşık
`20 ms` aralıklarla ilerler. `sbus_frame_counter` ise yalnız yeni kullanılabilir
SBUS frame alındığında ilerler. Böylece PLC, çalışan gateway ile kesilmiş veya
geçersiz G16/SBUS veri kaynağını birbirinden ayırabilir.

PLC session, snapshot sequence, gateway heartbeat, CRC, flags, channel-valid
mask ve frame-age alanlarını doğrular. `sbus_frame_counter` diagnostics için
kaydedilir; mevcut validator yeni SBUS frame kararını bu sayacı tek başına
karşılaştırarak vermez. Duplicate/frozen gateway snapshot ise freshness
watchdog ile geçersizleşir.

## Doğrulanan G16 kanal eşlemesi

| Kanal | İşlev | Ölçülen değer/aralık |
|---:|---|---|
| 0 | Joy X2 | `282 / 1002 / 1722` |
| 1 | Joy Y2 | `282 / 1002 / 1722` |
| 2 | Joy Y1 | `282 / 1002 / 1722` |
| 3 | Joy X1 | `282 / 1002 / 1722` |
| 4 | SW1, üç konum | `282 / 1002 / 1722` |
| 5 | SW2, üç konum | `282 / 1002 / 1722` |
| 6 | SW3, üç konum | `282 / 1002 / 1722` |
| 7 | SW4, üç konum | `282 / 1002 / 1722` |
| 8 | A-B-C-D-E-F selector | aşağıdaki tablo |
| 9 | MOD, iki konum | `282 / 1722` |
| 10 | AUX1 pot | `282..1722` |
| 11 | AUX2 pot | `282..1722` |
| 12 | Joy X3 | `282 / 1002 / 1722` |
| 13 | Joy Y3 | `282 / 1002 / 1722` |
| 14 | Kullanılmıyor/bilinmiyor | - |
| 15 | Kullanılmıyor/bilinmiyor | - |

CH8 selector değerleri:

| Seçim | Ham değer |
|---|---:|
| A | `426` |
| B | `685` |
| C | `858` |
| D | `1074` |
| E | `1290` |
| F | `1578` |

A-F seçimi kalıcı olduğu için şu anda araç kontrol işlevine bağlanmamıştır.

`AxisDeadband` ve `SwitchTolerance` PLC decoder FB girişleridir. Kesin runtime
değerleri TIA çağrısından ayrıca kaydedilecektir. ESP bu toleransları uygulamaz;
ham kanal değerini kayıpsız yayımlar.

## PLC tarafındaki operatör anlamları

```text
MOD = OperatorEnable

SW1:
-1 = Controlled stop
 0 = Manual G16
+1 = Orin AUTO

SW2:
-1 = Sol Y1 throttle, sağ X2 steering
 0 = Geçersiz/disabled layout
+1 = Sağ Y2 throttle, sol X1 steering

AUX1 = Manual speed scale
AUX2 = Steering response/rate
```

SW3, SW4, A-F selector ve Joy3 mevcut kontrol sürümünde kullanılmaz; ham
değerleri protokolde yayımlanmaya devam eder.

## PLC güvenlik ve authority davranışı

- MANUAL sırasında G16 geçersizleşirse PLC gecikmeden controlled-stop ister.
- AUTO sırasında Orin validity sürekli zorunludur.
- AUTO sırasında G16 kaybı yalnız `T#2s` grace süresince tolere edilir.
- Manuel yeniden yetkilendirme için geçerli G16, operator enable ve kesintisiz
  `T#500ms` neutral hold gerekir.
- Modbus TCP ve ESP gateway safety-rated kabul edilmez.
- Gerçek E-stop, safety relay/STO/kontaktör zinciri ESP ve Modbus'tan bağımsızdır.
- ESP safety state machine veya aktüatör komutu uygulamaz; yalnız doğrulanabilir
  gateway verisi ve açık diagnostics üretir.

## ESP tarafında korunması gereken davranışlar

1. Ham SBUS kanal değerlerini ek clamp/ölçek uygulamadan yayımla.
2. Yeni kullanılabilir SBUS frame alınmadığında `FRAME_VALID=0` ve
   `channel_valid_mask=0` yayımla; `frame_age_ms` değerini ilerlet.
3. `sbus_frame_counter` değerini yalnız gerçek yeni kullanılabilir SBUS frame
   alındığında ilerlet.
4. Gateway heartbeat ve snapshot sequence değerlerini SBUS'tan bağımsız olarak
   yaklaşık `20 ms` aralıklarla ilerlet.
5. `FRAME_LOST` ve `FAILSAFE` bitlerini yalnız alınan GR01 SBUS frame'i bu
   bitleri içeriyorsa yayımla. Tam kablo kopmasında bu bitleri zorunlu kabul
   etme veya sentetik olarak üretme.
6. Aynı snapshot yazılırken begin/end sequence bütünlüğünü koru.
7. CRC-32/ISO-HDLC'yi sözleşmedeki byte sırasıyla hesapla.
8. Boot/reset sonrası yeni session kimliği üret.
9. Rezerve register'ları sıfır tut.
10. PLC'nin kullandığı G16 alanını read-only tut; yazma fonksiyonlarını reddet.
11. Ethernet istemcisinin bağlantısı gateway'in SBUS health durumunu değiştirmesin.
12. Watchdog/frozen-frame durumu geçerli yeni kumanda gibi gösterilmesin.

## Bekleyen ortak fiziksel testler

- [ ] Kumanda kapatıldığında frame-valid, channel mask, frame-age ve PLC
      validity davranışı; GR01 frame gönderiyorsa lost/failsafe flags
- [ ] SBUS kablosu çıkarıldığında `FRAME_VALID=0`, `channel_valid_mask=0`,
      artan `frame_age_ms`, duran `sbus_frame_counter` ve ilerleyen gateway
      heartbeat/snapshot sequence
- [ ] ESP resetinde `SessionId` değişimi
- [ ] ESP resetinden sonra PLC neutral/re-arm davranışı
- [ ] Ethernet kablosu çıkarma, timeout ve yeniden bağlantı
- [ ] Frozen sequence/heartbeat enjeksiyonu ve PLC reddi
- [ ] Failsafe/lost flag enjeksiyonu ve PLC reddi
- [ ] Eski frame / `MaxFrameAgeMs` reddi
- [ ] CRC bozulması ve torn snapshot reddi
- [ ] Kumanda geri geldiğinde üç sağlıklı snapshot ve neutral hold davranışı

Her testte en az şu değerler kaydedilmelidir:

```text
ESP session ID
ESP sequence/heartbeat
ESP SBUS flags
ESP frame age
PLC DataValid
PLC RejectBits
PLC authority state
PLC transition reason
PLC controlled-stop durumu
Bağlantının kesilme ve geri gelme süreleri
```

## Kabul kriteri

Hiçbir bağlantı/reset/failsafe senaryosunda eski veya geçersiz G16/SBUS verisi
PLC tarafında geçerli yeni komut olarak kabul edilmemelidir. Gateway heartbeat
ve snapshot sequence ilerlemeye devam etse bile `FRAME_VALID=0`, sıfır channel
mask veya aşılmış frame-age hareket komutunu geçersiz kılmalıdır. MANUAL yetkisi
ancak haberleşme ve SBUS verisi yeniden sağlıklı olduktan ve neutral/re-arm
koşulları tamamlandıktan sonra geri verilmelidir.
