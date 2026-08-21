# PLC Entegrasyon Kılavuzu

Bu belge, `rover-g16-gateway-firmware` ile PLC uygulamasını devreye almak için
gereken teknik bilgileri ve kabul kontrollerini tek yerde toplar. Wire protokolü
`1.1`'dir. Register yerleşimi ana proje sözleşmesi değiştirilmeden bu depoda
değiştirilmez.

## 1. Bağlantı profili

| Parametre | Bench değeri | Not |
|---|---:|---|
| ESP32 adresi | `192.168.2.166/24` | Firmware build ayarıdır, değiştirilebilir |
| PLC/PC bench adresi | `192.168.2.241/24` | Production PLC adresi henüz atanmadı |
| Protokol | Modbus TCP | TCP port `502` |
| Unit ID | `1` | |
| Fonksiyon | Yalnız FC03 | Holding register okuma |
| Başlangıç adresi | `320` (`0x0140`) | Sıfır tabanlı wire offset |
| Okuma uzunluğu | `64` register | Her poll tek ve tam snapshot olmalı |

Yalnız `start=320, quantity=64` isteği kabul edilir. Parçalı okuma, farklı
adres/uzunluk ve bütün yazma fonksiyonları Modbus exception ile reddedilir.

## 2. PLC veri tipi ve byte sırası

- Her register 16 bittir ve wire üzerinde big-endian gönderilir.
- `uint32` alanlar iki register'dır: önce high word, sonra low word.
- Bir `uint32` oluşturma örneği:
  `value := SHL(DWORD(reg_hi), 16) OR DWORD(reg_lo)`.
- Sayaçlar taşabilir. İlerleme kontrolü unsigned 32-bit fark ile yapılmalıdır.
- Kanallar normalize edilmemiş 11-bit SBUS ham değerleridir. Kanal eşleme,
  neutral, endpoint, deadband ve yön tersleme PLC sorumluluğundadır.

## 3. Register haritası

| Absolute offset | Tip | Alan | Kabul kuralı |
|---:|---|---|---|
| 320 | `uint16` | `magic` | `16#4547` olmalı |
| 321 | `uint16` | `version_major` | `1` olmalı |
| 322 | `uint16` | `version_minor` | `1` olmalı |
| 323 | `uint16` | `length` | `64` olmalı |
| 324-325 | `uint32` | `begin_sequence` | `end_sequence` ile aynı olmalı |
| 326-327 | `uint32` | `gateway_session_id` | Her ESP boot/resetinde değişir |
| 328-329 | `uint32` | `gateway_heartbeat` | Gateway çalışırken ilerler |
| 330-331 | `uint32` | `sbus_frame_counter` | Yalnız kullanılabilir yeni frame ile ilerler |
| 332-333 | `uint32` | `gateway_monotonic_ms` | Uptime'ın düşük 32 biti |
| 334 | `uint16` | `frame_age_ms` | Son kullanılabilir frame yaşı, `65535`'te saturate |
| 335 | `bitfield16` | `sbus_flags` | Aşağıdaki bayraklar |
| 336 | `uint16` | `channel_count` | `16` olmalı |
| 337 | `bitfield16` | `channel_valid_mask` | Geçerli 16 kanal için `16#FFFF` |
| 338-353 | `uint16[16]` | `channel_raw[0..15]` | Ham SBUS kanal değerleri |
| 354-355 | `uint32` | `frame_period_us` | Ardışık kullanılabilir frame periyodu |
| 356-357 | `uint32` | `invalid_frame_count` | Parser/UART/frame hata sayacı |
| 358-359 | `uint32` | `frame_lost_count` | SBUS lost bayrağı olayları |
| 360-361 | `uint32` | `failsafe_count` | SBUS failsafe olayları |
| 362-379 | reserved | `reserved` | Tamamı sıfır olmalı |
| 380-381 | `uint32` | `crc32` | Register 320-379 üzerinden |
| 382-383 | `uint32` | `end_sequence` | `begin_sequence` ile aynı olmalı |

### `sbus_flags` bitleri

| Bit | Maske | Ad | Anlam |
|---:|---:|---|---|
| 0 | `16#0001` | `FRAME_VALID` | Frame freshness eşiği içinde kullanılabilir |
| 1 | `16#0002` | `FRAME_LOST` | Son yapısal frame lost işaretli |
| 2 | `16#0004` | `FAILSAFE` | Son yapısal frame failsafe işaretli |
| 3 | `16#0008` | `DECODER_ALIVE` | Decoder çalışma yolu canlı |
| 4 | `16#0010` | `DECODER_FAULT` | UART/parser/internal fault aktif |

## 4. CRC doğrulaması

Algoritma CRC-32/ISO-HDLC'dir:

- polynomial: `0x04C11DB7`;
- reflected implementation polynomial: `0xEDB88320`;
- init: `0xFFFFFFFF`;
- refin/refout: true;
- xorout: `0xFFFFFFFF`;
- kontrol vektörü: `"123456789" -> 0xCBF43926`.

