# 📄 Dokumentasi Proyek: IO Modul PLN 24-Channel

## 🎯 Latar Belakang Proyek

Proyek ini adalah **modul IO (Input/Output) 24-channel** yang dirancang untuk memantau status sinyal dari panel listrik PLN. Modul ini menggunakan **ESP32** sebagai otak utama, yang terhubung dengan **3 buah chip PCF8574** (expander I2C) untuk membaca 24 channel input secara paralel.

Setiap channel input dihubungkan melalui **optocoupler** sebagai isolator, yang berfungsi untuk:
- Melindungi ESP32 dari lonjakan tegangan AC/DC.
- Memisahkan ground antara sisi panel PLN dan sisi mikrokontroler.

Hasil pembacaan kemudian dikirim ke server melalui **MQTT** dalam format JSON, dengan interval yang dapat dikonfigurasi secara dinamis dari jarak jauh.

---

## ⚠️ Masalah Utama: Data Intermitten (Flicker)

### Konteks Masalah

Selama pengujian awal, muncul masalah serius pada pembacaan status pin: **data yang masuk sangat tidak stabil (intermitten/flicker)**. Suatu pin yang secara fisik seharusnya konstan `ON`, kadang terbaca `ON`, kadang `OFF`, kadang `ON`, kadang `OFF` dalam rentang milidetik.

### Penyebab

Masalah ini muncul karena **tidak adanya rangkaian filter fisik (kapasitor) pada sisi output optocoupler**. Idealnya, output optocoupler dihubungkan paralel dengan kapasitor (biasanya 100nF - 10µF) untuk:

1. **Menghilangkan noise elektromagnetik** yang diinduksi dari kabel listrik AC di sekitar panel.
2. **Memperlambat respons** transisi ON→OFF atau OFF→ON agar tidak terlalu tajam (mencegah bouncing).
3. **Menstabilkan tegangan** saat terjadi ripple dari sumber AC.

Karena kapasitor ini tidak terpasang, output optocoupler masih membawa noise asli dari sinyal AC PLN. Sinyal AC PLN 50Hz memiliki karakteristik:
- **Periode:** 20 ms
- **Fase ON:** ~10 ms
- **Fase OFF:** ~10 ms

Ketika pin PCF8574 membaca output optocoupler setiap **5 ms**, ia akan menangkap siklus AC yang sedang berdenyut, bukan status DC yang stabil. Inilah yang menyebabkan data mentah terlihat seperti `1,0,1,1,0,1,0,0,1,...` alih-alih `1,1,1,1,1,...` untuk pin yang sedang ON.

---

## 💡 Solusi: Kalman Filter sebagai Pengganti Kapasitor

### Mengapa Kalman Filter?

Karena kita **tidak dapat mengubah hardware** (menambah kapasitor) pada tahap ini, solusinya adalah melakukan **filtering di sisi software**. Dari berbagai algoritma filter yang ada (Moving Average, Median, Low-Pass RC Digital, dll.), **Kalman Filter** dipilih karena beberapa alasan:

| Kriteria | Moving Average | Median Filter | **Kalman Filter** |
|:---|:---|:---|:---|
| Responsif terhadap perubahan cepat | ❌ Lambat | ❌ Lambat | ✅ Cepat |
| Ketahanan terhadap noise | ✅ Baik | ✅ Sangat Baik | ✅ Sangat Baik |
| Kebutuhan memori | Tinggi (buffer) | Tinggi (buffer) | ✅ Rendah |
| Kompleksitas komputasi | Rendah | Sedang | Sedang |
| Adaptif terhadap perubahan | ❌ Statis | ❌ Statis | ✅ Adaptif |

Kalman Filter unggul karena bersifat **rekursif** (hanya butuh satu nilai sebelumnya, bukan buffer panjang) dan **adaptif** — ia dapat secara otomatis menyesuaikan bobot antara prediksi dan pengukuran baru, sehingga cocok untuk sinyal yang berubah dinamis seperti pulsa AC PLN.

### Cara Kerja Kalman Filter di Proyek Ini

Kalman Filter di sini **tidak** digunakan untuk memprediksi posisi atau kecepatan, melainkan untuk **mengestimasi probabilitas (duty cycle)** apakah suatu pin sedang `ON` atau `OFF`.

Setiap sampel biner mentah (`0` atau `1`) dimasukkan ke filter. Filter kemudian mengeluarkan nilai desimal antara `0.0` hingga `1.0`, yang merepresentasikan seberapa besar keyakinan kita bahwa pin tersebut sedang `ON` dalam beberapa sampel terakhir.

**Contoh:**
- Pin sedang OFF konstan → Estimasi mendekati `0.00`
- Pin sedang ON konstan → Estimasi mendekati `1.00`
- Pin sedang berdenyut karena noise → Estimasi di sekitar `0.3 - 0.7`

Nilai estimasi ini kemudian dibandingkan dengan **ambang batas histeresis** untuk menentukan status final:

- **`TH_ON = 0.15`** → Jika estimasi > 0.15, status dianggap ON.
- **`TH_OFF = 0.05`** → Jika estimasi < 0.05, status dianggap OFF.
- **Di antara 0.05 - 0.15** → Status dipertahankan (tidak berubah).

Adanya zona histeresis antara kedua ambang ini berfungsi untuk mencegah **flicker** ketika nilai estimasi berayun di sekitar satu titik tertentu.

