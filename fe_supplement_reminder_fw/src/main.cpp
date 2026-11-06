#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include "RTClib.h"
#include "esp_bt.h"
#include "time.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

// ==================== KONFIGURASI ====================

// Muat BOT_TOKEN dari secrets.h
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("include/secrets.h")
#include "include/secrets.h"
#else
#warning "File secrets.h tidak ditemukan! Salin include/secrets.h.example menjadi include/secrets.h dan isi BOT_TOKEN Anda."
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN_HERE"
#endif

// Konfigurasi Kredensial AP WiFiManager
const char* AP_SSID = "Fe-Reminder-Setup";
const char* AP_PASS = "12345678";

// Pin ESP32-C3 Super Mini
#define BUZZER_PIN  3  // Pin I/O Modul Buzzer (Low-Level Trigger)
#define LED_PIN     1  // Indikator LED
#define RESET_PIN   2  // Tombol reset Wi-Fi

// Bus I2C Hardware bawaan ESP32-C3
#define I2C_SDA     8  
#define I2C_SCL     9  

// Konfigurasi LEDC PWM Buzzer
#define PWM_CHANNEL 0
#define PWM_RES     8

// Objek RTC
RTC_DS3231 rtc;
// RTC_DS1307 rtc;

const char* daysOfTheWeek[7] = {
    "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"
};

const char* monthsOfTheYear[12] = {
    "Januari", "Februari", "Maret", "April", "Mei", "Juni",
    "Juli", "Agustus", "September", "Oktober", "November", "Desember"
};

// NTP Server
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;
const int   daylightOffset_sec = 0;

// Client Telegram
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ==================== STRUKTUR DATA SUBSCRIBER & JADWAL ====================

struct Subscriber {
    String chatId;
    String name;
    int hour;             // 0-23 (default 20:00)
    int minute;           // 0-59 (default 0)
    uint8_t daysMask;     // Bit 0=Minggu, 1=Senin, 2=Selasa, 3=Rabu, 4=Kamis, 5=Jumat, 6=Sabtu (default 32 = Jumat)
    bool active;          // true jika aktif berlangganan
    int lastTriggeredDay; // Tanggal hari terakhir alarm terpicu (-1 saat inisialisasi)
    int lastTriggeredMin; // Menit terakhir alarm terpicu (-1 saat inisialisasi)
};

// ==================== IDENTITAS PERANGKAT & PEMILIK ====================

String deviceId = "";
String deviceOwnerChatId = "";
String deviceOwnerName = "";

bool isDeviceOwner(const String& chatId) {
    return (deviceOwnerChatId.length() > 0 && deviceOwnerChatId == chatId);
}

void initDeviceId() {
    #ifdef CUSTOM_DEVICE_ID
    if (String(CUSTOM_DEVICE_ID).length() > 0) {
        deviceId = String(CUSTOM_DEVICE_ID);
        deviceId.toUpperCase();
        Serial.printf("[Device] ID Perangkat (Kustom): %s\n", deviceId.c_str());
        return;
    }
    #endif

    uint64_t chipmac = ESP.getEfuseMac();
    char buf[20];
    snprintf(buf, sizeof(buf), "FEBOX-%02X%02X", (uint8_t)(chipmac >> 8), (uint8_t)(chipmac));
    deviceId = String(buf);
    deviceId.toUpperCase();
    Serial.printf("[Device] ID Perangkat FEBOX: %s\n", deviceId.c_str());
}

Preferences prefs;
std::vector<Subscriber> subscriberList;

void saveDeviceOwner() {
    prefs.begin("fe_bot", false);
    prefs.putString("owner_chat", deviceOwnerChatId);
    prefs.putString("owner_name", deviceOwnerName);
    prefs.end();
}

// Keyboard Menu Utama (Reply Keyboard di bawah layar chat Telegram)
const char MAIN_KEYBOARD[] = 
    "[[\"📋 Status & Jadwal\", \"⏰ Atur Jadwal\"],"
    " [\"📦 Tautkan Boks\", \"🔔 Berlangganan\"],"
    " [\"🧪 Tes Alarm\", \"❓ Bantuan\"]]";

// Inline Keyboards untuk Pengaturan Jadwal Interaktif
String getScheduleInlineKeyboard() {
    return "["
        "[{\"text\":\"📅 Tiap Jumat (20:00)\",\"callback_data\":\"set_fri_20\"},"
         "{\"text\":\"📅 Tiap Hari (20:00)\",\"callback_data\":\"set_daily_20\"}],"
        "[{\"text\":\"📅 Sen, Rab, Kam (20:00)\",\"callback_data\":\"set_mwth_20\"},"
         "{\"text\":\"📅 Sen & Kam (20:00)\",\"callback_data\":\"set_mth_20\"}],"
        "[{\"text\":\"🕐 Ubah Jam Saja\",\"callback_data\":\"menu_hour\"},"
         "{\"text\":\"🗓️ Pilih Hari Lain\",\"callback_data\":\"menu_day\"}],"
        "[{\"text\":\"📋 Cek Status Jadwal\",\"callback_data\":\"menu_status\"}]"
    "]";
}

String getHoursInlineKeyboard() {
    return "["
        "[{\"text\":\"🌅 06:00\",\"callback_data\":\"h_06_00\"},"
         "{\"text\":\"🌅 07:00\",\"callback_data\":\"h_07_00\"},"
         "{\"text\":\"☀️ 12:00\",\"callback_data\":\"h_12_00\"}],"
        "[{\"text\":\"🌙 19:00\",\"callback_data\":\"h_19_00\"},"
         "{\"text\":\"🌙 20:00\",\"callback_data\":\"h_20_00\"},"
         "{\"text\":\"🌙 21:00\",\"callback_data\":\"h_21_00\"}],"
        "[{\"text\":\"🌙 22:00\",\"callback_data\":\"h_22_00\"},"
         "{\"text\":\"🔙 Menu Jadwal\",\"callback_data\":\"menu_schedule\"}]"
    "]";
}

String getDaysInlineKeyboard() {
    return "["
        "[{\"text\":\"📅 Setiap Jumat (Remaja)\",\"callback_data\":\"d_fri\"},"
         "{\"text\":\"📅 Setiap Hari (Bumil)\",\"callback_data\":\"d_daily\"}],"
        "[{\"text\":\"📅 Senin, Rabu, Kamis\",\"callback_data\":\"d_mwth\"},"
         "{\"text\":\"📅 Senin & Kamis\",\"callback_data\":\"d_mth\"}],"
        "[{\"text\":\"📅 Hari Kerja (Sen-Jum)\",\"callback_data\":\"d_weekdays\"},"
         "{\"text\":\"📅 Akhir Pekan (Sab-Min)\",\"callback_data\":\"d_weekend\"}],"
        "[{\"text\":\"🔙 Menu Jadwal\",\"callback_data\":\"menu_schedule\"}]"
    "]";
}

// ==================== KONTROL AUDIO (BUZZER) ====================

