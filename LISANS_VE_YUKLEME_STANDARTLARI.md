# Demokratik Kongo — Lisans ve Yükleme Standartları

Sürüm: 1.1 · Son güncelleme: 2026-10-17 · Durum: **taslak** (açık kararlar §11)

Bu belge; launcher'ın (`RuntimeHost.exe`) ve çekirdek DLL'in
(`RuntimeHostCore.dll`) lisans doğrulama, HWID kilidi ve yükleme
(aktivasyon → enjeksiyon → oyun) akışında uyması gereken kuralları tanımlar.
Kod ile belge çelişirse **belge kazanır** — kod güncellenir.

---

## 1. Bileşenler

| Bileşen | Rol |
|---|---|
| **Panel** — `https://kongo.drip.invalid` | Key üretimi, isim/not, HWID sıfırlama, ban, +gün, silme. Giriş: `kongo` |
| **API** — `https://api.drip.invalid` | Keygen CE lisans API'si. TLS zorunlu; paneline admin anahtarı tarayıcıya verilmez (Caddy `/kg/*` proxy'si sunucu tarafında ekler) |
| **Launcher** — `RuntimeHost.exe` | Lisans ekranı, HWID, DPAPI saklama, doğrulama, enjeksiyon kapısı |
| **Çekirdek** — `RuntimeHostCore.dll` | (Faz 2) Oyun içi periyodik yeniden doğrulama |

## 2. Anahtar modeli

- Her key **tek bir PC'ye** bağlanır (policy: `strict`, `maxMachines = 1`, `floating = false`).
- Bağlanmamış key, ilk doğrulamada otomatik olarak o PC'ye bağlanır (kullanıcı ekstra işlem yapmaz).
- Süre politikaları: **1 / 3 / 7 / 14 / 30 / 90 / 180 / 365 gün** ve **Süresiz**. Süre, key **oluşturulduğu anda** başlar.
- HWID değişimi: panelden **HWID Sıfırla** → kullanıcı yeni PC'de tekrar giriş yapar.

## 3. HWID formülü (DONDURULMUŞ — değiştirme!)

```
source = "dkk1|" + MachineGuid + "|" + %08x(C: sürücü seri no) + "|" + BilgisayarAdı
hwid   = sha256(source)      // 64 karakter küçük harf hex
```

- `MachineGuid`: `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid`
- Formül değişirse `"dkk1"` ön ekini `"dkk2"` yapın **ve** panelde tüm eski makine
  kayıtlarını silin; aksi hâlde aktif tüm kullanıcılar eşleşme hatası alır.

## 4. Uçtan uca akış

1. **Panel:** key üret (istersen isim/not ver) → kullanıcıya ilet.
2. **Launcher:** kullanıcı keyi girer →
   `POST /v1/accounts/drip/licenses/actions/validate-key` (auth **yok**).
3. Yanıt `meta.code == NO_MACHINE` ise → **makine bağlama:**
   `POST /v1/accounts/drip/machines`,
   `Authorization: License <KEY>` + gövdede `relationships.license`
   (id = 2. adımdaki yanıtın `data.id` değeri) → `201`.
4. **Tekrar doğrula** → `VALID` → key, DPAPI ile
   `%APPDATA%\RuntimeHost\license.dat` içine yazılır.
5. Sonraki açılışlarda bu akış sessizce otomatik işler.

## 5. Doğrulama kodları → kanonik kullanıcı mesajları

| `meta.code` | Launcher mesajı | Anlam / davranış |
|---|---|---|
| `VALID` | License active | Giriş açılır |
| `NO_MACHINE` | *(otomatik aktivasyon denenir)* | Bu PC'ye henüz bağlı değil |
| `FINGERPRINT_SCOPE_MISMATCH`, `MACHINE_SCOPE_MISMATCH` | Key is linked to another PC - ask for a HWID reset | Başka PC'ye bağlı |
| `TOO_MANY_MACHINES` | *(aynı mesaj)* | Limit dolu |
| `SUSPENDED`, `BANNED` | License suspended by the administrator | Banlı |
| `EXPIRED` | License expired | Süre bitti |
| `NOT_FOUND` | Invalid key - check it and try again | Geçersiz key |
| `NETWORK` | Could not reach the license server | Ağ hatası — **ban değildir** |
| diğer | sunucudan gelen `detail` | Bilinmeyen durum |

