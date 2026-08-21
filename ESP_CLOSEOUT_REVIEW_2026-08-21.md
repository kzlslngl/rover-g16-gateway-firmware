# ESP G16 Gateway Kapanış Denetimi — 21 Ağustos 2026

Bu belge Ana Proje Codex'i tarafından, ESP firmware deposunun işlevsel
geliştirme kapsamını kapatmadan önce yapılan salt-okunur kod/GitHub denetimini
kaydeder. Firmware C kaynakları, build ayarları ve testleri bu denetimde
değiştirilmemiştir.

İncelenen kaynaklar:

- ESP dalı: `agent/phase1-foundation@404af0e636a8ccdd0b3c3011b69eb41ca6208702`
- Ana ROS/PLC sözleşme referansı:
  `rover-core-ros2/agent/local-metric-map@854fee6f67ad3cb37f526c84fbcf79cdcacf10fa`
- GitHub PR: `#1 Add verified Ethernet, Modbus TCP and runtime supervision`
- Fiziksel entegrasyon özeti: `ESP_GATEWAY_PLC_INTEGRATION_HANDOFF.md`

## 1. Kapanış kararı

ESP gateway'in hedeflenen **işlevsel firmware kapsamı tamamlanmıştır**:

- GR01/G16 SBUS verisi gerçek kartta alınır ve 16 ham kanal decode edilir;
- freshness, stale, lost/failsafe ve UART fault durumları fail-closed üretilir;
- heartbeat/snapshot sequence SBUS'tan bağımsız ilerler;
- `sbus_frame_counter` yalnız kullanılabilir yeni SBUS frame ile ilerler;
- 64-register protokol `1.1` görüntüsü reserved-zero, CRC ve eş begin/end
  sequence ile oluşturulur;
- ESP rebootunda yeni ve sıfırdan farklı session üretilir;
- Modbus TCP yalnız `FC03`, offset `320`, uzunluk `64` okumasını kabul eder;
- yazma fonksiyonları ve farklı adres/uzunluklar reddedilir;
- Ethernet ve sessiz istemci recovery davranışı ile runtime watchdog/tanılar
  uygulanmıştır;
- ana sözleşmedeki tam ESP known-result vektörü kaynak testinde birebir yer
  almaktadır (`CRC32 = 0x12749618`).

Kapanış denetiminde belirlenen iki depo kapısından CI düzeltmesi tamamlanmıştır:

1. GitHub `host-tests` işi commit `9480495` ile Debug/test profilinde assertion
   kontrolleri etkin tutularak yeşile çevrilmiştir.
2. Push ve PR koşularında `host-tests` ile `esp32-build` birlikte yeşildir;
   draft PR `#1` `main` dalına birleşmeye hazırdır.

Bu iki işlemden sonra ESP işi **bakım/commissioning moduna** alınabilir. Daha
sonraki olağan değişiklikler production IP/gateway/VLAN, yapılandırma değeri,
ESP-IDF bağımlılık bakımı veya doğrulanmış donanım profilindeki zorunlu bir
düzeltmeyle sınırlı olmalıdır. Register anlamı ESP deposunda bağımsız
değiştirilmemelidir.

## 2. Zorunlu CI düzeltmesi

GitHub Actions'ta commit `404af0e` için:

| İş | Sonuç |
|---|---|
| `esp32-build` | Başarılı |
| `host-tests` | Başarılı — commit `9480495` |

Önceki `host-tests`, CMake'i `-DCMAKE_BUILD_TYPE=Release` ile yapılandırmaktaydı.
Testler `<assert.h>` içindeki `assert(...)` ifadelerine dayandığı için Release
derlemesinde `NDEBUG` bu kontrolleri kaldırır. Böylece assertion içinde
kullanılan değişkenler `-Werror` altında `unused-variable` hatasına dönüşür ve
test binary'si oluşturulamaz. Daha önemlisi, yalnız uyarıları susturmak
assertion'ları geri getirmez; testler kontrol yapmadan geçebilir.

