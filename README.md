# CustomVehicles (ASI) — Experimental

> **EN:** An experimental GTA San Andreas (PC, 1.0 US) ASI plugin that swaps the model of the vehicle you are driving at runtime. Proof of concept for a custom-vehicle system for SA-MP 0.3.DL servers.
>
> **ID:** Plugin ASI eksperimental untuk GTA San Andreas (PC, 1.0 US) yang menukar model kendaraan yang sedang kamu kendarai saat game berjalan. Ini *proof of concept* untuk sistem kendaraan custom di server SA-MP 0.3.DL.

⚠️ **Status: alpha / test build.** Not ready for public servers. / **Belum siap dipakai di server publik.**

---

## Features

- Hotkey **F10** — log the model ID of the vehicle you are in (read-only, safe). / Catat ID model kendaraan (hanya baca, aman).
- Hotkey **F11** — swap your current car to a *donor* model; press again to restore. / Tukar mobil ke model *donor*; tekan lagi untuk mengembalikan.
- Hotkey **F12** — isolation test (swap to the same model). / Tes isolasi (tukar ke model yang sama).
- Donor model ID configurable through `CustomVehicles.ini`. / ID donor bisa diatur lewat `CustomVehicles.ini`.
- Detailed log file `CustomVehicles.log` for debugging. / Log rinci untuk debugging.

## Requirements 

- GTA San Andreas **1.0 US** (PC)
- An ASI Loader (`dinput8.dll`)
- Tested with SA-MP **0.3.DL** (PC)
- **Cars only** (automobile class). Do not use on bikes, boats, planes or helicopters.

## Install

1. Copy `CustomVehicles.asi` and `CustomVehicles.ini` into your GTA folder (next to `gta_sa.exe`).
2. Edit `Donor=` in `CustomVehicles.ini` to a vehicle model ID your server does **not** use (400–611).
3. Start the game, join a server, sit in a normal car, press **F10** / **F11**.
4. Check `CustomVehicles.log` if something goes wrong.

## How it works / Cara kerja

The plugin hooks into the game loop (through Plugin SDK), reads your hotkeys, and changes the model index of the vehicle you are driving, then re-initialises its suspension lines. The *donor* model slot can hold a model supplied by another tool (for example a mod loader); this plugin only swaps a vehicle to that slot.

Plugin membaca tombol, lalu mengganti model kendaraan yang kamu kendarai dan menyiapkan ulang suspensinya. Slot *donor* bisa diisi model dari alat lain (misalnya mod loader); plugin ini hanya menukar kendaraan ke slot itu.

## Known limitations / Keterbatasan

- The swapped car keeps the **handling of the original (base) car**. / Mobil hasil tukar tetap memakai **handling mobil asal**.
- Hitbox and wheel layout follow the donor model. / Hitbox dan posisi roda mengikuti model donor.
- Client-side only: other players do not see the change unless they run it too. / Hanya sisi client.
- Can crash the game on unsupported vehicle types. / Bisa membuat game crash pada jenis kendaraan yang tidak didukung.
- No downloader or server link yet. / Belum ada downloader maupun hubungan ke server.

## Credits

- Developed by **Hachiroku**
- Built with [Plugin SDK](https://github.com/DK22Pac/plugin-sdk) by DK22Pac and contributors. See their repository for its license terms.

## License & Disclaimer

© 2026 **Hachiroku**. *(Choose your license and add a `LICENSE` file. Before you pick one, check the license of Plugin SDK to make sure they are compatible.)*

This is an independent fan-made project. It is **not affiliated with, endorsed by, or sponsored by** Rockstar Games, Take-Two Interactive, or the SA-MP / open.mp teams. Use at your own risk and always back up your game files.

Proyek ini dibuat penggemar secara independen dan **tidak berafiliasi** dengan Rockstar Games, Take-Two Interactive, maupun tim SA-MP / open.mp. Gunakan dengan risiko sendiri dan selalu backup file game.
