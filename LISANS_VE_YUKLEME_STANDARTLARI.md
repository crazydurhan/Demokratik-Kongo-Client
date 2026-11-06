# Drip Launcher — Lisans ve Yükleme Standartları

Sürüm: 1.1 · Son güncelleme: 2026-10-17 · Durum: **taslak** (açık kararlar §11)

Bu belge; launcher'ın (`RuntimeHost.exe`) ve çekirdek DLL'in
(`RuntimeHostCore.dll`) lisans doğrulama, HWID kilidi ve yükleme
(aktivasyon → kurulum → oyun) akışında uyması gereken kuralları tanımlar.
Kod ile belge çelişirse **belge kazanır** — kod güncellenir.

---

## 1. Bileşenler

| Bileşen | Rol |
|---|---|
| **Panel** — `https://dripclient.duckdns.org` | Key üretimi, isim/not, HWID sıfırlama, ban, +gün, silme. Giriş: `admin` |
| **API** — `https://dripclient.duckdns.org` | Keygen CE lisans API'si. TLS zorunlu; paneline admin anahtarı tarayıcıya verilmez (Caddy `/kg/*` proxy'si sunucu tarafında ekler) |
| **Launcher** — `RuntimeHost.exe` | Lisans ekranı, HWID, DPAPI saklama, doğrulama, kurulum (loader mod + core işaretçisi) |
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
2. **Yükleme adımları görünür olmalı:** Lisans kontrolü → core indirme → java host
   ile enjeksiyon (3-4 adım) → sonuç. İlerleme çubuğu + durum pill'i + toast zorunlu.
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
8. **CLI istisnası:** `RuntimeHost.exe --inject <pid> [dll]` (geliştirici yolu) lisans
   kontrolünden **muaf**; yalnızca yerel test/otomasyon için, dağıtımda kullanılmaz.
9. **Core güncelleme:** her enjeksiyon öncesi sunucudaki **aktif core DLL** indirilir ve
   SHA-256 ile doğrulanır; indirilemezse en yeni önbellek kopyası (önceki lisanslı
   indirmelerden) kullanılır, o da yoksa kullanıcıya net hata gösterilir (§10). Core,
   launcher ile **dağıtılmaz** (ne gömülü ne yan dosya); uzak yükleme işini java.exe
   hostundaki ajan yapar (§12).
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
6. **Kapsanan senaryo:** DLL kopyalanıp elle veya başka bir yükleyiciyle yüklense bile kayıt + HWID
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

### 10.1 İki çekirdek hattı (1.8.9 ve yüksek sürümler)

Launcher açılışta **1.8.9** veya **Yüksek Sürümler (1.21.x)** seçimini ister. Seçim iki
şeyi belirler: süreç listesi (yalnızca o sürümün oyunları görünür) ve indirilecek çekirdek
hattı. Sunucuda iki bağımsız hat vardır:

| Hat | `edition` | Dizin | Panel bölümü |
|---|---|---|---|
| 1.8.9 | `legacy` (varsayılan) | `data/versions/core` | "Çekirdek DLL" |
| Yüksek sürümler | `modern` | `data/versions/core-modern` | "Çekirdek DLL — 1.21.11" |

Panelden yönetim `kind=core` / `kind=core_modern` ile yapılır (liste, yükle, aktif yap,
sil). İki hat birbirini etkilemez; birini güncellemek diğerini değiştirmez.

| Uç nokta | Kimlik | İşlev |
|---|---|---|
| `GET /up/dl/current?key=<KEY>&fp=<HWID>&edition=<legacy\|modern>` | lisans + HWID (herkese açık) | Seçilen hattın aktif DLL'ini indirir; yanıtta `X-DLL-SHA256` sağlama imzası bulunur. `edition` yoksa `legacy`. |
| `GET /up/dl/core-modern?...` | lisans + HWID | `edition=modern` için kısayol |
| `GET /up/list` | panel (basic auth) | Sürüm listesi + aktif sürüm |
| `POST /up/upload?name=&note=` | panel (basic auth) | Yeni sürüm yükler **ve aktif yapar** (ham gövde, `MZ` doğrulaması, ≤ 64 MB) |
| `POST /up/promote` `{"name"}` | panel | Eski sürümü tekrar aktif yapar (rollback) |
| `POST /up/delete` `{"name"}` | panel | Sürümü siler (aktifse aktiflik kalkar) |
| `GET /up/file?name=` | panel | Sürümü indirir (yedek amaçlı) |