void initBuzzer() {
    ledcSetup(PWM_CHANNEL, 2700, PWM_RES);
    ledcAttachPin(BUZZER_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
}

void buzzerTone(unsigned int freq) {
    if (freq == 0) {
        ledcWrite(PWM_CHANNEL, 0);
        return;
    }
    ledcWriteTone(PWM_CHANNEL, freq);
    ledcWrite(PWM_CHANNEL, 128);
}

void buzzerNoTone() {
    ledcWrite(PWM_CHANNEL, 0);
}

void beep(int count, int durationMs, int pauseMs = 100, unsigned int freq = 2700) {
    for (int i = 0; i < count; i++) {
        buzzerTone(freq);
        delay(durationMs);
        buzzerNoTone();
        if (i < count - 1) {
            delay(pauseMs);
        }
    }
}

// Nada alarm khusus pengingat minum obat
void playAlarmTone() {
    for (int k = 0; k < 5; k++) {
        beep(2, 100, 80, 2700);
        delay(200);
    }
}

// ==================== HELPER FORMAT & PARSING ====================

String formatDays(uint8_t mask) {
    if (mask == 0) return "Tidak ada (Nonaktif)";
    if (mask == 127) return "Setiap Hari (Senin - Minggu)";
    if (mask == 32) return "Setiap Jumat (Program Remaja)";
    if (mask == ((1 << 1) | (1 << 3) | (1 << 4))) return "Senin, Rabu, Kamis";
    if (mask == ((1 << 1) | (1 << 4))) return "Senin & Kamis";
    if (mask == ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5))) return "Senin s/d Jumat (Hari Kerja)";
    if (mask == ((1 << 0) | (1 << 6))) return "Akhir Pekan (Sabtu & Minggu)";

    const char* dayNames[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    String res = "";
    for (int i = 0; i < 7; i++) {
        if (mask & (1 << i)) {
            if (res.length() > 0) res += ", ";
            res += dayNames[i];
        }
    }
    return res;
}

String formatTime(int h, int m) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d WIB", h, m);
    return String(buf);
}

bool parseTime(String s, int &outH, int &outM) {
    s.trim();
    int sep = s.indexOf(':');
    if (sep == -1) sep = s.indexOf('.');
    if (sep == -1) return false;

    String hStr = s.substring(0, sep);
    String mStr = s.substring(sep + 1);
    hStr.trim();
    mStr.trim();

    if (hStr.length() == 0 || mStr.length() == 0) return false;
    for (size_t i = 0; i < hStr.length(); i++) {
        if (!isDigit(hStr[i])) return false;
    }
    for (size_t i = 0; i < mStr.length(); i++) {
        if (!isDigit(mStr[i])) return false;
    }

    int h = hStr.toInt();
    int m = mStr.toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;

    outH = h;
    outM = m;
    return true;
}

uint8_t parseDays(String s) {
    s.toLowerCase();
    s.trim();

    if (s.indexOf("tiap hari") != -1 || s.indexOf("setiap hari") != -1 || 
        s.indexOf("daily") != -1 || s.indexOf("semua") != -1) {
        return 127;
    }
    if (s.indexOf("hari kerja") != -1 || s.indexOf("senin-jumat") != -1 || s.indexOf("senin - jumat") != -1) {
        return ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
    }
    if (s.indexOf("akhir pekan") != -1 || s.indexOf("weekend") != -1) {
        return ((1 << 0) | (1 << 6));
    }

    uint8_t mask = 0;
    if (s.indexOf("minggu") != -1 || s.indexOf("ahad") != -1) mask |= (1 << 0);
    if (s.indexOf("senin") != -1) mask |= (1 << 1);
    if (s.indexOf("selasa") != -1) mask |= (1 << 2);
    if (s.indexOf("rabu") != -1) mask |= (1 << 3);
    if (s.indexOf("kamis") != -1) mask |= (1 << 4);
    if (s.indexOf("jumat") != -1 || s.indexOf("jum'at") != -1) mask |= (1 << 5);
    if (s.indexOf("sabtu") != -1) mask |= (1 << 6);

    return mask;
}

// ==================== MANAJEMEN PENYIMPANAN NVS ====================

Subscriber* findSubscriber(const String& chatId) {
    for (size_t i = 0; i < subscriberList.size(); i++) {
        if (subscriberList[i].chatId == chatId) {
            return &subscriberList[i];
        }
    }
    return nullptr;
}

Subscriber& getOrCreateSubscriber(const String& chatId, const String& name) {
    Subscriber* existing = findSubscriber(chatId);
    if (existing != nullptr) {
        if (name.length() > 0 && name != "Teman") existing->name = name;
        return *existing;
    }

    Subscriber newSub;
    newSub.chatId = chatId;
    newSub.name = (name.length() > 0) ? name : "Teman";
    newSub.hour = 20;
    newSub.minute = 0;
    newSub.daysMask = 32; // Default: Jumat
    newSub.active = false; // Default: Belum aktif sebelum user konfirmasi/pilih
    newSub.lastTriggeredDay = -1;
    newSub.lastTriggeredMin = -1;
    subscriberList.push_back(newSub);
    return subscriberList.back();
}

void parseSubscriberEntry(const String& entry) {
    // Format: chatId|hour|minute|daysMask|active|name
    int idx = 0;
    int next = entry.indexOf('|', idx);
    if (next == -1) return;
    String id = entry.substring(idx, next);

    idx = next + 1;
    next = entry.indexOf('|', idx);
    int h = (next != -1) ? entry.substring(idx, next).toInt() : 20;

    if (next != -1) {
        idx = next + 1;
        next = entry.indexOf('|', idx);
    }
    int m = (next != -1) ? entry.substring(idx, next).toInt() : 0;

    if (next != -1) {
        idx = next + 1;
        next = entry.indexOf('|', idx);
    }
    int d = (next != -1) ? entry.substring(idx, next).toInt() : 32;

    if (next != -1) {
        idx = next + 1;
        next = entry.indexOf('|', idx);
    }
    bool act = (next != -1) ? (entry.substring(idx, next).toInt() == 1) : true;

    String name = (next != -1) ? entry.substring(next + 1) : "Teman";
    if (name.length() == 0) name = "Teman";

    Subscriber s;
    s.chatId = id;
    s.name = name;
    s.hour = h;
    s.minute = m;
    s.daysMask = (uint8_t)d;
    s.active = act;
    s.lastTriggeredDay = -1;
    s.lastTriggeredMin = -1;
    subscriberList.push_back(s);
}

void saveSubscribers() {
    String data = "";
    for (size_t i = 0; i < subscriberList.size(); i++) {
        const auto& s = subscriberList[i];
        data += s.chatId + "|" + 
                String(s.hour) + "|" + 
                String(s.minute) + "|" + 
                String(s.daysMask) + "|" + 
                String(s.active ? 1 : 0) + "|" + 
                s.name;
        if (i < subscriberList.size() - 1) data += ";";
    }
    prefs.begin("fe_bot", false);
    prefs.putString("v2_subs", data);
    prefs.end();
}