CRC girdisi absolute register `320-379` arasındaki 60 register'dır. Her
register CRC'ye önce high byte, sonra low byte olarak verilir. Wire CRC değeri
380 high word, 381 low word içindedir. Reserved alanlar da CRC kapsamındadır.

## 5. Her poll için zorunlu kabul sırası

PLC, kanalları kullanmadan önce aşağıdaki kontrollerin tamamını geçirmelidir:

1. FC03 yanıtı başarılı, byte count `128` ve 64 register tam alınmış olmalı.
2. `magic=16#4547`, sürüm `1.1`, `length=64`, `channel_count=16` olmalı.
3. `begin_sequence = end_sequence` ve sequence önceki kabul edilen snapshot'a
   göre ilerlemiş olmalı.
4. Register 320-379 üzerinden hesaplanan CRC, 380-381 ile aynı olmalı.
5. `gateway_session_id` izlenmeli; değişim yeni boot/reset kabul edilmelidir.
6. `gateway_heartbeat` ilerlemeli. Yalnız TCP bağlantısının açık olması sağlık
   kanıtı değildir.
7. `FRAME_VALID=1`, `FRAME_LOST=0`, `FAILSAFE=0`, `DECODER_ALIVE=1` ve
   `DECODER_FAULT=0` olmalı.
8. `channel_valid_mask=16#FFFF` ve `frame_age_ms` PLC'de seçilecek freshness
   sınırının altında olmalı.
9. `sbus_frame_counter` PLC'nin belirleyeceği zaman penceresinde ilerlemeli.

Kontrollerden biri başarısızsa G16 verisi MANUAL komut adayı olamaz. Son iyi
kanal değerleri hareket üretmek için tutulmamalıdır.

## 6. Session değişimi ve fail-safe davranışı

`gateway_session_id` değişirse PLC:

1. mevcut G16/MANUAL yetkisini düşürür ve güvenli/neutral çıkış uygular;
2. eski session'a ait snapshot ve kanal değerlerini reddeder;
3. yeni session'da ardışık sağlıklı snapshot'lar görülene kadar re-arm etmez;
4. operatör/authority state-machine onayını yeniden ister.

Re-arm süresi ve kaç ardışık snapshot gerektiği PLC safety tasarımına aittir;
ESP firmware'i bu kararı vermez.

## 7. PLC tarafında hazırlanacak bloklar

- Modbus TCP client bağlantı ve tam 64-register FC03 poll bloğu.
- Big-endian `uint32` birleştirme yardımcı fonksiyonu.
- CRC-32/ISO-HDLC doğrulama fonksiyonu.
- Header, sequence, session, heartbeat, freshness ve sayaç gözlem bloğu.
- Ham 16 kanal için sürümlü mapping/neutral/endpoint/deadband tablosu.
- G16 verisini authority state machine'e yalnız `gateway_data_valid` üzerinden
  veren fail-closed arayüz.
- Tanı ekranında session, heartbeat, frame counter, age, flags ve hata
  sayaçlarının gösterimi.

## 8. PLC entegrasyonundan önce verilmesi gereken bilgiler

- Production ESP ve PLC IP adresleri, subnet, gateway/VLAN ve varsa ACL.
- PLC marka/modeli, CPU ve kullanılan Modbus TCP kütüphane/blok sürümü.
- PLC poll periyodu, response timeout ve reconnect politikası.
- Kabul edilen `frame_age_ms`, heartbeat ve frame-counter timeout eşikleri.
- G16 kanal numarası -> fonksiyon eşleme tablosu.
- Her kanal için neutral, min/max, deadband, yön ve kalibrasyon sürümü.
- MANUAL authority kazanma/kaybetme ve session sonrası neutral/re-arm kuralı.
- Haberleşme kaybında güvenli çıkış ve aktüatör inhibit davranışı.

Bu sayısal değerler PLC scan süresi, ağ yükü ve HIL ölçümleri görülmeden
production-final kabul edilmemelidir.

## 9. PLC/HIL kabul testleri

- Normal G16 -> ESP -> PLC kanal akışı ve 16 kanal mapping doğrulaması.
- Ethernet kablosu çıkarma, Modbus timeout ve reconnect.
- SBUS hattı çıkarma: frame counter donar, age artar, valid/mask kapanır.
- Lost ve failsafe bayrak enjeksiyonu; hareketin engellenmesi.
- ESP reseti: session değişimi, eski verinin reddi ve neutral/re-arm.
- Bozuk CRC, begin/end uyuşmazlığı ve yanlış header'ın PLC'de reddi.
- Frozen frame/heartbeat ve sayaç anomalilerinin reddi.
- Orin kapalıyken yetkili G16 manuel yol testi.
- ESP enerjisi kesildiğinde hiçbir eski kanalın aktüatöre uygulanmaması.

## 10. Bench hızlı kontrol

PC adresi `192.168.2.241/24` iken:

```powershell
ping 192.168.2.166
python scripts\test_modbus_tcp.py 192.168.2.166
```

Script tam snapshot, header, begin/end sequence, CRC ve yazma reddini kontrol
eder. Bu script PLC safety mantığının yerine geçmez; yalnız endpoint bench
kanıtıdır.