## 6. Launcher standartları (bu sürümde uygulandı)

1. **Kapı:** `Inject` yalnızca **son 30 dakika içinde** doğrulanmış geçerli lisansta
   etkinleşir; doğrulama daha eskiyse enjeksiyon işçisi önce **yeniden doğrular**
   ("Verifying license..." adımı, ilerleme çubuğunda 0. adım).
2. **Yükleme adımları görünür olmalı:** Lisans kontrolü → hazırlık → injector'ın
   adımları (10 adım) → sonuç. İlerleme çubuğu + durum pill'i + toast zorunlu.
3. **Saklama:** key **asla düz metin** yazılmaz; DPAPI (`CryptProtectData`,
   kullanıcı hesabına bağlı) ile şifrelenir. Kayıt içeriği:
   `{ key, lastValidAt, expiry }`.
4. **Çevrimdışı tolerans:** sunucuya ulaşılamazsa, **son başarılı doğrulamadan
   itibaren 24 saat** girişe izin verilir; durum satırı "Offline mode" uyarısı
   gösterir. *(Açık karar #1 — süre ayarlanabilir.)*
5. **Timeout:** HTTP 8 sn. Başarısız istekte kullanıcı bilgilendirilir; sonsuz
   otomatik yeniden deneme yok.
6. **UI dili:** İngilizce (mevcut launcher ile tutarlı). Türkçeye çevirmek için
   tek yer: `licenseClient.cpp` içindeki `userMessageForCode` tablosu. *(Açık karar #2.)*
7. **Günlükleme:** key hiçbir log satırına yazılmaz; yalnızca sonuç kodu loglanır.
8. **CLI istisnası:** `RuntimeHost.exe --inject <pid>` (geliştirici yolu) lisans
   kontrolünden **muaf**; yalnızca yerel test için, dağıtımda kullanılmaz.
9. **Core güncelleme:** her enjeksiyon öncesi sunucudaki **aktif core DLL** indirilir ve
   SHA-256 ile doğrulanır; indirilemezse gömülü kopya kullanılır (§10).
10. **Launcher güncelleme:** launcher her açılışta siteye bakar; yeni sürüm varsa indirir,
    doğrular, kendini **yerinde değiştirir** ve yeniden başlar (§10.2). Kullanıcı hiçbir
    şey yapmaz — "siteye yeni EXE at" dışında dağıtım işi kalmaz.

## 7. Çekirdek DLL standartları (Faz 2 — **UYGULANDI**)

1. DLL yüklenirken `%APPDATA%\RuntimeHost\license.dat` kaydını (DPAPI, launcher yazar —
   çekirdek yalnızca **okur**) çözüp **bağımsız** doğrulama yapar; kayıt yoksa/çözülemezse
   **kilitli** başlar (fail-closed).
2. Doğrulama: sunucuya `validate-key` + **bu makinenin HWID'i** (launcher ile birebir aynı
   `dkk1|…` formülü). Başarılıysa modüller açılır.
3. **Heartbeat:** 30 dakikada bir yinelenir. Kilitliyken ilk 10 saniyede hızlı yeniden
   deneme yapılır (bildirim gösterilene kadar). *(Ayar: `licenseGate.cpp` sabitleri.)*
4. **Çevrimdışı:** sunucuya ulaşılamazsa kayıttaki son başarılı doğrulamadan itibaren
   **24 saat** tolerans; kayıt süresi yerel olarak dolmuşsa çevrimdışıyken bile kilitlenir.
5. **Kilitliyken ne olur:** bytecode patch uygulanmaz, modül tick/render/HUD çalışmaz;
   ClickGUI açık kalır ve `License not verified - features disabled` bildirimi gösterilir.
   Doğrulama düzelince otomatik açılır (`License verified - features enabled`).
6. **Kapsanan senaryo:** DLL kopyalanıp başka bir injector ile sokulsa bile kayıt + HWID
   doğrulaması DLL'in içinde yapıldığı için lisanssız çalışmaz. (Kod düzeyi kırmaya karşı
   "eşik yükseltme"dir — Faz 3 ile kriptografik imza da eklendi: §7.1.)

