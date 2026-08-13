# ROS / PLC Uyumluluk Kararları

Bu belge ESP32 G16 gateway firmware'inin `rover_core_ros2` ve S7-1200 PLC
uygulamasıyla aynı sözleşmeyi uygulaması için gerekli kararları sabitler.
Firmware kaynak kodunun nasıl yazılacağını tarif eder; donanım ölçümü
yapılmış gibi varsayım üretmez.

Karar statüleri:

- **FIXED:** protokol v1.1'in değişmez parçası;
- **FIRMWARE:** bu firmware deposunda uygulanacak davranış;
- **PLC-OWNED:** ESP tarafından yorumlanmayacak PLC kararı;
- **DEPLOYMENT:** araç/ağ kurulumu sırasında atanacak değer;
- **MEASUREMENT-GATE:** ölçüm yapılmadan donanım kodunda kalıcı seçilemez;
- **PC-EVIDENCE:** geliştirici PC'sinde doğrulanıp kayda geçirilecek bilgi.

## 1. Kaynak otoritesi ve sürüm

**FIXED**

Ana sözleşme `rover_core_ros2` deposudur:

- referans commit:
  `66f6bde8b457ff8ef04ed045184f519dc34e5ef2`;
- makine-okunur şema:
  `rover_hardware/config/plc_protocol_v1.yaml`;
- test vektörleri:
  `rover_hardware/config/plc_protocol_v1_test_vectors.yaml`;
- insan-okunur sözleşme:
  `docs/PLC_ORIN_G16_HABERLESME_SOZLESMESI.md`;
- gateway profili:
  `rover_hardware/config/g16_gateway.yaml`.

Uyumluluk sürümü:

| Alan | Değer |
|---|---:|
| protocol major | `1` |
| protocol minor | `1` |
| ESP block magic | `0x4547` |
| block length | 64 register |
| absolute base | `320 / 0x0140` |

Makine-okunur YAML sürüm ve offset otoritesidir. İnsan-okunur PLC sözleşmesinin
`ORIN_TO_PLC_COMMAND` tablosunda kalan `protocol_minor = 0` satırı eski
bir dokümantasyon değeridir; firmware bunu takip etmez. Protokol v1.1 için
wire değeri `1`'dir ve ana ROS deposunda ayrıca düzeltilmelidir.

Bu depoda protokol anlamı bağımsız değiştirilmez. Değişiklik sırası:

1. ana ROS sözleşmesi ve test vektörü;
2. PLC uygulaması;
3. ESP firmware;
4. entegrasyon testi.

## 2. Sistem içindeki rol

**FIXED**

```text
GR01/G16 --SBUS--> ESP32 gateway --Modbus TCP/FC03--> PLC
PLC --G16_STATUS + authority/state--> Orin/ROS 2
```

- ESP yalnız SBUS transport, decode, freshness ve ham kanal snapshot'ı üretir.
- PLC, ESP'nin Modbus TCP client'ıdır.
- Orin/ROS 2 ESP gateway'i hareket komutu kaynağı olarak kullanmaz.
- PLC ham kanalları doğrular, kalibre eder, anlamlandırır ve
  `G16_STATUS` bloğunda Orin'e yeniden yayınlar.
- ESP `joyRightX`, gaz yüzdesi, direksiyon yüzdesi, mode switch anlamı,
  endpoint, deadband veya neutral kalibrasyonu üretmez.
- ESP hareket yetkisi, AUTO/MANUAL kararı, controlled stop, safety reset veya
  aktüatör çıkışı üretmez.

ROS uyumluluğu ESP'nin ROS mesajı yayınlaması anlamına gelmez. Uyumluluk,
PLC'nin beklediği v1.1 snapshot'ını eksiksiz üretmesidir.

## 3. Wire kodlama

**FIXED**

