# Geliştirme Yol Haritası

Bu liste `README.md` içindeki güvenlik sınırını ve protokol `1.1` sözleşmesini
uygulanabilir geliştirme adımlarına böler. Register yerleşimi bu depoda
bağımsız değiştirilmez.

## Tamamlandı

- [x] Minimal ESP-IDF ve PlatformIO proje iskeleti
- [x] Protokol sürümü, register offsetleri ve compile-time boyut kontrolleri
- [x] CRC-32/ISO-HDLC byte ve big-endian register uygulaması
- [x] 32-bit high-word-first register okuma/yazma yardımcıları
- [x] CRC/endian için donanımdan bağımsız C test kaynakları
- [x] ESP32 firmware derleme doğrulaması

## Aşama 1 — Kart gerektirmeyen güvenli çekirdek

### SBUS decoder

- [x] 25-byte SBUS frame yapısal doğrulaması
- [x] 16 adet 11-bit kanalın saf fonksiyonla decode edilmesi
- [x] Frame-lost ve failsafe bitlerinin ayrıştırılması
- [x] Geçersiz header/footer ve eksik frame reddi
- [x] Gürültü ve byte-gap sonrasında yeniden senkronizasyon
- [x] Bilinen frame ve sınır kanal değerleri testleri

### Freshness ve tanılar

- [x] Son geçerli frame zamanının monotonic saatle izlenmesi
- [x] `frame_age_ms` değerinin `uint16` aralığında saturasyonu
- [x] Alive/stale/fault durum geçişleri
- [x] Geçerli, invalid, lost ve failsafe sayaçları
- [x] 32-bit sayaç taşmalarının tanımlı davranışı
- [x] Frozen-frame ve zaman taşması testleri

### Register snapshot

- [x] 64-register staging görüntüsünün sıfırdan kurulması
- [x] Reserved registerların daima sıfır tutulması
- [ ] Boot başına değişen `gateway_session_id`
- [x] Heartbeat ve sequence yönetimi
- [x] Register 320–379 üzerinden CRC üretimi
- [ ] Aynı begin/end sequence ile atomik aktif görüntü değişimi
- [ ] Protokol test vektörleriyle birebir karşılaştırma
- [ ] Reboot, eski snapshot ve bozuk CRC testleri

### Host doğrulama altyapısı

- [x] Windows için native C derleyicisinin kurulması veya CI ortamının eklenmesi
- [x] `test/core_tests.c` testlerinin host üzerinde çalıştırılması
- [x] SBUS, freshness ve snapshot testlerinin host test hedefine eklenmesi
- [ ] Biçim ve statik analiz komutlarının belgelenmesi
- [x] GitHub Actions üzerinde host test ve firmware build kontrolü

## Aşama 2 — ESP32 çevre birimleri

Bu aşamaya Aşama 1 testleri geçmeden başlanmaz.

- [x] GPIO35 RX-only UART, 100000 baud ve 8E2 adaptörü
- [x] GR01 üzerinde doğrulanmış inverted RX yapılandırması
- [ ] UART hata ve byte-gap tanıları
- [ ] Kesin kart varyantına uygun Ethernet PHY/pin yapılandırması
- [ ] Statik/DHCP ağ ayarlarının yapılandırılabilir tutulması
- [ ] Yalnız FC03 ve yalnız 64-register bloğunu sunan Modbus TCP sunucusu
- [ ] Tüm Modbus yazma fonksiyonlarının reddedilmesi
- [ ] Client, Ethernet ve watchdog tanıları
- [ ] Çalışma yolunda dinamik bellek ve uzun bloklayan çağrı denetimi

## Aşama 3 — Masa ve entegrasyon testleri

- [ ] Bilinen SBUS frame'lerinden 16 kanal decode testi
- [ ] Lost, failsafe, frozen-frame ve sayaç anomalisi enjeksiyonu
- [ ] Reboot/session değişimi ve eski snapshot reddi
- [ ] CRC/endian sonuçlarının ana ROS sözleşmesiyle karşılaştırılması
- [ ] Ethernet kopması ve PLC poll kesilmesi
- [ ] Orin kapalıyken G16 → ESP → PLC manuel yol testi
- [ ] ESP arızasında stale verinin aktüatörlere uygulanmadığının doğrulanması

## Donanım kodundan önce gerekli bilgiler

- [ ] GR01 SBUS high/low voltajı ve idle polaritesi osiloskop ölçümü
- [ ] Aktif çift çıkışlı buffer veya izolatör şeması
- [ ] ESP32-ETH01 v1.4 kesin PHY, pin ve flash varyantı
- [ ] PLC ve ESP IP/subnet değerleri
- [ ] Modbus poll periyodu ve timeout değerleri
- [ ] PLC tarafındaki G16 kanal eşleme ve neutral kalibrasyon sürümü

## Sıradaki geliştirme dilimi

Bir sonraki değişiklik yalnızca saf SBUS decoder ve host testlerini kapsamalıdır.
UART, Ethernet ve Modbus kodu bu decoder testleri geçmeden eklenmemelidir.
