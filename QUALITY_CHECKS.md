# Biçim ve Statik Kontroller

Bu proje için kabul kapısı derleyici uyarıları, host testleri, ESP-IDF build ve
Git whitespace kontrolünden oluşur. Yerel Windows kontrolü:

```powershell
.\scripts\check_quality.ps1
```

Firmware daha önce aynı değişiklik setiyle derlendiyse hızlı kontrol:

```powershell
.\scripts\check_quality.ps1 -SkipFirmwareBuild
```

## Uygulanan kontroller

- Saf C çekirdeği Clang ile `-std=c11 -Wall -Wextra -Werror -pedantic`
  seçenekleri altında derlenir ve bütün host testleri çalıştırılır.
- ESP32 firmware'i PlatformIO/ESP-IDF ile `-Wall -Wextra -Werror` altında
  derlenir.
- `git diff --check` trailing whitespace ve hatalı patch whitespace'ını
  reddeder.
- GitHub Actions her push/PR için host testlerini ve firmware build'ini tekrar
  çalıştırır.
- GitHub host testleri `Debug` profiliyle derlenir; `core_tests.c` içindeki
  `assert(...)` kontrollerinin `NDEBUG` nedeniyle kaldırılmasına izin verilmez.

Windows script'i varsayılan olarak Clang'ı
`C:\Program Files\LLVM\bin\clang.exe` konumunda arar. Farklı kurulum için:

```powershell
.\scripts\check_quality.ps1 -ClangPath "D:\LLVM\bin\clang.exe"
```

## Biçim politikası

Bu geliştirme bilgisayarında `clang-format`, `clang-tidy` ve `cppcheck` kurulu
değildir. Bu nedenle otomatik format aracı varmış gibi zorunluluk tanımlanmaz.
Kod mevcut C stilini izlemeli, satır sonu/whitespace kontrolünü geçmeli ve
yalnız biçim amacıyla geniş mekanik değişiklik yapılmamalıdır.

İleride `clang-format` eklenecekse sürümü CI ile sabitlenmeli, `.clang-format`
dosyası ayrı ve gözden geçirilen bir değişiklik olarak eklenmelidir. Araç
sürümü sabitlenmeden toplu format uygulanmaz.

## Çalışma yolu kontrolü

Heap, bloklama, socket timeout ve watchdog için ayrıca
[`RUNTIME_AUDIT.md`](RUNTIME_AUDIT.md) kontrol listesi uygulanır.
