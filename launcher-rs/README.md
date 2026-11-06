# Drip Launcher — Rust

Mevcut C++/ImGui launcher'ın Rust ile yeniden yazımı. Amaç: **özellik kaybı yok**,
**minimal GUI** (tema/animasyon yok), hızlı ve sorunsuz kullanım.

## Ne yapar

- **Lisans**: Keygen CE doğrulaması (`validate-key`), ilk kullanımda makine bağlama
  (`machines`), 24 saat çevrimdışı tolerans + sunucu imzalı Ed25519 offline token,
  DPAPI ile `%APPDATA%\RuntimeHost\license.dat` (C++ launcher ile aynı format).
- **Core teslimi**: `/up/dl/current?key&fp&edition` → `X-DLL-SHA256` doğrulaması →
  içerik adresli önbellek (`RuntimeHostCore-<legacy|modern>-<sha12>.dll`), sunucu
  yoksa en yeni önbellek.
- **Enjeksiyon**: gömülü `RuntimeHostInjector.dll` (JVMTI agent) `%TEMP%\RuntimeHost\<hash>`
  altına çıkarılır; `java.exe -agentpath:<agent>=pid=<pid>;dll=<core> -version` ile
  java host üzerinden yüklenir.
- **Self-update**: `/up/dl/launcher/info` SHA karşılaştırması → indir/doğrula →
  çalışan EXE'yi `.old` yapıp yenisini yerleştir → yeniden başlat.
- **HWID**: `sha256("dkk1|" + MachineGuid + "|" + C: serial + "|" + hostname)` — C++ ile birebir.
- Log: `%APPDATA%\RuntimeHost\launcher.log`.

## Build

```powershell
cargo build --release
# çıktı: target\release\RuntimeHost.exe  (~5.5 MB, LTO+strip)
```

## Gizli CLI (C++ launcher ile uyumlu)

| Komut | Anlam |
|---|---|
| `RuntimeHost.exe --inject <pid> [dll] [--edition modern\|legacy]` | GUI'siz enjeksiyon |
| `RuntimeHost.exe --token-test <file>` | Offline token doğrula (0 geçerli, 2 geçersiz/süresi dolmuş, 3 okunamadı) |
| `RuntimeHost.exe --selftest` | HWID + DPAPI round-trip testi (mevcut kayıt korunur) |
| `RuntimeHost.exe --license-set <key>` | QA/destek: DPAPI kayıt yaz |
| `RuntimeHost.exe --license-clear` | QA/destek: kayıt sil |
| `RuntimeHost.exe --core-fetch <key> [--edition modern\|legacy]` | QA: lisans kapılı core indirme + önbellek yolunu çalıştırır |

Çevre değişkenleri: `DRIP_NO_UPDATE=1` self-update kontrolünü kapatır (test için).

Not: resmi build `..\build.ps1` üzerinden yapılır; script `--remap-path-prefix` ile
mutlak kullanıcı/Cargo yollarını EXE'den temizler ve `/PDBALTPATH` verir. Doğrudan
`cargo build --release` çalıştırırsan aynı bayrakları `CARGO_ENCODED_RUSTFLAGS` ile
vermeyi unutma, aksi halde panic yerleri `C:\Users\<ad>\.cargo\...` yollarını taşır.

## Doğrulananlar

- HWID bağımsız hesapla birebir: `bcb79f7c…80035`
- DPAPI yaz/oku round-trip
- Sunucudan alınan gerçek imzalı token → `--token-test` = 0
- Canlı lisans doğrulaması: "License active - 30 days left" (panelde makineye bağlı geçici key ile)
- CLI çıkış kodları: token 0/2/3, inject 3/5, selftest 0
- GUI: "Host Process for Windows Services" başlığı, License/Target/Inject/Log bölümleri

## Kalanlar

- Windows kaynakları: sürüm bilgisi + ikon (C++ tarafındaki `version.rc`/`appicon.ico`)
- Depoya entegrasyon (C++ launcher'ın yerine geçiş) ve dağıtım/duyuru
- Gerçek Minecraft ile enjeksiyon testi (oyun çalışırken)