void loadSubscribers() {
    prefs.begin("fe_bot", false);
    deviceOwnerChatId = prefs.getString("owner_chat", "");
    deviceOwnerName = prefs.getString("owner_name", "");
    String data = prefs.getString("v2_subs", "");

    if (data.length() == 0) {
        // Cek migrasi dari format v1 (hanya comma-separated chat_id)
        String oldChats = prefs.getString("chats", "");
        if (oldChats.length() > 0) {
            int start = 0;
            int end = oldChats.indexOf(',');
            while (end != -1) {
                Subscriber s;
                s.chatId = oldChats.substring(start, end);
                s.name = "Teman";
                s.hour = 20;
                s.minute = 0;
                s.daysMask = 32;
                s.active = true;
                s.lastTriggeredDay = -1;
                s.lastTriggeredMin = -1;
                subscriberList.push_back(s);
                start = end + 1;
                end = oldChats.indexOf(',', start);
            }
            if (start < oldChats.length()) {
                Subscriber s;
                s.chatId = oldChats.substring(start);
                s.name = "Teman";
                s.hour = 20;
                s.minute = 0;
                s.daysMask = 32;
                s.active = true;
                s.lastTriggeredDay = -1;
                s.lastTriggeredMin = -1;
                subscriberList.push_back(s);
            }
            prefs.end();
            saveSubscribers();
            Serial.printf("[Storage] Migrasi data lama sukses: %d subscriber.\n", subscriberList.size());
            return;
        }
    }
    prefs.end();

    subscriberList.clear();
    if (data.length() > 0) {
        int start = 0;
        int end = data.indexOf(';');
        while (end != -1) {
            String entry = data.substring(start, end);
            parseSubscriberEntry(entry);
            start = end + 1;
            end = data.indexOf(';', start);
        }
        if (start < data.length()) {
            parseSubscriberEntry(data.substring(start));
        }
    }
    Serial.printf("[Storage] Total subscriber terdaftar: %d\n", subscriberList.size());
}

// ==================== TAMPILAN PESAN & MENU BOT ====================

void sendScheduleMenu(const String& chat_id, const String& from_name) {
    Subscriber* s = findSubscriber(chat_id);
    int curH = (s != nullptr) ? s->hour : 20;
    int curM = (s != nullptr) ? s->minute : 0;
    uint8_t curD = (s != nullptr) ? s->daysMask : 32;
    bool isAct = (s != nullptr && s->active);

    String msg = "⏰ *PENGATURAN JADWAL PENGINGAT Fe* 🩸\n\n";
    msg += "Jadwal Anda saat ini:\n";
    msg += "📅 Hari: *" + formatDays(curD) + "*\n";
    msg += "⏰ Jam: *" + formatTime(curH, curM) + "*\n";
    msg += "🔔 Status: *" + String(isAct ? "Aktif ✅" : "Nonaktif ❌") + "*\n\n";
    msg += "Pilih preset cepat di bawah ini atau atur waktu secara mandiri:";

    bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", getScheduleInlineKeyboard());

    // Tampilkan panduan format teks kustom
    String helpText = "✏️ *Atau ketik langsung jadwal bebas Anda:*\n";
    helpText += "• `/jadwal jumat 20:30`\n";
    helpText += "• `/jadwal senin,rabu,kamis 20:00`\n";
    helpText += "• `/jadwal tiap hari 07:00`\n";
    helpText += "• `/jam 21:00` _(ubah jam saja)_\n";
    helpText += "• `/hari senin,kamis` _(ubah hari saja)_";
    bot.sendMessage(chat_id, helpText, "Markdown");
}