- Modbus adresleri sıfır tabanlı holding-register offset'idir.
- Register içi byte sırası big-endian'dır.
- `uint32` alanlarda high word önce gelir.
- CRC, CRC-32/ISO-HDLC'dir:
  - polynomial normal: `0x04C11DB7`;
  - reflected implementation polynomial: `0xEDB88320`;
  - init: `0xFFFFFFFF`;
  - refin/refout: true;
  - xorout: `0xFFFFFFFF`;
  - check: `"123456789" -> 0xCBF43926`.
- CRC byte akışı absolute register 320-379 üzerinden, her register için high
  byte sonra low byte şeklindedir.
- CRC alanı 380-381, end sequence 382-383'tür ve CRC kapsamına girmez.
- Reserved register 362-379 daima sıfırdır ve CRC kapsamındadır.

Register offsetlerinin C kaynaklarındaki gösterimi 64-register blok içinde
relative olabilir; wire üzerindeki absolute adres relative offset + 320'dir.

## 4. Snapshot üretimi ve atomiklik

**FIRMWARE**

Firmware iki ayrı 64-register görüntü kullanır:

- `staging`: yalnız üretici görev tarafından yazılır;
- `active`: Modbus server tarafından yalnız okunur.

Her yayın çevriminde:

1. staging görüntüsü tamamen sıfırlanır;
2. header, `begin_sequence`, session, sayaçlar, zaman, flags ve kanallar
   yazılır;
3. reserved alanların sıfır olduğu doğrulanır;
4. absolute 320-379 eşdeğeri 60 register üzerinden CRC hesaplanır;
5. CRC 380-381'e, aynı sequence değeri CRC kapsamı dışındaki
   `end_sequence` 382-383'e yazılır;
6. tamamlanmış staging görüntüsü kısa bir kritik bölümde active yapılır.

Modbus callback'i yapım aşamasındaki staging görüntüsünü hiçbir zaman okumaz.
`begin_sequence == end_sequence` olması tek başına atomiklik yerine geçmez;
PLC ayrıca header, CRC, session ve freshness kontrollerini uygular.

Boot sonrası, ilk kullanılabilir SBUS frame gelmeden yayınlanan görüntü:

- doğru magic/version/length ve session içerir;
- `FRAME_VALID = 0`;
- `channel_valid_mask = 0`;
- `channel_raw[16] = 0`;
- `frame_age_ms = 0xFFFF`;
- geçerli CRC ve eşit begin/end sequence içerir.

## 5. Sayaç ve zaman semantiği

**FIRMWARE**

Tüm 32-bit sayaçlar modulo-`2^32` ilerler. Wrap normaldir; reset veya session
değişimi değildir.

| Alan | Kesin davranış |
|---|---|
| `gateway_heartbeat` | Her yeni active snapshot yayınında bir artar |
| `begin/end_sequence` | Her yeni active snapshot yayınında aynı yeni değere ilerler |
| `sbus_frame_counter` | Yalnız kullanılabilir yeni SBUS frame kabul edildiğinde bir artar |
| `gateway_monotonic_ms` | Snapshot hazırlanırken monotonic uptime'ın düşük 32 biti |
| `frame_period_us` | Son iki kullanılabilir SBUS frame arasındaki monotonic fark |
| `invalid_frame_count` | Reddedilen frame adayı başına bir artar |
| `frame_lost_count` | Yapısal olarak çözülen ve lost flag taşıyan frame başına bir artar |
| `failsafe_count` | Yapısal olarak çözülen ve failsafe flag taşıyan frame başına bir artar |

Bir frame ancak aşağıdakilerin tamamında **kullanılabilir** sayılır:

- UART aktarımında parity/framing/overflow hatası yok;
- 25-byte yapı, header ve kabul edilen footer doğru;
- 16 kanal eksiksiz decode edildi;
- SBUS frame-lost flag kapalı;
- SBUS receiver-failsafe flag kapalı.

Lost veya failsafe taşıyan yapısal frame ilgili hata sayacını artırır fakat
`sbus_frame_counter`, son kullanılabilir frame zamanı veya kanal snapshot'ını
ilerletmez.

