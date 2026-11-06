# 💊 Fe Supplement Reminder Box

> **Kotak Pengingat Suplemen Zat Besi (Fe) Pintar Berbasis IoT ESP32-C3 & Telegram Bot**

Proyek ini adalah sistem pengingat minum suplemen zat besi / obat berbasis Internet of Things (IoT) yang mengombinasikan **boks fisik pintar** (LED & buzzer alarm) dengan **Bot Telegram interaktif**. Sistem ini dirancang untuk meningkatkan kepatuhan konsumsi suplemen harian bagi ibu hamil, remaja putri, maupun pasien yang membutuhkan asupan rutin.

Proyek ini bersifat **inklusif**: pengguna yang memiliki perangkat boks fisik dapat menghubungkan perangkatnya agar alarm fisik berbunyi saat jadwal tiba, sedangkan pengguna yang **tidak memiliki perangkat fisik** tetap dapat menikmati layanan pengingat rutin secara gratis langsung melalui chat Telegram.

---

## 🌟 Fitur Utama

- **Dual-Reminder System**: Notifikasi pesan di Telegram dan alarm suara (buzzer) + visual (LED) pada boks fisik.
- **Inklusif (Device + Telegram-Only)**: Pengguna tanpa alat fisik tetap dapat mendaftar dan menerima jadwal pengingat di Telegram.
- **1 Device = 1 Owner Binding**: Setiap boks memiliki **Device ID unik** (berbasis hardware MAC). Hanya pemilik sah terdaftar yang dapat memicu alarm fisik boksnya.
- **Penjadwalan Fleksibel**:
  - Pilihan hari: Setiap hari atau kombinasi hari tertentu (misal: Senin, Rabu, Kamis / Jumat saja).
  - Pilihan jam pengingat kustom (format `JJ:MM`, misal: `20:00` atau `06:30`).