void sendDeviceLinkMenu(const String& chat_id, const String& from_name) {
    if (isDeviceOwner(chat_id)) {
        String msg = "📦 *STATUS PERANGKAT FEBOX*\n\n";
        msg += "✅ *Boks ini telah tertaut ke akun Anda!*\n";
        msg += "• ID Perangkat: `" + deviceId + "`\n";
        msg += "• Pemilik Terdaftar: *" + deviceOwnerName + "*\n";
        msg += "• Alarm Fisik (Buzzer): *Aktif untuk Anda* 🔊\n\n";
        msg += "Setiap kali jadwal pengingat Anda tiba, boks fisik ini akan berbunyi dan LED akan menyala.\n\n";
        msg += "💡 _Jika ingin melepaskan boks ini agar bisa digunakan akun lain, klik tombol di bawah atau ketik `/lepaskan`._";

        String kb = "[ [{\"text\":\"🔓 Lepaskan Tautan Boks\",\"callback_data\":\"unpair_device\"}] ]";
        bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", kb);
    }
    else if (deviceOwnerChatId.length() > 0) {
        String msg = "📦 *STATUS PERANGKAT FEBOX*\n\n";
        msg += "⚠️ *Boks Sedang Dimiliki Pengguna Lain*\n";
        msg += "Boks fisik ini saat ini sudah ditautkan ke pengguna lain (*" + deviceOwnerName + "*).\n";
        msg += "Satu boks fisik FEBOX hanya dapat dimiliki oleh 1 akun pengguna.\n\n";
        msg += "📱 *Tetap Tenang!* Anda tetap dapat menggunakan bot ini untuk menerima pengingat rutin melalui notifikasi Telegram!\n\n";
        msg += "💡 _Pemilik boks dapat me-reset kepemilikan dengan menekan tombol fisik boks selama 20 detik atau mengetik `/lepaskan`._";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else {
        String msg = "📦 *PENAUTAN PERANGKAT FEBOX*\n\n";
        msg += "Boks fisik FEBOX ini saat ini berstatus *Tersedia* (belum memiliki pemilik).\n\n";
        msg += "Untuk menautkan boks fisik ini ke akun Anda, ketik:\n";
        msg += "`/tautkan " + deviceId + "`\n\n";
        msg += "Atau klik tombol cepat di bawah ini:";

        String kb = "[ [{\"text\":\"🔗 Tautkan ke Boks Ini (" + deviceId + ")\",\"callback_data\":\"pair_current_device\"}] ]";
        bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", kb);

        String note = "📱 *Informasi Tambahan:*\nJika Anda tidak memiliki perangkat fisik FEBOX, Anda *tidak perlu* menautkan boks. Layanan pengingat via pesan Telegram tetap dapat Anda gunakan 100%!";
        bot.sendMessage(chat_id, note, "Markdown");
    }
}

void handlePairDevice(const String& chat_id, const String& from_name, String inputId) {
    inputId.trim();
    inputId.toUpperCase();

    if (inputId == deviceId) {
        if (deviceOwnerChatId.length() > 0 && deviceOwnerChatId != chat_id) {
            String msg = "⚠️ *Boks Sudah Dimiliki Pengguna Lain*\n\n";
            msg += "Boks fisik dengan ID `" + deviceId + "` saat ini sudah ditautkan ke akun *" + deviceOwnerName + "*.\n\n";
            msg += "Satu boks fisik FEBOX hanya dapat dimiliki oleh satu pengguna.\n";
            msg += "• Anda tetap dapat menikmati layanan pengingat gratis lewat Telegram bot ini.\n";
            msg += "• Jika Anda adalah pemilik sah yang baru, silakan tekan tombol fisik pada boks selama 20 detik untuk me-reset kepemilikan.";
            bot.sendMessage(chat_id, msg, "Markdown");
            return;
        }

        if (isDeviceOwner(chat_id)) {
            bot.sendMessage(chat_id, "ℹ️ Akun Anda sudah terdaftar sebagai pemilik resmi boks fisik `" + deviceId + "`.", "Markdown");
            return;
        }

        // Tautkan device ke user ini
        deviceOwnerChatId = chat_id;
        deviceOwnerName = from_name;
        saveDeviceOwner();

        // Pastikan user terdaftar sebagai subscriber aktif
        Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
        s.active = true;
        saveSubscribers();

        // Audio feedback ceria pada alat fisik (2 nada tinggi)
        beep(2, 100, 80, 3000);

        String msg = "🎉 *BERHASIL MENAUTKAN PERANGKAT!* 📦\n\n";
        msg += "Boks *FEBOX (" + deviceId + ")* kini resmi terhubung dengan akun Anda (*" + from_name + "*).\n\n";
        msg += "🔊 *Fitur Unggulan Aktif:*\n";
        msg += "• Alarm buzzer suara pada boks fisik akan berbunyi khusus saat jadwal minum obat Anda tiba!\n";
        msg += "• Jadwal Anda: *" + formatDays(s.daysMask) + "* pukul *" + formatTime(s.hour, s.minute) + "*\n\n";
        msg += "💡 _Untuk me-reset kepemilikan boks sewaktu-waktu, Anda bisa mengetik `/lepaskan` atau menekan tombol fisik boks selama 20 detik._";

        bot.sendMessage(chat_id, msg, "Markdown");
        Serial.printf("[Device] Boks %s berhasil ditautkan ke %s (%s)\n", deviceId.c_str(), from_name.c_str(), chat_id.c_str());
    } else {
        String msg = "❌ *ID Perangkat Salah / Tidak Sesuai*\n\n";
        msg += "ID yang Anda masukkan (*" + inputId + "*) tidak cocok dengan perangkat ini.\n\n";
        msg += "Pastikan Anda memasukkan ID yang tertera pada boks FEBOX Anda.\n";
        msg += "_Contoh penggunaan:_ `/tautkan " + deviceId + "`";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
}

void handleUnpairDevice(const String& chat_id, const String& from_name) {
    if (!isDeviceOwner(chat_id)) {
        bot.sendMessage(chat_id, "Akun Anda saat ini tidak sedang menautkan perangkat boks fisik FEBOX.", "");
        return;
    }

    deviceOwnerChatId = "";
    deviceOwnerName = "";
    saveDeviceOwner();

    // Feedback audio nada rendah 1x
    beep(1, 250, 0, 1600);

    String msg = "🔓 *Tautan Perangkat Berhasil Dilepas*\n\n";
    msg += "Boks FEBOX (" + deviceId + ") kini berstatus bebas dan dapat didaftarkan oleh akun lain.\n\n";
    msg += "📱 *Catatan:* Akun Anda tetap aktif berlangganan pengingat via notifikasi pesan Telegram.";
    bot.sendMessage(chat_id, msg, "Markdown");
    Serial.println("[Device] Tautan pemilik boks telah dilepas via Telegram.");
}

void sendStartMessage(const String& chat_id, const String& from_name) {
    Subscriber* s = findSubscriber(chat_id);
    bool isSubbed = (s != nullptr && s->active);
    bool isOwner = isDeviceOwner(chat_id);

    String greeting = "👋 *Halo, " + from_name + "! Selamat datang di FEBOX* 🩸\n";
    greeting += "_(Smart Pillbox Pengingat Tablet Tambah Darah / Fe)_\n\n";
    greeting += "FEBOX membantu Anda meminum Tablet Tambah Darah secara tepat waktu demi mencegah dan mengatasi anemia.\n\n";

    greeting += "💡 *Model Layanan FEBOX:*\n";
    greeting += "1. 📱 *Pengguna Telegram:* Siapa pun dapat menerima notifikasi pengingat tepat waktu secara gratis.\n";
    greeting += "2. 📦 *Pemilik Boks Fisik:* Boks fisik FEBOX dapat ditautkan ke 1 akun pengguna agar alarm buzzer bersuara saat jam minum tiba.\n\n";

    greeting += "📌 *Status Akun Anda:*\n";
    if (isSubbed) {
        greeting += "• Pengingat: *Aktif (" + formatDays(s->daysMask) + " " + formatTime(s->hour, s->minute) + ")*\n";
    } else {
        greeting += "• Pengingat: ❌ _Belum Berlangganan_\n";
    }
    if (isOwner) {
        greeting += "• Perangkat: ✅ *Pemilik Boks (" + deviceId + ")*\n\n";
    } else {
        greeting += "• Perangkat: 📱 _Layanan Telegram (Belum Menautkan Boks)_\n\n";
    }

    greeting += "Silakan pilih salah satu menu di bawah ini untuk memulai:";

    // 1. Tampilkan reply keyboard (tombol permanen di bawah layar obrolan)
    bot.sendMessageWithReplyKeyboard(chat_id, greeting, "Markdown", MAIN_KEYBOARD, true);

    // 2. Berikan tombol aksi cepat jika belum berlangganan atau belum menautkan boks
    String quickKeyboard = "[";
    if (!isSubbed) {
        quickKeyboard += "[{\"text\":\"🔔 Langganan Cepat (Tiap Jumat 20:00)\",\"callback_data\":\"set_fri_20\"}],";
    }
    if (!isOwner && deviceOwnerChatId.length() == 0) {
        quickKeyboard += "[{\"text\":\"📦 Tautkan Boks Fisik (" + deviceId + ")\",\"callback_data\":\"pair_current_device\"}],";
    }
    quickKeyboard += "[{\"text\":\"⏰ Atur Jadwal Sendiri\",\"callback_data\":\"menu_schedule\"}],"
                     "[{\"text\":\"📋 Cek Status Lengkap\",\"callback_data\":\"menu_status\"}]"
                     "]";
    bot.sendMessageWithInlineKeyboard(chat_id, "💡 *Pilihan Aksi Cepat:*", "Markdown", quickKeyboard);
}

void sendStatus(const String& chat_id, const String& from_name) {
    Subscriber* s = findSubscriber(chat_id);
    DateTime now = rtc.now();

    char clockBuf[64];
    snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d:%02d WIB", now.hour(), now.minute(), now.second());

    int dayIdx = now.dayOfTheWeek();
    int monthIdx = now.month() - 1;
    if (monthIdx < 0 || monthIdx > 11) monthIdx = 0;

    char dateBuf[96];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s %04d", 
             daysOfTheWeek[dayIdx], now.day(), monthsOfTheYear[monthIdx], now.year());

    bool isOwner = isDeviceOwner(chat_id);

    String rep = "📋 *STATUS PENGINGAT FEBOX*\n";
    rep += "━━━━━━━━━━━━━━━━━━━━\n";
    rep += "👤 Nama: *" + from_name + "*\n";
    if (s != nullptr && s->active) {
        rep += "🔔 Pengingat: *Aktif Berlangganan* ✅\n";
        rep += "📅 Jadwal Hari: *" + formatDays(s->daysMask) + "*\n";
        rep += "⏰ Jadwal Jam: *" + formatTime(s->hour, s->minute) + "*\n";
    } else {
        rep += "🔔 Pengingat: *Nonaktif / Belum Langganan* ❌\n";
        if (s != nullptr) {
            rep += "📅 Tersimpan: " + formatDays(s->daysMask) + " pukul " + formatTime(s->hour, s->minute) + "\n";
        }
        rep += "_Ketik /subscribe untuk mengaktifkan pengingat._\n";
    }
    rep += "━━━━━━━━━━━━━━━━━━━━\n";
    rep += "📦 *Perangkat Fisik:*\n";
    if (isOwner) {
        rep += "✅ *Tertaut ke Boks Fisik (`" + deviceId + "`)*\n";
        rep += "🔊 Alarm Buzzer: *Aktif berbunyi untuk Anda*\n";
    } else {
        rep += "📱 *Layanan Telegram Saja (Tanpa Boks Fisik)*\n";
        rep += "🔊 Alarm Buzzer: *Hanya notifikasi pesan Telegram*\n";
        if (deviceOwnerChatId.length() == 0) {
            rep += "💡 _Boks fisik tersedia! Ketik `/tautkan " + deviceId + "` untuk menghubungkan boks._\n";
        } else {
            rep += "ℹ️ _Boks fisik sedang digunakan oleh user lain._\n";
        }
    }
    rep += "━━━━━━━━━━━━━━━━━━━━\n";
    rep += "🕒 *Waktu Box:* " + String(clockBuf) + "\n";
    rep += "📆 *Tanggal:* " + String(dateBuf) + "\n";
    rep += "👥 *Total Pengguna Terdaftar:* " + String(subscriberList.size()) + "\n";

    bot.sendMessage(chat_id, rep, "Markdown");
}

void sendHelp(const String& chat_id) {
    String help = "📖 *PANDUAN & BANTUAN FEBOX* 🩸\n\n";
    help += "*Daftar Perintah Bot:*\n";
    help += "• `/start` - Buka menu utama & cek status awal\n";
    help += "• `/status` - Cek jadwal & status tautan boks\n";
    help += "• `/jadwal` - Menu pemilihan jadwal interaktif\n";
    help += "• `/jadwal <hari> <jam>` - Set hari & jam kustom _(contoh: `/jadwal jumat 20:00`)_\n";
    help += "• `/jam <HH:MM>` - Ganti jam saja _(contoh: `/jam 20:30`)_\n";
    help += "• `/hari <hari>` - Ganti hari saja _(contoh: `/hari senin,kamis`)_\n";
    help += "• `/tautkan <ID>` - Tautkan boks fisik _(contoh: `/tautkan " + deviceId + "`)_\n";
    help += "• `/lepaskan` - Lepaskan tautan boks fisik Anda\n";
    help += "• `/subscribe` - Aktifkan pengingat\n";
    help += "• `/unsubscribe` - Nonaktifkan pengingat\n";
    help += "• `/tes_alarm` - Uji coba buzzer (khusus pemilik boks)\n\n";
    help += "🔘 *Reset Fisik Boks (20 Detik):*\n";
    help += "Tekan tombol fisik pada boks selama 20 detik untuk menghapus data pemilik. Pemilik lama akan menerima notifikasi Telegram bahwa boks telah terlepas.\n\n";
    help += "💡 *Tips Minum Tablet Tambah Darah (Fe):*\n";
    help += "1. Minum malam hari setelah makan / menjelang tidur untuk meminimalkan rasa mual.\n";
    help += "2. Minum dengan segelas air putih atau jus jeruk (Vitamin C meningkatkan penyerapan zat besi).\n";
    help += "3. Jangan diminum bersama teh, kopi, atau susu karena dapat menghambat penyerapan.";

    bot.sendMessageWithReplyKeyboard(chat_id, help, "Markdown", MAIN_KEYBOARD, true);
}

void handleSubscribe(const String& chat_id, const String& from_name) {
    Subscriber* s = findSubscriber(chat_id);
    if (s != nullptr && s->active) {
        String msg = "Anda sudah berlangganan pengingat!\n";
        msg += "📅 Hari: *" + formatDays(s->daysMask) + "*\n";
        msg += "⏰ Jam: *" + formatTime(s->hour, s->minute) + "*\n\n";
        msg += "💡 _Klik tombol *⏰ Atur Jadwal* jika ingin mengubah waktu pengingat._";
        bot.sendMessage(chat_id, msg, "Markdown");
        return;
    }

    Subscriber& sub = getOrCreateSubscriber(chat_id, from_name);
    sub.active = true;
    saveSubscribers();

    String msg = "✅ *Berhasil Berlangganan Pengingat Fe!*\n\n";
    msg += "📅 Hari: *" + formatDays(sub.daysMask) + "*\n";
    msg += "⏰ Jam: *" + formatTime(sub.hour, sub.minute) + "*\n";
    msg += "🔔 Status: *Aktif*\n\n";
    msg += "💡 _Tips: Klik *⏰ Atur Jadwal* kapan saja untuk menyesuaikan hari dan jam._";
    bot.sendMessage(chat_id, msg, "Markdown");
}

void handleUnsubscribe(const String& chat_id, const String& from_name) {
    Subscriber* s = findSubscriber(chat_id);
    if (s == nullptr || !s->active) {
        bot.sendMessage(chat_id, "Pengingat Anda saat ini memang sedang tidak aktif.", "");
        return;
    }

    s->active = false;
    saveSubscribers();

    String msg = "🔕 *Pengingat Telah Dinonaktifkan*\n\n";
    msg += "Anda tidak akan menerima notifikasi sampai diaktifkan kembali.\n";
    msg += "Jadwal Anda sebelumnya tetap tersimpan dan dapat diaktifkan kembali via tombol *🔔 Berlangganan*.";
    bot.sendMessage(chat_id, msg, "Markdown");
}

void handleCustomJadwal(const String& chat_id, const String& from_name, String args) {
    args.trim();
    if (args.length() == 0) {
        sendScheduleMenu(chat_id, from_name);
        return;
    }

    int lastSpace = args.lastIndexOf(' ');
    int h = 20, m = 0;
    uint8_t d = 0;

    if (lastSpace != -1) {
        String timePart = args.substring(lastSpace + 1);
        String daysPart = args.substring(0, lastSpace);

        if (parseTime(timePart, h, m)) {
            d = parseDays(daysPart);
            if (d > 0) {
                Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
                s.daysMask = d;
                s.hour = h;
                s.minute = m;
                s.active = true;
                saveSubscribers();

                String res = "✅ *Jadwal Pengingat Berhasil Diatur!*\n\n";
                res += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
                res += "⏰ Waktu: *" + formatTime(s.hour, s.minute) + "*\n";
                res += "🔔 Status: *Aktif*\n\n";
                res += "Kotak obat FEBOX akan membunyikan alarm dan mengirim notifikasi sesuai jadwal tersebut.";
                bot.sendMessage(chat_id, res, "Markdown");
                return;
            }
        }
    }

    // Jika user hanya memasukkan jam (misal "/jadwal 20:00")
    if (parseTime(args, h, m)) {
        Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
        s.hour = h;
        s.minute = m;
        s.active = true;
        saveSubscribers();

        String res = "✅ *Jam Pengingat Berhasil Diperbarui!*\n\n";
        res += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        res += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        res += "🔔 Status: *Aktif*";
        bot.sendMessage(chat_id, res, "Markdown");
        return;
    }

    // Jika user hanya memasukkan hari (misal "/jadwal jumat" atau "/jadwal senin,kamis")
    d = parseDays(args);
    if (d > 0) {
        Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
        s.daysMask = d;
        s.active = true;
        saveSubscribers();

        String res = "✅ *Hari Pengingat Berhasil Diperbarui!*\n\n";
        res += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        res += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        res += "🔔 Status: *Aktif*";
        bot.sendMessage(chat_id, res, "Markdown");
        return;
    }

    String err = "⚠️ *Format jadwal belum tepat.*\n\n";
    err += "*Contoh penggunaan yang benar:*\n";
    err += "• `/jadwal jumat 20:00`\n";
    err += "• `/jadwal senin,rabu,kamis 20:30`\n";
    err += "• `/jadwal tiap hari 07:00`\n";
    err += "• `/jam 20:30` _(ubah jam saja)_\n";
    err += "• `/hari jumat` _(ubah hari saja)_\n\n";
    err += "Atau gunakan tombol preset di bawah:";
    bot.sendMessageWithInlineKeyboard(chat_id, err, "Markdown", getScheduleInlineKeyboard());
}

void handleCustomJam(const String& chat_id, const String& from_name, String args) {
    int h, m;
    if (parseTime(args, h, m)) {
        Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
        s.hour = h;
        s.minute = m;
        s.active = true;
        saveSubscribers();

        String reply = "✅ *Jam Pengingat Berhasil Diperbarui!*\n";
        reply += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        reply += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        reply += "🔔 Status: *Aktif*";
        bot.sendMessage(chat_id, reply, "Markdown");
    } else {
        bot.sendMessage(chat_id, "⚠️ Format jam salah. Gunakan format JJ:MM (contoh: `/jam 20:00` atau `/jam 07:30`).", "Markdown");
    }
}

void handleCustomHari(const String& chat_id, const String& from_name, String args) {
    uint8_t d = parseDays(args);
    if (d > 0) {
        Subscriber& s = getOrCreateSubscriber(chat_id, from_name);
        s.daysMask = d;
        s.active = true;
        saveSubscribers();

        String reply = "✅ *Hari Pengingat Berhasil Diperbarui!*\n";
        reply += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        reply += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        reply += "🔔 Status: *Aktif*";
        bot.sendMessage(chat_id, reply, "Markdown");
    } else {
        bot.sendMessage(chat_id, "⚠️ Hari tidak dikenali.\nContoh: `/hari jumat` atau `/hari senin, rabu, kamis` atau `/hari tiap hari`.", "Markdown");
    }
}

// ==================== HANDLER CALLBACK QUERY & PESAN ====================

void handleCallbackQuery(const String& chat_id, const String& from_name, const String& query_id, const String& data, int message_id) {
    bot.answerCallbackQuery(query_id, "Pilihan diterima!");

    Subscriber& s = getOrCreateSubscriber(chat_id, from_name);

    if (data == "pair_current_device") {
        handlePairDevice(chat_id, from_name, deviceId);
    }
    else if (data == "unpair_device") {
        handleUnpairDevice(chat_id, from_name);
    }
    else if (data == "menu_device") {
        sendDeviceLinkMenu(chat_id, from_name);
    }
    else if (data == "set_fri_20") {
        s.daysMask = 32;
        s.hour = 20;
        s.minute = 0;
        s.active = true;
        saveSubscribers();
        String msg = "✅ *Jadwal Diatur: Setiap Jumat 20:00 WIB*\nStatus pengingat kini *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (data == "set_daily_20") {
        s.daysMask = 127;
        s.hour = 20;
        s.minute = 0;
        s.active = true;
        saveSubscribers();
        String msg = "✅ *Jadwal Diatur: Setiap Hari 20:00 WIB*\nStatus pengingat kini *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (data == "set_mwth_20") {
        s.daysMask = ((1 << 1) | (1 << 3) | (1 << 4)); // Senin, Rabu, Kamis
        s.hour = 20;
        s.minute = 0;
        s.active = true;
        saveSubscribers();
        String msg = "✅ *Jadwal Diatur: Senin, Rabu, Kamis 20:00 WIB*\nStatus pengingat kini *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (data == "set_mth_20") {
        s.daysMask = ((1 << 1) | (1 << 4)); // Senin & Kamis
        s.hour = 20;
        s.minute = 0;
        s.active = true;
        saveSubscribers();
        String msg = "✅ *Jadwal Diatur: Senin & Kamis 20:00 WIB*\nStatus pengingat kini *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (data == "menu_schedule") {
        sendScheduleMenu(chat_id, from_name);
    }
    else if (data == "menu_hour") {
        String msg = "🕐 *PILIH JAM PENGINGAT*\n\n";
        msg += "Jam saat ini: *" + formatTime(s.hour, s.minute) + "*\n";
        msg += "Pilih rekomendasi jam di bawah atau ketik `/jam JJ:MM`:";
        bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", getHoursInlineKeyboard());
    }
    else if (data == "menu_day") {
        String msg = "🗓️ *PILIH HARI PENGINGAT*\n\n";
        msg += "Pola hari saat ini: *" + formatDays(s.daysMask) + "*\n";
        msg += "Pilih pola hari di bawah atau ketik `/hari nama_hari`:";
        bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", getDaysInlineKeyboard());
    }
    else if (data == "menu_status") {
        sendStatus(chat_id, from_name);
    }
    else if (data.startsWith("h_")) {
        // Format h_HH_MM
        int h = data.substring(2, 4).toInt();
        int m = data.substring(5, 7).toInt();
        s.hour = h;
        s.minute = m;
        s.active = true;
        saveSubscribers();

        String msg = "✅ *Jam Pengingat Diperbarui!*\n";
        msg += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        msg += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        msg += "🔔 Status: *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (data.startsWith("d_")) {
        if (data == "d_fri") s.daysMask = 32;
        else if (data == "d_daily") s.daysMask = 127;
        else if (data == "d_mwth") s.daysMask = ((1 << 1) | (1 << 3) | (1 << 4));
        else if (data == "d_mth") s.daysMask = ((1 << 1) | (1 << 4));
        else if (data == "d_weekdays") s.daysMask = ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
        else if (data == "d_weekend") s.daysMask = ((1 << 0) | (1 << 6));

        s.active = true;
        saveSubscribers();

        String msg = "✅ *Hari Pengingat Diperbarui!*\n";
        msg += "📅 Hari: *" + formatDays(s.daysMask) + "*\n";
        msg += "⏰ Jam: *" + formatTime(s.hour, s.minute) + "*\n";
        msg += "🔔 Status: *Aktif* ✅";
        bot.sendMessage(chat_id, msg, "Markdown");
    }
}

void handleTextMessage(const String& chat_id, const String& from_name, const String& text) {
    Serial.printf("[Bot] Pesan dari %s (%s): %s\n", from_name.c_str(), chat_id.c_str(), text.c_str());

    String textLower = text;
    textLower.toLowerCase();
    textLower.trim();

    if (textLower == "/start") {
        sendStartMessage(chat_id, from_name);
    }
    else if (textLower == "/status" || textLower == "📋 status & jadwal" || textLower == "status") {
        sendStatus(chat_id, from_name);
    }
    else if (textLower == "/jadwal" || textLower == "/atur_jadwal" || textLower == "⏰ atur jadwal") {
        sendScheduleMenu(chat_id, from_name);
    }
    else if (textLower == "/tautkan" || textLower == "📦 tautkan boks" || textLower == "/pair") {
        sendDeviceLinkMenu(chat_id, from_name);
    }
    else if (textLower.startsWith("/tautkan ") || textLower.startsWith("/pair ")) {
        String inputId = text.substring(text.indexOf(' ') + 1);
        handlePairDevice(chat_id, from_name, inputId);
    }
    else if (textLower == "/lepaskan" || textLower == "/unpair") {
        handleUnpairDevice(chat_id, from_name);
    }
    else if (textLower.startsWith("/jadwal ")) {
        handleCustomJadwal(chat_id, from_name, text.substring(8));
    }
    else if (textLower.startsWith("/jam ")) {
        handleCustomJam(chat_id, from_name, text.substring(5));
    }
    else if (textLower.startsWith("/hari ")) {
        handleCustomHari(chat_id, from_name, text.substring(6));
    }
    else if (textLower == "/subscribe" || textLower == "🔔 berlangganan" || textLower == "🔔 mulai berlangganan") {
        handleSubscribe(chat_id, from_name);
    }
    else if (textLower == "/unsubscribe" || textLower == "/stop" || textLower == "🔕 berhenti") {
        handleUnsubscribe(chat_id, from_name);
    }
    else if (textLower == "/tes_alarm" || textLower == "🧪 tes alarm") {
        if (isDeviceOwner(chat_id)) {
            bot.sendMessage(chat_id, "🧪 Memulai tes buzzer alarm pada boks FEBOX Anda...", "");
            playAlarmTone();
            bot.sendMessage(chat_id, "✅ Tes alarm selesai. Modul audio boks FEBOX normal.", "");
        } else {
            String msg = "ℹ️ *Layanan Notifikasi Telegram Saja*\n\n";
            msg += "Akun Anda saat ini terdaftar pada layanan notifikasi Telegram (tanpa menautkan boks fisik FEBOX).\n\n";
            msg += "• Alarm buzzer fisik boks *hanya berbunyi* untuk pemilik resmi yang telah menautkan ID boks.\n";
            msg += "• Anda tetap mendapatkan pesan notifikasi pengingat via Telegram tepat waktu sesuai jadwal!\n\n";
            if (deviceOwnerChatId.length() == 0) {
                msg += "💡 _Boks fisik saat ini tersedia! Anda dapat menautkannya dengan mengetik:_ `/tautkan " + deviceId + "`";
            } else {
                msg += "💡 _Boks fisik saat ini sedang ditautkan oleh user lain._";
            }
            bot.sendMessage(chat_id, msg, "Markdown");
        }
    }
    else if (textLower == "/help" || textLower == "/bantuan" || textLower == "❓ bantuan") {
        sendHelp(chat_id);
    }
    else {
        String help = "Halo *" + from_name + "*! Perintah belum dikenali.\n\n";
        help += "Silakan gunakan tombol menu di bawah atau ketik:\n";
        help += "• /start - Buka menu utama\n";
        help += "• /jadwal - Pengaturan hari & jam pengingat\n";
        help += "• /tautkan - Tautkan boks fisik FEBOX\n";
        help += "• /status - Cek status pengingat Anda\n";
        help += "• /bantuan - Panduan lengkap & tips Fe";
        bot.sendMessageWithReplyKeyboard(chat_id, help, "Markdown", MAIN_KEYBOARD, true);
    }
}

// ==================== TELEGRAM BOT DISPATCHER ====================

void handleNewMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String msgType = bot.messages[i].type;
        String chat_id = bot.messages[i].chat_id;
        String from_name = bot.messages[i].from_name;
        if (from_name.length() == 0) from_name = "Teman";

        if (msgType == "callback_query") {
            String query_id = bot.messages[i].query_id;
            String data = bot.messages[i].text;
            int message_id = bot.messages[i].message_id;
            handleCallbackQuery(chat_id, from_name, query_id, data, message_id);
        } else {
            String text = bot.messages[i].text;
            handleTextMessage(chat_id, from_name, text);
        }
    }
}