ESP Firmware Codex'i aşağıdaki iki güvenli çözümden birini uygulamalıdır:

- host testlerini assertions etkin kalacak bir Debug/test profiliyle derlemek;
  veya
- testlerde Release/NDEBUG'den etkilenmeyen, hata durumunda dosya/satır yazıp
  non-zero dönen kalıcı bir `CHECK` mekanizması kullanmak.

Çözüm olarak host test CI profili `Debug` yapılmış, assertion kontrolleri etkin
tutulmuştur. Hem push hem PR GitHub Actions çalıştırmasında `host-tests` ve
`esp32-build` birlikte yeşildir; uyarılar cast ile susturulmamıştır.

## 3. Fiziksel doğrulama durumu

Depoda kayıtlı kanıtlar:

- gerçek GR01 frame alımı, inverted `100000 8E2` UART ve canlı kanal hareketi;
- canlı snapshot sequence/heartbeat/CRC;
- FC03 tam snapshot ve Modbus yazma reddi;
- ESP rebootunda session değişimi;
- SBUS hattı kesildiğinde age artışı, valid/mask kapanması ve frame counter'ın
  durması; hat geri geldiğinde yeni tam frame sonrası recovery;
- Ethernet kablosu çıkarma, client timeout ve yeniden bağlantı sonrası FC03
  recovery.

21 Ağustos 2026 tarihinde kullanıcı ayrıca PLC tarafında kopma ve SBUS kaybı
senaryolarını tek tek uyguladığını ve geçersiz G16 verisinin PLC tarafından
hareket adayı yapılmadığını bildirmiştir. Bu kullanıcı bench beyanıdır; ileride
production kabul raporu hazırlanırsa TIA watch-table değerleri ve süreler ayrıca
kaydedilmelidir.

Bozuk CRC, torn snapshot, frozen heartbeat/sequence ve sentetik lost/failsafe
enjeksiyonları esas olarak PLC validator kabul testleridir. Bunların açık
kalması, doğru snapshot üreten ESP firmware kapsamının kapanmasını engellemez;
PLC/HIL güvenlik kabul listesinde izlenmelidir.

## 4. Bilinçli ertelenenler

Aşağıdaki maddeler firmware özelliği eksikliği değil, production kurulum veya
elektriksel commissioning kapısıdır:

- production ESP/PLC IP, gateway, VLAN ve varsa ACL seçimi;
- saha ölçümüne göre son PLC poll/freshness/timeout değerleri;
- GR01 elektrik seviyesinin osiloskop kaydı;
- paralel ikinci SBUS tüketicisi eklenecekse aktif buffer/izolasyon tasarımı;
- gerçek UART parity/framing/overflow elektriksel hata enjeksiyonu;
- ESP-IDF/PlatformIO sürüm bakım güncellemeleri.

Bench ağı için doğrulanmış mevcut ESP adresi `192.168.2.166/24`'tür. Firmware'de
kalan `192.168.2.241` gateway değeri aynı `/24` içindeki PLC haberleşmesini
etkilemez; production ağ geçidi kesinleştiğinde yapılandırma üzerinden
değiştirilmelidir.

## 5. Ana proje ile uyumluluk sonucu

İncelenen ESP protokol sabitleri, register yerleşimi, CRC byte sırası, SBUS
flag anlamları, session/heartbeat/frame-counter ayrımı ve fail-closed freshness
davranışı ana ROS/PLC protokol `1.1` sözleşmesiyle uyumludur. Kapanış denetiminde
rover mimarisine aykırı yeni bir firmware davranışı bulunmamıştır.

Son durum:

```text
İşlevsel ESP firmware kapsamı : TAMAM
Fiziksel bench kapsamı        : YETERLİ / production commissioning erteli
ESP32 firmware CI build       : YEŞİL
Host güvenli çekirdek CI      : YEŞİL
Ana dala birleşme             : HAZIR — PR #1 temiz ve merge edilebilir
```