Tek UART/frame olayının birden çok belirtisi varsa
`invalid_frame_count` aynı frame adayı için yalnız bir kez artırılır.
Lost ve failsafe sayaçları birbirinden bağımsızdır; aynı frame ikisini de
artırabilir.

`frame_age_ms`, son kullanılabilir frame'den snapshot zamanına kadar geçen
süredir ve `0xFFFF` değerinde saturate edilir. Henüz kullanılabilir frame
yoksa değer `0xFFFF`'tir.

Snapshot yayın periyodu protokol alanı değildir. Derleme/deployment
konfigürasyonunda açık bir değer olmalı ve PLC poll periyodundan bağımsız
tutulmalıdır. PLC bağlantı açık olsa bile heartbeat, frame counter ve age
kontrollerini birlikte uygular.

## 6. SBUS flag ve kanal geçerliliği

**FIXED + FIRMWARE**

| Bit | Alan | Firmware davranışı |
|---:|---|---|
| 0 | `FRAME_VALID` | kullanılabilir frame var, age limiti içinde ve decoder fault yok |
| 1 | `FRAME_LOST` | en son yapısal SBUS frame'in lost flag'i |
| 2 | `RECEIVER_FAILSAFE` | en son yapısal SBUS frame'in failsafe flag'i |
| 3 | `DECODER_ALIVE` | decoder görevi çalışıyor; tek başına kanal geçerliliği değildir |
| 4 | `DECODER_FAULT` | UART/parser/internal invariant fault aktif |
| 5 | `CHANNEL_CALIBRATION_VALID` | daima sıfır; kalibrasyon PLC'ye aittir |
| 6 | `LINK_QUALITY_VALID` | daima sıfır; v1.1 ESP bloğunda değer yoktur |
| 7 | `RSSI_VALID` | daima sıfır; v1.1 ESP bloğunda değer yoktur |
| 8-15 | reserved | daima sıfır |

`FRAME_VALID` yalnız aşağıdakilerin tamamında 1 olur:

- en az bir kullanılabilir SBUS frame kabul edildi;
- `frame_age_ms <= sbus_stale_timeout_ms`;
- lost ve failsafe aktif değil;
- decoder fault aktif değil.

`sbus_stale_timeout_ms` compile/deployment config değeridir. Üretim değeri
gerçek GR01 frame periyodu ve PLC HIL testi görülmeden safety-final sayılmaz.

Kanal alanları:

- `channel_count = 16`;
- `channel_valid_mask = 0xFFFF` yalnız `FRAME_VALID = 1` iken;
- diğer bütün durumlarda `channel_valid_mask = 0`;
- `channel_raw` yalnız 11-bit ham değer içerir;
- boot'ta kanallar sıfırdır;
- stale/lost/failsafe sırasında son kullanılabilir ham değer bellekte
  tutulabilir, ancak mask sıfır olmadan hiçbir zaman yayınlanamaz.

PLC kanal değerini yalnız `FRAME_VALID`, mask, snapshot/CRC, session,
heartbeat, frame counter ve kendi timeout kontrolleri birlikte geçerliyse
kullanır.

## 7. SBUS parser sınırı

**FIXED**

- RX-only GPIO: `35`;
- baud: `100000`;
- data/parity/stop: `8E2`;
- frame: 25 byte;
- ham kanal: 16 x 11 bit;
- ESP TX kullanılmaz;
- SBUS dijital kanal 17/18 bu v1.1 wire sözleşmesine taşınmaz.

**MEASUREMENT-GATE**

Aşağıdakiler GR01 capture/ölçüm kanıtı olmadan kalıcı sabit yapılmaz:

- RX inversion;
- elektriksel idle polaritesi;
- kabul edilen footer allowlist;
- byte-gap resync eşiği;
- gerçek normal/fast frame periyodu.

Parser start/header doğrulaması yapar. Footer politikası “her değeri kabul et”
olamaz; GR01'den yakalanmış geçerli frameler ve kullanılan SBUS varyantı
belgelendikten sonra allowlist olarak sabitlenir.