// ==================== CALLBACK & RESET ====================

void configModeCallback(WiFiManager *myWiFiManager) {
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.println("[Info] Mode AP Aktif. TX Power diset ke 8.5 dBm.");
    beep(2, 80, 80, 2000);
}

void checkResetButton() {
    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        unsigned long startPress = millis();
        Serial.println("\n[Button] Tombol fisik boks mulai ditekan...");

        int lastBeepSec = 0;
        bool resetCompleted = false;

        while (digitalRead(RESET_PIN) == LOW) {
            unsigned long duration = millis() - startPress;
            int seconds = duration / 1000;

            // Indikator audio beep pendek setiap 5 detik (5s, 10s, 15s)
            if (seconds >= 5 && seconds < 20 && seconds != lastBeepSec && seconds % 5 == 0) {
                lastBeepSec = seconds;
                Serial.printf("[Button] Tombol ditahan: %d detik...\n", seconds);
                beep(1, 40, 0, 2000);
            }

            // Jika tombol ditahan selama 20 detik!
            if (seconds >= 20 && !resetCompleted) {
                resetCompleted = true;
                Serial.println("\n[Action] 20 detik tercapai! Melakukan Reset User Perangkat...");

                // Nada konfirmasi selesai (3x beep ceria)
                beep(3, 100, 80, 3000);

                // Kirim notifikasi Telegram ke user pemilik sebelumnya jika ada
                if (deviceOwnerChatId.length() > 0) {
                    if (WiFi.status() == WL_CONNECTED) {
                        String notify = "⚠️ *PEMBERITAHUAN RESET PERANGKAT FEBOX* ⚠️\n\n";
                        notify += "Halo *" + deviceOwnerName + "*!\n";
                        notify += "Tombol reset fisik pada boks FEBOX (*" + deviceId + "*) telah ditekan selama *20 detik*.\n\n";
                        notify += "🔓 Boks Anda saat ini *SUDAH TIDAK LAGI TERHUBUNG* dengan akun Anda.\n";
                        notify += "Alarm buzzer fisik pada boks tidak akan berbunyi sampai perangkat didaftarkan kembali.\n\n";
                        notify += "📱 *Layanan Telegram:* Anda tetap menerima notifikasi pengingat via bot ini.\n\n";
                        notify += "📦 *Daftarkan Kembali:* Jika ini Anda dan ingin menautkan kembali, ketik:\n";
                        notify += "`/tautkan " + deviceId + "`";

                        bot.sendMessage(deviceOwnerChatId, notify, "Markdown");
                        Serial.printf("[Action] Notifikasi reset berhasil dikirim ke Chat ID: %s\n", deviceOwnerChatId.c_str());
                        delay(600); // Berikan jeda agar transmisi paket Telegram tuntas
                    }
                }

                // Hapus data kepemilikan di flash memory
                deviceOwnerChatId = "";
                deviceOwnerName = "";
                saveDeviceOwner();

                Serial.println("[Sukses] Kepemilikan boks berhasil di-reset. Boks kini berstatus bebas.");
                break;
            }

            delay(50);
        }

        // Tunggu hingga tombol benar-benar dilepas
        while (digitalRead(RESET_PIN) == LOW) {
            delay(50);
        }

        if (!resetCompleted) {
            unsigned long duration = millis() - startPress;
            Serial.printf("[Button] Tombol dilepas setelah %lu ms (reset 20 detik dibatalkan).\n", duration);
        }
    }
}