---

## 🏗️ Arsitektur Sistem

### Diagram Blok Sederhana

```
[Panel PLN] 
    │
    ▼
[Optocoupler] ─── (Tanpa Kapasitor Filter = Sumber Noise)
    │
    ▼
[PCF8574 x3] ─── I2C @ 400kHz
    │
    ▼
[ESP32]
    ├── taskSampling   (Core 1) → Baca PCF + Kalman Filter
    ├── taskNetwork    (Core 0) → WiFi + MQTT Publish
    ├── taskDebug      (Core 1) → Serial Monitor
    └── taskHeartbeat  (Core 1) → Watchdog Eksternal
    │
    ▼
[Broker MQTT] ─── JSON Payload
```

### Pembagian Task (FreeRTOS)

| Task | Core | Prioritas | Fungsi |
|:---|:---|:---|:---|
| `taskSampling` | 1 | 3 (Tertinggi) | Baca 3 chip PCF tiap 5ms, filter Kalman |
| `taskNetwork` | 0 | 2 | WiFi, MQTT connect/publish, callback |
| `taskDebug` | 1 | 1 | Print estimasi Kalman & status ke Serial |
| `taskHeartbeat` | 1 | 1 | Toggle WDT eksternal sebagai watchdog |

**Alasan Pemisahan Core:**
- **Core 0** khusus untuk WiFi/MQTT karena stack WiFi ESP32 berjalan di core ini.
- **Core 1** untuk sampling real-time agar tidak terganggu oleh latensi jaringan.

---

## 📡 Format Data yang Dipublish

Payload JSON yang dikirim ke topik `ESP32/data`:

```json
{
  "R1": "1", "S1": "0", "T1": "1",
  "R2": "1", "S2": "1", "T2": "0",
  "R3": "0", "S3": "1", "T3": "1"
}
```

Setiap field merepresentasikan status satu channel fasa (R/S/T) pada masing-masing jalur (1/2/3).

---

## 🔄 Kontrol Dinamis via MQTT

### Topik Subscribe:
| Topik | Fungsi |
|:---|:---|
| `event/interval` | Mengubah interval publish (ms), min. 100ms |
| `esp32/output` | (Reserved untuk kontrol output di masa depan) |

### Contoh Perintah:
Kirim `2000` ke topik `event/interval` → ESP32 akan mengubah interval publish menjadi 2 detik. Nilai ini akan disimpan ke **NVS (Non-Volatile Storage)** menggunakan library `Preferences`, sehingga tetap tersimpan meskipun board di-reset.

---

## 🛡️ Mekanisme Keamanan Sistem

### 1. Watchdog Eksternal
Pin `PIN_WDT` hanya akan di-toggle jika **taskSampling** dan **taskNetwork** sama-sama menunjukkan tanda kehidupan (via variabel `sampleBeat` dan `netBeat`). Jika salah satu task hang, pin WDT berhenti toggle, dan rangkaian WDT eksternal akan mereset ESP32 secara otomatis.

### 2. Auto-Reconnect
- **WiFi:** Dicoba ulang setiap 10 detik jika terputus.
- **MQTT:** Dicoba ulang setiap 5 detik jika terputus.
- **Client ID Unik:** Menggunakan `esp_random()` untuk mencegah konflik ID saat reconnect.

### 3. Error Counter I2C
Variabel `gI2cErr` menghitung berapa kali pembacaan PCF gagal. Jika nilainya terus bertambah, ini menandakan masalah hardware (kabel longgar, alamat salah, atau noise berat).

---

## 🎓 Pelajaran Penting dari Proyek Ini

1. **Hardware dan Software Harus Saling Melengkapi.** Ketika filter hardware (kapasitor) tidak tersedia, filter software (Kalman) dapat menjadi alternatif yang efektif.

2. **Jangan Remehkan Noise di Lingkungan Industri.** Panel listrik adalah tempat yang sangat "kotor" secara elektromagnetik. Pembacaan digital sederhana tidak cukup — dibutuhkan strategi filtering yang cerdas.

3. **FreeRTOS Memisahkan Concern.** Dengan memisahkan task sampling, network, dan debug, sistem menjadi lebih responsif dan mudah di-debug.

4. **Arsitektur yang Aman Itu Sengaja Didesain.** Tidak adanya Mutex di kode ini bukan kelalaian, melainkan hasil dari desain yang cermat: data biner disimpan di `volatile uint8_t` (atomic), dan semua operasi MQTT diisolasi di satu task.

---

## 🚀 Pengembangan Selanjutnya

Beberapa ide untuk iterasi berikutnya:

- [ ] Menambahkan filter **Median-of-3** sebelum Kalman Filter untuk membuang spike noise ekstrem.
- [ ] Menambahkan **auto-tuning `q`** berdasarkan tingkat noise yang terdeteksi.
- [ ] Menambahkan **OTA Update** (Over-The-Air) untuk mempermudah update firmware di lapangan.
- [ ] Menambahkan **kontrol relay** sebagai output (bukan hanya input) via topik `esp32/output`.
- [ ] Menambahkan **timestamp NTP** pada payload JSON.
- [ ] Membuat **dashboard web** sederhana untuk monitoring real-time.

---

*Dokumen ini dibuat sebagai catatan teknis proyek Magang di PT Kamiko — Modul IO PLN 24-Channel.*