Launcher tarafı (`coreUpdater.cpp`) kuralları:

1. İndirilen dosya, yanıt başlığındaki `X-DLL-SHA256` ile **doğrulanır**; uyuşmazsa
   indirme reddedilir ve en yeni önbellek kopyası kullanılır.
2. Önbellek **içerik adresli**: `%APPDATA%\RuntimeHost\update\RuntimeHostCore-<edition>-<sha12>.dll`
   — oyunda yüklü eski DLL dosya kilidi yüzünden yeni sürümün yazılmasını engelleyemez.
3. İndirme başarısızsa (ağ / sunucu / lisans reddi) en yeni önbellek kopyası (önceki
   lisanslı indirmelerden) kullanılır; o da yoksa kullanıcıya net bir hata gösterilir.
   Core hiçbir biçimde launcher ile **dağıtılmaz** — ne gömülü ne yan dosya (§12).
4. Lisans kapısı **sunucudadır**: indirme anında `validate-key + HWID` yeniden
   doğrulanır; geçersiz anahtar/başka PC `403` alır. Yani DLL, lisanssız kimseye
   servis edilmez — KeyAuth tarzı dağıtım bunun üzerine kuruludur.

### 10.2 Launcher hattı (EXE de kendini günceller)

Panel → **Core + Launcher** sekmesi → "Çekirdek DLL" (1.8.9), "Çekirdek DLL — 1.21.11"
ve Launcher bölümleri. Launcher da aynı depoda
versiyonlanır; **farkı**: indirme linki herkese açıktır (yeni kullanıcı, daha
key'i yokken launcher'ı indirebilmeli).

| Uç nokta | Kimlik | İşlev |
|---|---|---|
| `GET /up/dl/launcher/info` | herkese açık | Aktif launcher'ın `{name, sha256, size}` bilgisi (küçük JSON) |
| `GET /up/dl/launcher` | herkese açık | Aktif launcher EXE'sini indirir |

Yeni kullanıcı linki: `https://dripclient.duckdns.org/up/dl/launcher`
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

**Kök neden:** Defender'ın davranışsal ML modeli, **launcher EXE'sinin** başka bir
sürece uzaktan iş parçacığı enjekte etmesini (`Behavior:Win32/DefenseEvasion.A!ml`)
her seferinde işaretliyordu. Lunar'ın `-XX:+DisableAttachMechanism` bayrağı standart
JVMTI attach'i kapattığı için her inject bu klasik zincire düşüyordu
(`VirtualAllocEx + WriteProcessMemory + NtCreateThreadEx`).

**Çözüm — enjeksiyonu java.exe'ye yaptırmak (uygulandı):** Launcher artık uzak
yükleme işini kendisi yapmıyor; gömülü bir JVMTI aracı ajanını **java.exe host
süreci** içinde çalıştırıyor:

    java.exe -agentpath:RuntimeHostInjector.dll=pid=<hedef>;dll=<core.dll> -version

Ajan (`src/agent/injectAgent.cpp`) klasik ilkel API'leri (OpenProcess +
VirtualAllocEx + WriteProcessMemory + CreateRemoteThread + LoadLibraryW) java.exe
**içinden** çalıştırır. Bu API dizisi, jcmd/profiler'ların her attach'te yaptığı
şeyle birebir aynıdır; davranış modeli bunu "normal JDK araç aktivitesi" olarak
görür — kullanıcının makinesinde birebir doğrulandı: enjeksiyon tamamlandı,
Defender sıfır uyarı verdi. Ek fayda: `jdk.attach` modülü gerekmediği için **her
HotSpot java** host olabilir — Lunar'ın kendi Zulu JRE'si dahil.

- **Lisans kapısı değişmedi:** core DLL yine servisten lisans+HWID ile iner; core
  içindeki bağımsız doğrulama (§7/§7.1) aynen sürer.
- **Tanılama:** `%APPDATA%\RuntimeHost\inject-agent.log` (ajan) +
  `%APPDATA%\RuntimeHost\launcher.log` (launcher).
- **İstisna notu (son çare, başka AV'ler için):** paneldeki klasör istisnası notu
  duruyor; standart adım değildir.
- **Yanlış-pozitif bildirimi / kod imzalama (EV):** opsiyonel; davranışsal tespit
  yerine SmartScreen itibarını düzeltir.
