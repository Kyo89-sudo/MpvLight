# Pacchetto sorgenti MpvLight 0.5.1

Contiene il codice completo di MpvLight e del suo setup, l'header pubblico libmpv, il decoder 7z dell'LZMA SDK (`third_party/lzma`, pubblico dominio), le risorse con l'icona e il suo generatore, e i test. Non contiene eseguibili né la DLL del motore.

- `src/`: MpvLight (`main.cpp`, motore `engine.hpp`/`player.hpp`, componenti `components.hpp`/`sevenzip.hpp`).
- `packaging/`: setup (`setup.cpp`, `setup.rc`, `setup.manifest`).
- `third_party/lzma/`: decoder 7z.
- `assets/`: icona (`mpvlight.ico`/`.png`) e `make_icon.py` per rigenerarla (Python + Pillow).
- `tests/`: test di integrazione del motore.

Per compilare servono Visual Studio con gli strumenti C++ desktop e il Windows SDK. `build.cmd` produce `portable\MpvLight.exe` e `dist\MpvLight-Setup-0.5.1-x64.exe`.

Il motore non va copiato a mano: il setup lo scarica da GitHub, e la versione portatile lo scarica con "Scarica il motore" (oppure si mette una `libmpv-2.dll` x64 accanto a `MpvLight.exe`).

Per i test: leggere TESTING.md, quindi eseguire `test.cmd` con il percorso della fixture multitraccia.
