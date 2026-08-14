# Secure KMDF buffered-IOCTL sample

Bu örnek, Windows 10/11 için KMDF tabanlı bir **non-PnP control device** ve
ona bağlanan bir Win32 konsol uygulamasıdır. İstenen beş kaynak dosyası:

- `Public.h`
- `Driver.c`
- `Device.c`
- `Queue.c`
- `UserApp.cpp`

## Tasarım özeti

- IOCTL yalnızca `METHOD_BUFFERED` kullanır.
- Girdi ve çıktı uzunlukları tam olarak (`== sizeof(...)`) doğrulanır.
- Paket doğal hizalamalı, sabit boyutlu ve pointersızdır; ABI boyutu derleme
  zamanında 288 bayt olarak doğrulanır.
- Buffered I/O'daki ortak sistem tamponu olasılığı nedeniyle girdi yalnız bir
  kez yerel kopyaya alınır ve çıktı bundan sonra yazılır.
- Ayrılmış alanlar ve kullanılmayan payload kuyruğu sıfır olmak zorundadır.
- Çıktı bütünüyle sıfırlanarak hazırlanır; çekirdek belleği sızıntısı önlenir.
- Control-device ACL'i `SDDL_DEVOBJ_SYS_ALL_ADM_ALL` ile yalnızca `SYSTEM` ve
  yerel `Administrators` grubuna tam erişim verir.
- IOCTL erişim bitleri hem okuma hem yazma yetkili handle gerektirir.
- Queue sıralıdır ve `WdfExecutionLevelPassive` ile callback PASSIVE_LEVEL'a
  sabitlenmiştir; pageable fonksiyonlar `PAGED_CODE()` ile doğrulanır.
- Örnek dinamik driver belleği ayırmaz. WDF nesneleri parent/child yaşam
  döngüsüyle temizlenir; yarım kalan `WDFDEVICE_INIT` ve cihaz nesneleri tüm
  hata yollarında serbest bırakılır. Dört baytlık WDF pool tag'i `MydT`'dir.

`MY_OPERATION_ECHO`, payload'u opak baytlar olarak taşır; bu nedenle user-mode
tarafında üretilmiş ciphertext de taşınabilir. Örnek uygulama, güvenli ve kolay
gözlenebilir bir çekirdek işlemi göstermek için
`MY_OPERATION_ASCII_UPPERCASE` kullanır. Bu örnek kriptografi sağlamaz ve
yerel IOCTL aktarımını "şifreli kanal" olarak tanımlamaz. Gerçek anahtar
yönetimi gerekiyorsa sabit anahtar veya özel şifreleme algoritması eklemeyin;
CNG/KSP tabanlı, ayrıca tehdit modeli çıkarılmış bir tasarım kullanın.

## Visual Studio + WDK derleme

1. Visual Studio'da WDK ile iki proje oluşturun:
   - `Kernel Mode Driver, Empty (KMDF)` (C kaynakları),
   - `Console App` (C++17 veya üstü, `UserApp.cpp`).
2. Driver projesine `Driver.c`, `Device.c`, `Queue.c`, `Public.h`
   dosyalarını; uygulama projesine `UserApp.cpp` ve `Public.h` dosyasını
   ekleyin.
3. Driver projesinin KMDF sürümünü hedef Windows 10/11 sisteminizin desteklediği
   sürüme ayarlayın. WDK'nin oluşturduğu driver-package/INF ayarlarını koruyun
   ve non-PnP servis için paketinizde KMDF service metadata'sının bulunduğunu
   doğrulayın.
4. Aynı mimariyi seçin (`x64` önerilir), driver paketini test-signing açık bir
   sanal makineye kurun ve servisi başlatın.
5. `UserApp.exe "merhaba kmdf"` komutunu **yükseltilmiş yönetici** konsolunda
   çalıştırın. Beklenen yanıt `MERHABA KMDF` olur.

## Doğrulama önerileri

- Driver Verifier: Special Pool, Pool Tracking, I/O Verification, Security
  Checks, Miscellaneous Checks ve DDI compliance seçenekleri.
- Static Driver Verifier / Code Analysis for Drivers.
- IOCTL fuzzer ile 0..512 bayt tüm uzunluklar, bilinmeyen IOCTL'ler, hatalı
  version/operation/length/reserved alanları ve eşzamanlı istekler.
- Normal kullanıcı hesabında `CreateFileW` çağrısının
  `ERROR_ACCESS_DENIED` verdiğini doğrulayın.

Hiçbir örnek kaynak kod mutlak olarak “zero-vulnerability” garantisi veremez.
Üretim kullanımı ayrıca imzalama, INF güvenliği, güvenli güncelleme, tehdit
modelleme, fuzzing, verifier/SDV ve hedef sürümlerde regresyon testi gerektirir.
