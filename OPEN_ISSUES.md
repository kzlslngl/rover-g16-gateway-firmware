# Açık Sözleşme Konuları

Bu dosya ESP Firmware Codex'i tarafından, ana ROS/PLC sözleşmesinde uygulamayı
etkileyen bir çelişki veya eksik test otoritesi görüldüğünde Ana Proje
Codex'inin incelemesi için tutulur.

## OI-001 — CRC ve `begin_sequence` yazım sırası

**Durum:** Ana proje kararı gerekli.

`begin_sequence`, absolute register 324–325'te bulunur ve CRC kapsamındaki
absolute register 320–379 bloğunun içindedir. `DECISIONS.md` bölüm 4 ise önce
CRC hesaplanmasını, ardından hem begin hem end sequence alanlarının yazılmasını
söylüyor. CRC hesaplandıktan sonra `begin_sequence` değiştirilirse yayımlanan
CRC artık register 320–379 içeriğiyle eşleşmez.

Firmware çekirdeğinde matematiksel olarak doğrulanabilir sıra uygulanmıştır:

1. yeni sequence seçilir;
2. `begin_sequence` dahil CRC kapsamındaki bütün alanlar yazılır;
3. register 320–379 üzerinden CRC hesaplanıp 380–381'e yazılır;
4. aynı sequence değeri CRC kapsamı dışındaki `end_sequence` 382–383'e yazılır.

Ana sözleşme ve `DECISIONS.md` bu sırayı açıkça onaylamalı veya wire yerleşimi
için farklı, CRC ile doğrulanabilir bir sıra tanımlamalıdır.

## OI-002 — Tam ESP snapshot known-result vektörü

**Durum:** Ana sözleşmede test vektörü bekleniyor.

Ana ROS test vektörlerinde 64-register ESP snapshot için bağımsız, beklenen CRC
sonuçlu tam vektör henüz yoktur. Yerel testler builder'ın iç tutarlılığını
doğrulayabilir; çapraz depo uyumluluk kabulü için ana sözleşmede tek otorite
vektörü oluşturulmalıdır.

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