Gürültü veya gap sonrası parser eski kanal değerini yeni frame gibi
işaretlemez. Tam ve kullanılabilir yeni frame oluşana kadar valid flag ve mask
kapalı kalır.

## 8. Session ve reboot davranışı

**FIRMWARE**

- Her boot/watchdog resetinde sıfır olmayan yeni `gateway_session_id`
  üretilir.
- Session, en az cihazın benzersiz kimliği, kalıcı boot counter ve donanım
  random değerini birleştiren tanımlı bir üretim fonksiyonundan gelir.
- Kalıcı boot counter boot başına yalnız bir kez güncellenir.
- Session üretimi veya kalıcı sayaç erişimi başarısızsa decoder fault açılır;
  kanal snapshot'ı valid ilan edilmez.
- Önceki boot'tan kanal, counter veya active snapshot tekrar kullanılmaz.
- PLC session değişiminde MANUAL neutral/re-arm handshake ister.

Session kimliği güvenlik anahtarı veya kimlik doğrulama değildir; reboot ve
stale cache ayrımı içindir.

## 9. Modbus TCP server davranışı

**FIXED + FIRMWARE**

- transport: kablolu Ethernet;
- Modbus Unit ID: `1`;
- izin verilen veri isteği: yalnız `FC03 Read Holding Registers`;
- sunulan aralık: absolute holding-register offset `320-383`;
- tam 64-register snapshot tek FC03 isteğinde okunabilir;
- aralık dışına taşan veya sunulmayan register isteyen okuma
  `Illegal Data Address` ile reddedilir;
- FC06, FC16, FC23 ve bütün yazma istekleri reddedilir;
- safety reset, config write, counter reset veya hareket komutu yoktur;
- Wi-Fi manuel kontrol yolunda kullanılmaz;
- TCP client bağlı bilgisi freshness kanıtı değildir.

Modbus kütüphanesi register descriptor'larını varsayılan olarak yazılabilir
sunuyorsa uygulama katmanı yazma function code'larını açıkça reddetmelidir.
Yalnız “kodumuz yazmıyor” yaklaşımı salt-okunur sözleşmesini sağlamaz.

## 10. PLC ve deployment sahipliğindeki değerler

**PLC-OWNED**

ESP aşağıdakileri içermez veya yorumlamaz:

- G16 kanal numarası -> gaz/fren/direksiyon/mode eşlemesi;
- endpoint, reverse, center, deadband ve neutral değerleri;
- MANUAL talep ve neutral handshake süresi;
- PLC'nin ESP poll periyodu ve response timeout'u;
- AUTO/MANUAL authority state machine;
- controlled-stop profili;
- aktüatör limitleri ve safety interlock'ları.

**DEPLOYMENT**

- PLC ve ESP statik IP/subnet/VLAN değerleri;
- izin verilen PLC client IP'si;
- snapshot yayın periyodu;
- geliştirici log seviyesi;
- üretim watchdog ayarları.

Bu değerlerin ortak otoritesi ileride deployment config olmalıdır. Aynı değer
ESP ve PLC README'lerinde ayrı ayrı elle değiştirilmez.

## 11. Donanım bağlantı kapıları

**MEASUREMENT-GATE**

Aşağıdakiler kapanmadan UART/Ethernet donanım bağlantısı production-ready
sayılmaz:

1. GR01 SBUS high/low voltajının osiloskop ölçümü;
2. idle polaritesi ve inversion ölçümü;
3. ESP32-ETH01 v1.4 kartının kesin PHY, clock, pin ve flash varyantı;
4. pasif Y yerine kullanılacak aktif çift çıkışlı buffer/izolatör şeması;
5. her dal için seri direnç, giriş koruması ve ortak referans/isolation kararı;
6. GR01 gerçek SBUS frame capture'ı ve footer/frame-period kaydı.