### 7.1 Sunucu imzalı offline token (Faz 3 — **UYGULANDI**)

- **Ne:** Sunucu, tam doğrulanmış her lisans için **Ed25519 ile imzalı** bir token
  üretir; istemciler yalnızca **gömülü public key** ile doğrular (TweetNaCl,
  `include/licenseShared/`). İmza taklit edilemez; ağ kandırması veya ağ kesintisi
  lisans kanıtını değiştiremez.
- **Token:** `base64url(payload).base64url(signature)` — payload:
  `{"v":1,"key":…,"fp":<HWID>,"name":…,"iat":…,"exp":…}`. Sunucu yalnızca
  `validate-key + HWID` = VALID olduğunda imzalar. TTL: **7 gün** (lisans süresini aşamaz).
- **Kullanım:** Launcher her başarılı doğrulamada tokenı tazeler ve DPAPI kaydına yazar.
  Ağ yoksa **hem launcher hem çekirdek** önce tokenı doğrular (imza + key + HWID + süre);
  token içermeyen eski kayıtlar için 24 saatlik son-doğrulama kuralı geçerli kalır.
- **Uç noktalar:** `POST /up/api/token` `{key, fp}` (herkese açık; VALID lisans şart) ·
  `GET /up/api/pubkey`.
- **Anahtar:** özel anahtar yalnızca VPS'te (`/data/ed25519.key.pem`, dkk-update
  konteyneri). Public key istemcilere gömülü: `include/licenseShared/licensePublicKey.h`.
  **Rotasyon iki tarafın da yeniden derlenmesini gerektirir.**
- **Doğrulama zinciri (öncelik sırası):** 1) Sunucu (TLS) → 2) İmzalı token (kripto)
  → 3) 24 saat son-doğrulama toleransı (yalnızca eski kayıtlar).
- **Test aracı:** `RuntimeHost.exe --token-test <token-dosyası>` → çıkış 0 = geçerli,
  2 = geçersiz. *(Kurcalanmış token reddi doğrulandı.)*

## 8. API sözleşmesi (contract testleriyle doğrulandı)

### 8.1 Doğrulama — auth yok
```http
POST /v1/accounts/drip/licenses/actions/validate-key
Content-Type: application/vnd.api+json

{ "meta": { "key": "DKK-XXXXX-XXXXX-XXXXX-XXXXX",
            "scope": { "fingerprint": "<hwid>" } } }
```
→ `200` gövde: `{ "meta": { "valid": true, "code": "VALID", ... },
"data": { "id": "<licenseId>", "attributes": { "name": "...", "expiry": "...|null" } } }`

### 8.2 Makine aktivasyonu — License yetkisi
```http
POST /v1/accounts/drip/machines
Authorization: License DKK-XXXXX-XXXXX-XXXXX-XXXXX

{ "data": { "type": "machines",
    "attributes": { "fingerprint": "<hwid>", "name": "<PC adı>", "platform": "windows" },
    "relationships": { "license": { "data": { "type": "licenses",
                                             "id": "<8.1'deki data.id>" } } } } }
```
→ `201` · limit doluysa `4xx` + `errors[0]` içinde `TOO_MANY_MACHINES`.

### 8.3 Panel operasyonları (yalnızca sunucu tarafı)
- HWID sıfırlama: `DELETE /v1/accounts/<acc>/machines/<id>`
- Ban / kaldır: `PATCH /v1/accounts/<acc>/licenses/<id>` → `{"suspended": true|false}`
- Süre ekle: `PATCH .../licenses/<id>` → `{"expiry": "<ceil ISO>"}`
- Sil: `DELETE /v1/accounts/<acc>/licenses/<id>`

## 9. Operasyon rehberi

