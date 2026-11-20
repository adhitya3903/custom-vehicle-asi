# Custom Vehicle ASI (`custom-vehicle-asi`)

[![Build Status](https://img.shields.io/badge/build-GitHub%20Actions-blue.svg)](https://github.com/adhitya3903/custom-vehicle-asi/actions)
[![Platform](https://img.shields.io/badge/platform-PC%20(Windows)-lightgrey.svg)]()
[![Game](https://img.shields.io/badge/game-GTA%20SA%20%7C%20SA--MP%20%7C%20open.mp-orange.svg)]()

A lightweight client-side `.asi` plugin designed to extend vehicle customization capabilities in **SA-MP** and **open.mp**. Built to overcome the native limitation of `AddSimpleModel` and `AddCharModel` (which natively restrict custom vehicle models) by dynamically swapping and loading custom `.dff` and `.txd` vehicle models directly on the client side.

> **Catatan (ID):** Plugin `.asi` client-side ini dikembangkan khusus untuk memungkinkan penggantian model/skin kendaraan kustom (`.dff` & `.txd`) secara dinamis pada SA-MP / open.mp PC tanpa mengubah file bawaan `gta3.img` secara permanen.

---

## ✨ Features & Development Roadmap
- **Dynamic Vehicle Model Swapping:** Replaces specific base vehicle models (e.g., Infernus) in memory during runtime.
- **Zero-IMG Modification:** Works seamlessly alongside ASI Loader without corrupting original game archives.
- **Automated Cloud Build:** Compiled cleanly using CMake and GitHub Actions.
- **Server-Side Integration Ready:** Designed to communicate with Pawn scripts (SA-MP / open.mp) for future server-wide model synchronization.

*(Current Status: Stage 1 Experimental Build — PC Client Only)*

---

## 📋 Prerequisites (Persyaratan Sistem)
Before installing this plugin, ensure your GTA San Andreas directory meets the following requirements:
1. **GTA San Andreas v1.0 US**.
2. **SA-MP 0.3.DL** or **open.mp** client (`samp.dll`).
3. **Ultimate ASI Loader** (`vorbisFile.dll` / `dinput8.dll`) installed in your root GTA SA folder.

---

## 🚀 Installation & Usage (Cara Pemakaian)
1. Download the latest compiled `.asi` file from the [**Releases**](https://github.com/adhitya3903/custom-vehicle-asi/releases) page (or build it yourself via GitHub Actions).
2. Place the `.asi` file directly into your **GTA San Andreas root folder** (e.g., `D:\Game\GTA SAMP\`) or inside your `scripts/` folder.
3. Ensure your custom vehicle assets (`.dff` and `.txd`) are placed in the designated mod/cache directory as configured in the source.
4. Launch SA-MP / open.mp and connect to the server. The plugin will automatically initialize and hook into the game memory.

---

## 🛠️ Building from Source
This repository uses **CMake** and **GitHub Actions** for automated compilation (no local Visual Studio installation required):
1. Fork or clone this repository.
2. Push any changes to `src/main.cpp` or `CMakeLists.txt`.
3. Navigate to the **Actions** tab on GitHub to download the compiled `.asi` artifact.

---

## ©️ Credits & Terms of Use (Lisensi & Larangan)
- **Author & Lead Developer:** **Hachiroku** (`adhitya3903`)
- **Special Thanks:** SA-MP & open.mp Modding Community.

### ⚠️ STRICTLY NO RE-SHARING / DILARANG MENYEBARLUASKAN ULANG
- **EN:** This project is published for educational, testing, and personal server integration purposes only. **You are strictly prohibited from re-uploading, re-sharing, redistributing, or claiming this `.asi` plugin and its source code as your own** on any other websites, Discord servers, YouTube links, or repositories without explicit written permission from the author (**Hachiroku**).
- **ID:** **DILARANG KERAS** mengunggah ulang (*re-upload*), membagikan ulang (*re-share*), atau memperjualbelikan file `.asi` maupun *source code* dari repositori ini di platform mana pun tanpa izin resmi dari pembuat (**Hachiroku**).

---