Ölçüm gelmeden saf decoder, freshness ve register builder geliştirilebilir.
Varsayılan inversion veya internetten bulunan başka kart pinout'u gerçek
donanıma uygulanmaz.

## 12. PC'de kaydedilecek build kanıtı

**PC-EVIDENCE**

ESP-IDF, PlatformIO ve karta yükleme bu VM'de çalıştırılmaz. Geliştirici
PC'sindeki doğrulama kaydı en az şunları içerir:

- commit SHA;
- `pio --version`;
- PlatformIO `espressif32` platform sürümü;
- gerçek ESP-IDF sürümü;
- compiler sürümü;
- board/environment adı;
- clean build sonucu;
- binary boyutları;
- kullanılan `sdkconfig` farkı;
- karta yükleme yapıldıysa kart revizyonu ve seri/log kanıtı.

`platformio.ini` içindeki platform pini tek başına bütün toolchain kanıtı
değildir. Başarılı PC build'inde çözülen gerçek framework ve paket sürümleri
PR açıklamasına veya sürümlü build-evidence belgesine yazılır.

## 13. Zorunlu ortak testler

Firmware merge/entegrasyon kabulü için:

1. `"123456789" -> 0xCBF43926` CRC kontrolü;
2. uint32 `0x12345678 -> [0x1234, 0x5678]`;
3. 16 kanal için bilinen SBUS frame decode vektörü;
4. min/center/max 11-bit kanal sınırları;
5. bad header/footer, eksik frame, UART error ve gap resync;
6. lost/failsafe ve frozen-frame;
7. boot/session değişimi;
8. reserved-zero kontrolü;
9. tam 64-register ESP snapshot ve beklenen CRC;
10. Modbus FC03 doğru aralık;
11. yazma function code ve aralık dışı okuma reddi;
12. PLC'nin stale/session/bad-CRC snapshot'ı reddetmesi.

Ana ROS test vektörü dosyasında henüz tam ESP snapshot known-result vektörü
bulunmamaktadır. Bu vektör önce ana sözleşmeye eklenmeli, sonra firmware ve
PLC testlerinde aynı veri kullanılmalıdır.

## 14. Değişiklik kontrol listesi

Her firmware PR'ında:

- [ ] protokol major/minor ve kaynak commit kaydedildi;
- [ ] wire offset veya anlam bağımsız değiştirilmedi;
- [ ] reserved alanlar sıfır;
- [ ] ESP'de kanal kalibrasyonu/authority/motion logic eklenmedi;
- [ ] yeni hata yolu valid flag ve mask'i güvenli kapatıyor;
- [ ] ilgili saf C testleri güncellendi;
- [ ] PC firmware build kanıtı ayrı raporlandı;
- [ ] donanım gerektiren sonuç, çalıştırılmadıysa açıkça belirtildi;
- [ ] PLC/ROS uyumluluğunu etkileyen değişiklik ana sözleşmeye işlendi.

## 15. Proje kimliği, ihtiyaçların kaynağı ve denetim rolü

### Bu bileşenin kimliği

Bu depo rover'ın ESP32-ETH01 G16 gateway bileşenidir. Görevi SKYDROID
GR01/G16 alıcısından gelen SBUS verisini yapısal olarak decode etmek,
freshness ve hata durumunu üretmek ve ham kanal snapshot'ını kablolu Ethernet
üzerinden PLC'ye salt-okunur Modbus TCP verisi olarak sunmaktır.

Bu bileşen:

- ROS 2 node'u veya görev planlayıcısı değildir;
- kanal kalibrasyonu, gaz/fren/direksiyon anlamlandırması yapmaz;
- MANUAL/AUTO authority kararı vermez;
- controlled stop veya safety fonksiyonu uygulamaz;
- PLC ya da aktüatör çıkışı sürmez.

### Kuralların sistem gerekçesi