void syncNTPtoRTC() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    Serial.print("[NTP] Menghubungi NTP server...");
    
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.print(".");
        delay(500);
        retry++;
    }

    if (retry < 10) {
        Serial.println("\n[NTP] Waktu NTP berhasil didapatkan.");
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, 
                            timeinfo.tm_mon + 1, 
                            timeinfo.tm_mday, 
                            timeinfo.tm_hour, 
                            timeinfo.tm_min, 
                            timeinfo.tm_sec));
        Serial.println("[RTC] Waktu RTC berhasil disinkronkan!");
    } else {
        Serial.println("\n[NTP] Gagal mengambil waktu NTP.");
    }
}

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);
    delay(2000);

    initBuzzer();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    esp_bt_controller_disable();

    Serial.println("\n--- Memulai Fe-Reminder ESP32-C3 ---");
    beep(1, 100, 0, 2700);

    // Inisialisasi ID Perangkat unik
    initDeviceId();

    // Baca daftar subscriber & pemilik boks dari flash memory
    loadSubscribers();

    // Inisialisasi I2C & RTC
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    if (!rtc.begin(&Wire)) {
        Serial.println("[Error] Modul RTC tidak terdeteksi pada pin 8 & 9!");
    } else {
        Serial.println("[Sukses] Modul RTC terhubung.");
        if (rtc.lostPower()) {
            Serial.println("[Peringatan] Baterai RTC habis/baru dipasang! Menyetel ke waktu kompilasi...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }

    checkResetButton();

    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConnectTimeout(15);
    wm.setConfigPortalTimeout(180);

    bool res = wm.autoConnect(AP_SSID, AP_PASS);

    if (!res) {
        Serial.println("[Error] WiFi timeout / gagal connect.");
        beep(1, 1000, 0, 1200);
        delay(2000);
        ESP.restart();
    } else {
        Serial.println("\n[Sukses] Berhasil terhubung ke WiFi!");
        Serial.print("IP Address : ");
        Serial.println(WiFi.localIP());

        beep(1, 80, 40, 2200);
        beep(1, 150, 0, 3000);
        digitalWrite(LED_PIN, HIGH);

        syncNTPtoRTC();

        // Lewati validasi sertifikat SSL agar hemat RAM dan koneksi cepat
        secured_client.setInsecure();
    }
}

// ==================== LOOP ====================

void loop() {
    checkResetButton();

    static unsigned long lastBotCheck = 0;
    static unsigned long lastClockPrint = 0;

    // 1. Cek Pesan Masuk Telegram setiap 1.5 detik
    if (WiFi.status() == WL_CONNECTED && millis() - lastBotCheck >= 1500) {
        lastBotCheck = millis();
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        while (numNewMessages) {
            handleNewMessages(numNewMessages);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
    }

    // 2. Cek Jadwal Alarm dari RTC setiap 1 detik
    if (millis() - lastClockPrint >= 1000) {
        lastClockPrint = millis();
        DateTime now = rtc.now();

        Serial.printf("[%s] %04d-%02d-%02d %02d:%02d:%02d\n",
                      daysOfTheWeek[now.dayOfTheWeek()],
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());

        int currentDayOfWeek = now.dayOfTheWeek();
        int currentHour = now.hour();
        int currentMinute = now.minute();
        int currentDay = now.day();

        bool buzzerTriggeredThisCycle = false;

        for (auto &sub : subscriberList) {
            if (!sub.active) continue;

            // Cek apakah hari ini dijadwalkan
            if ((sub.daysMask & (1 << currentDayOfWeek)) != 0) {
                // Cek apakah jam dan menit sesuai
                if (sub.hour == currentHour && sub.minute == currentMinute) {
                    // Cegah pengingat terpicu berulang kali pada menit yang sama
                    if (sub.lastTriggeredDay != currentDay || sub.lastTriggeredMin != currentMinute) {
                        sub.lastTriggeredDay = currentDay;
                        sub.lastTriggeredMin = currentMinute;

                        bool isOwner = isDeviceOwner(sub.chatId);

                        Serial.printf("\n>>> [PENGINGAT] Mengirim pengingat Fe ke %s (%s) [%s] <<<\n",
                                      sub.name.c_str(), sub.chatId.c_str(),
                                      isOwner ? "PEMILIK BOKS" : "PENGGUNA TELEGRAM");

                        if (WiFi.status() == WL_CONNECTED) {
                            String msg = "💊 *PENGINGAT MINUM TABLET TAMBAH DARAH (Fe)* 💊\n\n";
                            msg += "Halo *" + sub.name + "*! Waktunya minum Tablet Tambah Darah sekarang (" + 
                                   formatTime(sub.hour, sub.minute) + ").\n\n";
                            if (isOwner) {
                                msg += "🔊 _Alarm boks fisik FEBOX Anda sedang berbunyi._\n\n";
                            }
                            msg += "💡 *Tips Penting:*\n";
                            msg += "• Minum dengan air putih atau jus jeruk (Vitamin C meningkatkan penyerapan zat besi).\n";
                            msg += "• *Hindari* teh, kopi, atau susu saat meminumnya karena menghambat penyerapan.\n\n";
                            msg += "Tetap sehat, bugar, dan bebas anemia! 💪✨";

                            bot.sendMessage(sub.chatId, msg, "Markdown");
                        }

                        // Hanya aktifkan buzzer fisik jika pengguna ini adalah pemilik sah boks!
                        if (isOwner) {
                            buzzerTriggeredThisCycle = true;
                        }
                    }
                }
            }
        }

        if (buzzerTriggeredThisCycle) {
            playAlarmTone();
        }
    }
}