- **🔕 Remote Stop Alarm via Telegram**: Jika boks berdering dan berada jauh dari jangkauan, pemilik dapat mematikan alarm langsung lewat tombol inline Telegram atau perintah `/matikan`.
- **🎵 Melodi Nada Dering Alarm Kustom (Samsung & Nokia)**: Menggunakan pola `RingtoneStep` pada file terpisah [`ringtones.h`](file:///D:/00_Project/Fe_supplement_reminder_box/fe_supplement_reminder_fw/include/ringtones.h) dengan pilihan motif Samsung *"Over the Horizon"* dan *"Nokia Tune"*, tersedia pilihan frekuensi asli dan nada resonansi tinggi (+1 oktaf) yang dioptimalkan untuk kekerasan suara maksimal pada buzzer piezo. Dijalankan pada *Dedicated FreeRTOS Task* dengan penjadwalan preemptive independen sehingga timing nada dan jeda melodi berbunyi sangat presisi tanpa terganggu (*jitter-free*) oleh lalu lintas jaringan HTTPS Telegram. Nada dapat diatur & dites langsung via perintah `/nada`.
- **🔘 Multifungsi Tombol Fisik Non-Blocking (Pin 2)**:
  - **1x Tekan (saat alarm berbunyi)**: Langsung mematikan/membisukan alarm buzzer.
  - **3x Klik Cepat**: Mereset pengaturan Wi-Fi (menghapus bersih data SSID/password dari Flash NVS & restart ke mode AP).
  - **Tahan 20 Detik**: Mereset kepemilikan perangkat (unpair) dan secara otomatis mengirim notifikasi peringatan ke Telegram pemilik lama.
  - **Proteksi Anti-Freeze**: Menggunakan *finite state machine non-blocking* sehingga jika tombol tertekan lama atau short ke GND, sistem tetap beroperasi normal tanpa deadlock.
- **Auto-Reconnect Simultan & AP Failover Non-Blocking**: Saat koneksi Wi-Fi terputus, boks secara otomatis membuka kembali hotspot AP portal (`Fe-Reminder-Setup`) agar dapat dikonfigurasi ulang oleh pengguna via browser, sekaligus secara simultan terus mencoba menyambung kembali (*auto-reconnect*) ke router tersimpan di latar belakang. Begitu sinyal router pulih, hotspot AP otomatis ditutup, boks beralih ke `WIFI_STA`, dan waktu NTP disinkronkan berkala setiap 1 jam untuk menjaga presisi tinggi RTC DS3231.
- **Proteksi Hardware Bus I2C**: Dilengkapi timeout 1000 ms pada jalur I2C untuk mencegah CPU hang jika koneksi modul RTC terlepas atau berderau (noisy).
- **Penyimpanan Permanen & Kuota Aman (Maks 20 User)**: Jadwal dan kepemilikan tersimpan di NVS Flash dengan batas kuota aman 20 pengguna untuk menjamin kestabilan memori jangka panjang tanpa risiko fragmentasi Heap atau crash OOM.
- **Efisiensi Daya, Hemat Arus (8.5 dBm) & Polling Cerdas**: Menggunakan daya pancar RF terkontrol (**8.5 dBm**) guna mencegah lonjakan arus catu daya (*anti-brownout*) dan panas chip, dipadukan mode *Station Murni* (`WIFI_STA`), 802.11 Wi-Fi modem sleep, serta throttling FreeRTOS CPU idle delay (15 ms) sehingga konsumsi arus sangat hemat dan chip tetap dingin (~40°C), serta respon bot Telegram sangat cepat via Adaptive Polling (800 ms/1000 ms/1500 ms).
- **Skema Partisi Flash 3 MB (`huge_app.csv`)**: Kapasitas alokasi partisi firmware diperbesar dari 1.25 MB menjadi 3 MB (utilisasi turun dari 84% menjadi **34.9%**), menyisakan ruang memori yang sangat longgar untuk stabilitas dan ekspansi.
- **Keamanan Kredensial**: Token bot disimpan di file lokal yang diproteksi oleh `.gitignore`, mencegah kebocoran kunci akses ke repositori publik.

---

## 🛠️ Kebutuhan Hardware & Skema Rangkaian

### Komponen
1. **ESP32-C3 Super Mini** (atau board ESP32-C3 lainnya)
2. **RTC DS3231 Module** (I2C) + Baterai CR2032
3. **Active/Passive Buzzer Module** (Low-level trigger)
4. **LED Indikator** (3mm / 5mm) + Resistor 220Ω / 330Ω
5. **Push Button** (Tactile switch)
6. Sumber daya: Kabel USB Type-C (5V)

### Konfigurasi Pin (Wiring)

| Komponen | Pin Komponen | Pin ESP32-C3 | Keterangan |
| :--- | :--- | :--- | :--- |
| **RTC DS3231** | SDA | `GPIO 8` | I2C Data Hardware |
| | SCL | `GPIO 9` | I2C Clock Hardware |
| | VCC | `3.3V` / `5V` | Sumber Tegangan |
| | GND | `GND` | Ground |
| **Buzzer** | I/O / SIG | `GPIO 3` | Sinyal PWM (LEDC Channel 0, 2.7 kHz) |
| | VCC | `3.3V` / `5V` | Sumber Tegangan |
| | GND | `GND` | Ground |
| **LED Indikator**| Anoda (+) | `GPIO 1` | Lewat resistor 220Ω/330Ω |
| | Katoda (-) | `GND` | Ground |
| **Tombol Reset** | Pin 1 | `GPIO 2` | Konfigurasi `INPUT_PULLUP` internal |
| | Pin 2 | `GND` | Ground |

---

## 🚀 Panduan Setup & Instalasi

### 1. Prasyarat Software
- [VS Code](https://code.visualstudio.com/) dengan ekstensi [PlatformIO IDE](https://platformio.org/).
- [Git](https://git-scm.com/) terinstal di komputer.
- Akun Telegram aktif.

---

### 2. Buat Bot Telegram & Dapatkan Token
1. Buka aplikasi Telegram dan cari **`@BotFather`**.
2. Kirim perintah `/newbot`.
3. Masukkan nama bot (contoh: `Fe Supplement Reminder Bot`) dan username bot (harus berakhiran `bot`, contoh: `fe_reminder_box_bot`).
4. BotFather akan memberikan **HTTP API Bot Token** (format contoh: `1234567890:ABCdefGhIJKlmNoPQRsTUVwxyZ`). Simpan token ini dengan aman!

---

### 3. Kloning Repositori & Konfigurasi Token
1. Buka terminal atau Command Prompt / PowerShell:
   ```bash
   git clone https://github.com/username/Fe_supplement_reminder_box.git
   cd Fe_supplement_reminder_box
   ```
2. Buat salinan file template konfigurasi rahasia:
   ```bash
   cp fe_supplement_reminder_fw/include/secrets.h.example fe_supplement_reminder_fw/include/secrets.h
   ```
3. Buka file `fe_supplement_reminder_fw/include/secrets.h` dan masukkan token bot Anda:
   ```cpp
   #pragma once

   // Ganti dengan token bot Telegram yang Anda dapatkan dari @BotFather
   #define BOT_TOKEN "1234567890:ABCdefGhIJKlmNoPQRsTUVwxyZ"
   ```
   > [!IMPORTANT]
   > File `secrets.h` otomatis diabaikan oleh Git via `.gitignore` sehingga token Anda tidak akan pernah terunggah ke GitHub.

---

### 4. Build & Flash Firmware ke ESP32-C3
1. Hubungkan ESP32-C3 ke komputer menggunakan kabel USB data.
2. Buka folder proyek di VS Code (PlatformIO).
3. Anda dapat mengompilasi dan mengunggah kode melalui PlatformIO UI atau terminal:
   ```powershell
   # Mengompilasi proyek
   platformio run -d fe_supplement_reminder_fw

   # Mengunggah firmware ke ESP32-C3
   platformio run -d fe_supplement_reminder_fw -t upload

   # Membuka Serial Monitor (baud rate 115200)
   platformio run -d fe_supplement_reminder_fw -t monitor
   ```

---

### 5. Konfigurasi Jaringan Wi-Fi Boks (WiFiManager)
Saat pertama kali dinyalakan (atau setelah reset Wi-Fi):
1. ESP32 akan menyalakan Wi-Fi Access Point sendiri bernama: **`Fe-Reminder-Setup`**.
2. Sambungkan HP atau Laptop Anda ke Wi-Fi tersebut (Password default: **`12345678`**).
3. Halaman Captive Portal akan muncul secara otomatis (atau buka browser ke alamat `192.168.4.1`).
4. Pilih **Configure WiFi**, pilih SSID Wi-Fi rumah/kantor Anda, masukkan passwordnya, dan klik **Save**.
5. Boks akan otomatis restart dan terhubung ke internet.

---

## 📱 Panduan Penggunaan Bot Telegram

Setelah bot aktif dan boks terhubung ke Wi-Fi:

### 1. Memulai Bot
Cari username bot Anda di Telegram dan tekan tombol **Start** atau kirim perintah `/start`.
Bot akan menampilkan menu keyboard interaktif:
- `📋 Status Pengingat`
- `⏰ Atur Jadwal`
- `⚙️ Hubungkan Device`
- `🔕 Berhenti Langganan`
- `ℹ️ Bantuan`

### 2. Mengatur Jadwal Pengingat
1. Tekan tombol menu **`⏰ Atur Jadwal`** atau kirim perintah `/setjadwal`.
2. Bot akan menampilkan pilihan hari:
   - **Setiap Hari** (default)
   - **Pilih Hari Tertentu** (contoh: Senin, Rabu, Kamis / Setiap Jumat).
3. Masukkan jam pengingat dalam format `JJ:MM` (contoh: `20:00` atau `06:30`).

### 3. Menghubungkan Box Fisik (Pairing)
1. Lihat **Device ID** pada boks Anda (dapat dilihat melalui Serial Monitor atau stiker perangkat, format: `FEBOX-XXXX`).
2. Kirim perintah:
   ```text
   /pair FEBOX-XXXX
   ```
   atau klik menu **`⚙️ Hubungkan Device`** dan masukkan ID tersebut.
3. Setelah berhasil terhubung, alarm fisik (LED & buzzer) pada boks hanya akan berbunyi khusus saat jadwal pengingat Anda tiba!

### 4. Mematikan Alarm Buzzer Jarak Jauh
Saat waktu pengingat tiba dan alarm berbunyi:
- Bot akan mengirimkan pesan pengingat ke Telegram Anda yang dilengkapi tombol inline:
  ```text
  [ 🔕 Matikan Alarm Buzzer ]
  ```
- Klik tombol tersebut, atau kirim perintah `/matikan` / `/stop_alarm`. Buzzer pada boks akan seketika berhenti berbunyi.

---

## 🔘 Panduan Aksi Tombol Fisik (Pin 2)

| Aksi | Kondisi | Fungsi | Indikasi |
| :--- | :--- | :--- | :--- |
| **1x Tekan Singkat** | Alarm sedang berbunyi | Mematikan alarm buzzer seketika | Buzzer langsung mati |
| **3x Klik Cepat** | Alarm diam (< 450 ms interval) | Menghapus data Wi-Fi dari Flash & restart ke mode AP | 3x beep ceria & ESP restart membuka AP `Fe-Reminder-Setup` |
| **Tahan 20 Detik** | Alarm diam | Mereset kepemilikan box (unpair) | Terdengar *beep* di detik 5, 10, 15, lalu 3 *beep* panjang + notifikasi ke Telegram pemilik lama |

---

## 📁 Struktur Direktori Proyek

```text
Fe_supplement_reminder_box/
├── .gitignore                          # Filter file rahasia & build artifacts
├── README.md                           # Dokumentasi lengkap proyek
└── fe_supplement_reminder_fw/          # Firmware PlatformIO untuk ESP32-C3
    ├── .gitignore                      # Proteksi folder firmware
    ├── platformio.ini                  # Konfigurasi board, framework, lib_deps
    ├── include/
    │   ├── README
    │   ├── ringtones.h                 # Definisi melodi & notasi nada buzzer (Samsung, Nokia)
    │   ├── secrets.h.example           # Template aman kredensial bot token
    │   └── secrets.h                   # Kredensial lokal (tidak masuk git)
    ├── lib/                            # Pustaka kustom tambahan (jika ada)
    ├── src/
    │   └── main.cpp                    # Logika utama (WiFi, Telegram, RTC, NVS, Tombol)
    └── test/                           # Unit testing
```

---

## ❓ FAQ & Troubleshooting

<details>
<summary><b>1. Mengapa bot tidak merespons pesan Telegram saya?</b></summary>
Pastikan:
- ESP32 terhubung ke Wi-Fi dengan akses internet yang stabil.
- Token di <code>include/secrets.h</code> sudah sesuai dengan token dari <code>@BotFather</code>.
- Tidak ada bot instance lain yang menggunakan token yang sama secara bersamaan (conflict polling).
</details>

<details>
<summary><b>2. Mengapa waktu pada pengingat tidak sesuai dengan waktu lokal saya?</b></summary>
Firmware dikonfigurasi default untuk Waktu Indonesia Barat (WIB / GMT+7). Boks akan otomatis menyinkronkan waktu dengan server NTP saat terhubung ke Wi-Fi. Modul RTC DS3231 akan menjaga waktu tetap akurat meskipun boks dimatikan.
</details>

<details>
<summary><b>3. Bagaimana jika saya ingin memindahkan boks ke jaringan Wi-Fi baru?</b></summary>
Tekan tombol fisik pada Pin 2 sebanyak **3 kali berturut-turut dengan cepat**. ESP32 akan mereset pengaturan Wi-Fi dan menyalakan kembali hotspot <code>Fe-Reminder-Setup</code> untuk dikonfigurasi ulang.
</details>

<details>
<summary><b>4. Bagaimana cara menyerahkan boks ke pengguna baru?</b></summary>
Tekan dan tahan tombol fisik pada Pin 2 selama **20 detik**. Kepemilikan boks akan terhapus, pemilik lama akan menerima notifikasi Telegram bahwa perangkat terputus, dan pengguna baru dapat menghubungkannya kembali dengan perintah <code>/pair FEBOX-XXXX</code>.
</details>

---

## 📄 Lisensi
Proyek ini dilisensikan di bawah [MIT License](LICENSE).