ESP, PLC ve Orin/ROS 2 ayrı depolarda ve ayrı geliştirme ortamlarında yazılır.
CRC, endian, offset, session, freshness, flag veya sayaç anlamlarından yalnız
birinin farklı uygulanması PLC'nin geçersiz SBUS verisini fresh/manual komut
olarak yorumlamasına neden olabilir. Bu belge ESP firmware'inin ana rover
sistemiyle aynı wire ve güvenlik sınırını uygulaması için gereken ortak
sözleşmedir.

### İsteklerin ve kararların kaynağı

Ana sistem ve sözleşme kaynağı:
[kzlslngl/rover-core-ros2](https://github.com/kzlslngl/rover-core-ros2)

Bu belgenin ilk referansı:
`66f6bde8b457ff8ef04ed045184f519dc34e5ef2`

Ana Proje Codex'inin bu incelemede doğruladığı güncel
`rover-core-ros2/main` referansı:
`a464881182189243e27e413fd4b0d136ed3a5322`

Talepler özellikle şu ana proje kaynaklarından gelir:

- `rover_hardware/config/plc_protocol_v1.yaml`;
- `rover_hardware/config/plc_protocol_v1_test_vectors.yaml`;
- `rover_hardware/config/g16_gateway.yaml`;
- `docs/PLC_ORIN_G16_HABERLESME_SOZLESMESI.md`;
- `docs/ESP32_ETH01_G16_GATEWAY_UYGULAMA_NOTU.md`;
- `docs/SISTEM_GEREKSINIMLERI_VE_KARARLAR.md`.

Bu ESP deposu uygulama deposudur; wire protokolün bağımsız otoritesi değildir.
Bu belge ile ana ROS şeması çelişirse geliştirici sessizce birini seçmez:
çelişki kaydedilir, ana sözleşme düzeltilir/sürümlenir ve ardından ESP
uygulaması güncellenir.

### Belgeyi hazırlayan Ana Proje Codex'i denetim rolü

Bu belge **Ana Proje Codex'i** tarafından, **rover sistem uyumluluğu ve
güvenlik mimarisi denetçisi** rolünde yapılan inceleme sonucunda
hazırlanmıştır. Buradaki "Codex" ifadesi ESP firmware geliştirme Codex'ini
değil, `rover-core-ros2` ana projesine bağlı üst seviye denetim otoritesini
ifade eder.

Ana Proje Codex'i:

- firmware C/C++, CMake, PlatformIO veya ESP-IDF kodunu yazmaz/değiştirmez;
- ESP-IDF/PlatformIO build veya karta yükleme çalıştırmaz;
- mevcut kodu ve Git diff'lerini salt-okunur inceler;
- ana ROS/PLC sözleşmesine aykırı, belirsiz veya kanıtsız yapıları tespit eder;
- bulguları ve gerekli kabul kriterlerini yalnız Markdown belgeleriyle
  geliştiriciye aktarır;
- düzeltmenin nasıl uygulanacağına mimari yön verir, uygulamayı firmware
  geliştirme projesine bırakır.

Bu not bir otomatik onay değildir. Firmware uygunluğu ancak ilgili commit,
PC build kanıtı, host testleri, register test vektörleri, masa testi ve
donanım ölçüm kapıları yeniden incelendikten sonra kabul edilir.

### ESP Firmware Codex'i uygulama rolü ve güncel durum

Bu depoda çalışan **ESP Firmware Codex'i**, Ana Proje Codex'inden ayrı bir
uygulama rolüdür. Firmware C/C++ kaynaklarını, CMake/PlatformIO yapılandırmasını
ve testleri değiştirme; yerel build/test çalıştırma ve doğrulanmış değişiklikleri
Git üzerinden yayınlama yetkisine sahiptir.

ESP Firmware Codex'i:

- Ana Proje Codex'i tarafından eklenen karar ve uyumluluk belgelerini inceler;
- ana ROS/PLC sözleşmesine, protokol v1.1'e ve bu belgedeki güvenlik sınırına
  uygun firmware geliştirir;
- sözleşmeyle çelişen veya güvenlik açısından belirsiz bir talebi sessizce
  uygulamaz; çelişkiyi kaydeder ve ana otoriteye geri bildirir;
- firmware build, host test, statik kontrol ve donanım testi kanıtlarını ayrı
  raporlar;
- donanım ölçüm kapıları kapanmadan inversion, PHY/pin veya elektriksel profil
  gibi değerleri production varsayımı olarak sabitlemez.

**Güncel geliştirme aşaması:** Aşama 2 tamamlandı; Aşama 3 masa/PLC/HIL
entegrasyonuna hazır.

Tamamlanan temel:

- minimal ESP-IDF/PlatformIO proje iskeleti;
- protokol sabitleri ve compile-time register yerleşimi kontrolleri;
- CRC-32/ISO-HDLC ve big-endian register yardımcıları;
- saf SBUS decoder ve byte-gap/gürültü sonrası resync parser'ı;
- freshness, hata sayaçları ve stale/valid durum üretimi;
- 64-register staging snapshot builder, heartbeat/sequence ve CRC üretimi;
- CRC/endian/SBUS/freshness/snapshot host testleri;
- geliştirici PC'sinde başarılı ESP32 firmware build doğrulaması.

11 Ağustos 2026 donanım doğrulaması:

- SKYDROID GR01 SBUS çıkışı WT32-ETH01 GPIO35'e doğrudan bağlandı;
- UART2 üzerinde `100000 8E2` ve RX inversion aktif olarak kararlı frame alındı;
- gerçek alıcı footer değeri `0x00`, merkez kanal değeri yaklaşık `1002`,
  gözlenen kanal hareket aralığı yaklaşık `282..1722` olarak doğrulandı;
- test süresince frame-lost ve failsafe bitleri sıfır kaldı;
- hedef kartın fiziksel flash kapasitesi 2 MB olarak algılandı ve build/upload
  ayarı buna göre sabitlendi.

13 Ağustos 2026 çalışma zamanı doğrulaması:

- gerçek SBUS kareleri freshness ve 64-register snapshot hattına bağlandı;
- `VALID | ALIVE`, frame age, filtrelenmiş frame period, sequence ve CRC alanları
  kart üzerinde canlı veriyle doğrulandı;
- GR01 için kabul edilen frame-period ölçüm penceresi `5000..20000 us` olarak
  sabitlendi; UART görev/log gecikmesi kaynaklı aykırı örnekler reddedilir;
- NVS boot sayacı ardışık resetlerde 2'den 3'e ilerledi, session ID değişti ve
  snapshot sequence her boot'ta yeniden 1'den başladı.
- LAN8720 RMII linki ve `192.168.144.166/24` masa profili gerçek kartta
  doğrulandı;
- salt-okunur Modbus TCP sunucusu FC03 ile 64 register döndürdü; begin/end
  sequence ve CRC doğrulandı, FC06 yazma isteği reddedildi;
- atomik snapshot kopyası, Ethernet/Modbus tanı sayaçları, iki saniyelik
  sessiz-client timeout'u ve SBUS task-watchdog gözetimi doğrulandı.

Sıradaki eksikler, özetle:

1. ana sözleşmeden tam 64-register known-result test vektörü ve CRC/sequence
   sırası onayı;
2. FC03 alt-aralık politikasının ve production deployment değerlerinin ana
   proje tarafından kesinleştirilmesi;
3. UART hata olayları, biçim/statik analiz ve dinamik bellek/bloklama denetimi;
4. lost/failsafe/stale/reboot/Ethernet-kopma hata enjeksiyonları;
5. PLC/HIL üzerinde session/CRC/stale reddi, neutral/re-arm ve manuel yol
   entegrasyon testleri;
6. production öncesi elektriksel ölçümler ve aktif buffer/izolasyon kararı.

Ayrıntılı ve güncel iş listesi için `ROADMAP.md` kullanılır. Ana Proje Codex'i
sonraki denetimlerde bu bölüm, `ROADMAP.md`, Git diff'i ve build/test kanıtlarını
birlikte değerlendirir.