| İhtiyaç | Yapılacak |
|---|---|
| Key üretmek | Panel → Key Üret → (isim gir) → Üret → Tümünü Kopyala |
| İsimi sonradan eklemek | Key Listesi → İsim sütunundaki hücreye tıkla |
| "HWID'im değişti" | Key Listesi → HWID Sıfırla → kullanıcı tekrar giriş yapar |
| Süre uzatmak | + Gün |
| Banlamak | Banla (doğrulama anında kesilir; açık oyuncu en geç 30 dk içinde düşer) |
| Oyun içi durum | Çekirdek kendi doğrulamasını yapar (§7, §7.1); ban sonrası en geç 30 dk içinde oyun içi modüller de kapanır |

## 10. DLL güncelleme servisi (canlı)

Panel → **DLL / Güncelleme** sekmesinden yeni bir `*.dll` yüklersin; yükleme o sürümü
anında **aktif sürüm** yapar. Launcher her enjeksiyon öncesi aktif sürümü indirir —
EXE hiç değişmez, güncelleme dağıtımı "siteye DLL at" kadar basittir. Panelden eski
bir sürümü **Aktif Yap** ile geri alabilir (rollback), silebilir veya indirebilirsin.

| Uç nokta | Kimlik | İşlev |
|---|---|---|
| `GET /up/dl/current?key=<KEY>&fp=<HWID>` | lisans + HWID (herkese açık) | Aktif DLL'i indirir; yanıtta `X-DLL-SHA256` sağlama imzası bulunur |
| `GET /up/list` | panel (basic auth) | Sürüm listesi + aktif sürüm |
| `POST /up/upload?name=&note=` | panel (basic auth) | Yeni sürüm yükler **ve aktif yapar** (ham gövde, `MZ` doğrulaması, ≤ 64 MB) |
| `POST /up/promote` `{"name"}` | panel | Eski sürümü tekrar aktif yapar (rollback) |
| `POST /up/delete` `{"name"}` | panel | Sürümü siler (aktifse aktiflik kalkar) |
| `GET /up/file?name=` | panel | Sürümü indirir (yedek amaçlı) |

Launcher tarafı (`coreUpdater.cpp`) kuralları:

1. İndirilen dosya, yanıt başlığındaki `X-DLL-SHA256` ile **doğrulanır**; uyuşmazsa
   indirme reddedilir ve gömülü kopya kullanılır.
2. Önbellek **içerik adresli**: `%APPDATA%\RuntimeHost\update\RuntimeHostCore-<sha12>.dll`
   — oyunda yüklü eski DLL dosya kilidi yüzünden yeni sürümün yazılmasını engelleyemez.
3. İndirme başarısızsa (ağ / sunucu / lisans reddi) launcher **gömülü kopyayla** devam
   eder; bir güncelleme kesintisi lisanslı kullanıcıyı asla kilitleyemez.
4. Lisans kapısı **sunucudadır**: indirme anında `validate-key + HWID` yeniden
   doğrulanır; geçersiz anahtar/başka PC `403` alır. Yani DLL, lisanssız kimseye
   servis edilmez — KeyAuth tarzı dağıtım bunun üzerine kuruludur.

### 10.2 Launcher hattı (EXE de kendini günceller)

Panel → **Core + Launcher** sekmesi → Launcher bölümü. Launcher da aynı depoda
versiyonlanır; **farkı**: indirme linki herkese açıktır (yeni kullanıcı, daha
key'i yokken launcher'ı indirebilmeli).

| Uç nokta | Kimlik | İşlev |
|---|---|---|
| `GET /up/dl/launcher/info` | herkese açık | Aktif launcher'ın `{name, sha256, size}` bilgisi (küçük JSON) |
| `GET /up/dl/launcher` | herkese açık | Aktif launcher EXE'sini indirir |

Yeni kullanıcı linki: `https://kongo.drip.invalid/up/dl/launcher`
(panelde tıkla-kopyala butonu var).

Launcher içi self-update (`coreUpdater.cpp::checkAndApplyLauncherUpdate`):
1. Her açılışta **yalnızca `/info`** çekilir (küçük) ve kendi SHA-256'sı ile karşılaştırılır;
   aynıysa hiçbir indirme yapılmaz.
2. Farklıysa EXE indirilir ve `X-DLL-SHA256`/`info` değeriyle doğrulanır; içerik-adresli
   kopya `%APPDATA%\RuntimeHost\update\RuntimeHost-<sha12>.exe` altına yazılır.
3. **Swap:** çalışan EXE değiştirilemez ama **yeniden adlandırılabilir** → çalışan dosya
   `RuntimeHost.exe.old` olarak taşınır, yeni dosya asıl yola konur. İkinci adım
   başarısız olursa geri alınır (kullanıcı hiçbir durumda EXE'siz kalmaz).
4. Swap sonrası launcher **kendini yeniden başlatır** (yeni EXE ile). Döngü güvenli:
   yeni EXE'nin sha'sı sunucudakiyle aynıdır, tekrar güncelleme tetiklenmez.
5. **Tek seferlik elle devir:** self-update kodu ancak yeni EXE'de var — mevcut
   kullanıcılara bu sürüm bir kez elle ulaştırılmalı; sonrası tamamen otomatik.

---

## 11. Kararlar (güncel durum)

1. **Çevrimdışı tolerans:** eski kayıtlar için 24 saat + **7 günlük imzalı token** (§7.1)
   uygulandı — süreler `licenseGate.cpp` sabitlerinden ayarlanır.
2. **Launcher arayüzü İngilizce** (mevcut üslupla tutarlı). Türkçe istenirse tek yer:
   `licenseClient.cpp` mesaj tablosu.
3. **Heartbeat:** 30 dakika uygulandı (§7).
4. **DLL içi zorunlu doğrulama (Faz 2):** uygulandı; üzerine imzalı offline token
   (Faz 3, §7.1) eklendi.

---

## 12. Defender / dağıtım notu

Bir JVM injector'u (JVMTI ajanı + MinHook + süreç içi erişim) Defender'ın davranışsal ML
modelinde her zaman işaretlenme adayıdır; tamamen görünmez yapmak kod imzalama (EV
sertifikası) ve ciddi anti-analiz işi gerektirir — bu kapsamın dışındadır. Uygulanan
azaltmalar ve dağıtım kuralı:

1. **Tembel çıkarma (uygulandı):** launcher açılışta DLL'i `%TEMP%`e çıkarmaz; diske
   yalnızca kullanıcı **Inject'e bastığında** dokunulur (açılıştaki "dropped binary" izi kalktı).
2. **Kullanıcı istisnası (standart adım):** istemciyi sabit bir klasöre kurun (örn.
   `C:\DemokratikKongo`); kullanıcı yönetici PowerShell'de
   `Add-MpPreference -ExclusionPath "C:/DemokratikKongo"` çalıştırsın — veya Güvenlik
   Merkezi > Virüs ve tehdit koruması > İstisnalar > Klasör ekle. Panelin Launcher
   bölümünde bu not hazır.
3. **Yanlış-pozitif bildirimi:** `https://www.microsoft.com/en-us/wdsi/filesubmission`
   üzerinden dosyayı gönderip inceleme isteyebilirsiniz (gün sürebilir).
4. Opsiyonel ileri adım: **kod imzalama sertifikası** — davranışsal tespitleri tamamen
   kaldırmaz; SmartScreen itibarını ve ilk izlenimi düzeltir.
5. **Attach yolu onarımı (asıl çözüm):** Lunar'ın paketlediği JRE görüntüleri `jdk.attach`
   modülü içermez; attach denemesi `ClassNotFoundException` ile düşüp her inject **klasik
   uzak-iş-parçacığı** yoluna (VirtualAllocEx + WriteProcessMemory + NtCreateThreadEx) geçiyordu
   — Defender'ın işaretlediği davranış tam buydu. Launcher artık attach için **tam JDK'ları**
   arar (JAVA_HOME → HKCU/HKLM kayıt defteri → PATH → yaygın kurulum dizinleri; hedefin kendi
   JRE'si **en sonda**) ve adayları sırayla dener. Makinede herhangi bir JDK varsa inject
   **uzak süreç yazımı olmadan** tamamlanır; Defender'nın davranış modelinin gördüğü zincir
   ortadan kalkar. Hiç JDK bulunamazsa klasik yol kullanılır (nadir) — o kullanıcılar için
   istisna hâlâ son çaredir.
