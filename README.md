# MpvLight 0.5.1 — Windows x64

Lettore multimediale leggero in C++/Win32 basato su libmpv, con interfaccia scura in stile mpv. Non usa framework UI aggiuntivi.

## Interfaccia

Il video riempie tutta la finestra. Quando muovi il mouse compare sopra il video una barra semitrasparente, che sparisce dopo 2 secondi senza muovere il mouse, anche in pausa. Resta solo mentre punti un pulsante, mentre trascini, a fine file e con i file solo audio. Il tempo si cambia da Menu → Finestra (2 s, 3,5 s, 6 s o sempre visibile). La barra contiene:

- timeline con anteprima del tempo al passaggio del mouse;
- play/pausa e file precedente/successivo della stessa cartella;
- volume (icona e cursore);
- tempo trascorso e durata: un clic passa al tempo rimanente;
- velocità, traccia audio, sottotitoli, menu e schermo intero.

I messaggi (volume, salti, tracce, screenshot, errori) appaiono in alto a sinistra e svaniscono da soli.

Senza file aperti c'è la schermata iniziale: pulsante **Apri file…**, scorciatoie principali e file recenti. Si può anche trascinare un file sulla finestra; trascinando un file di sottotitoli su un video in riproduzione, i sottotitoli vengono caricati.

Quando carichi un video la finestra si adatta alle sue proporzioni, fino all'80% dello schermo, anche per i video piccoli. Si può disattivare da Menu → Finestra.

## Comandi

| Tasto | Azione |
|---|---|
| Spazio, K | Riproduci / pausa |
| ← → | −5 / +5 s (Shift: 1 s, Ctrl: 30 s) |
| ↑ ↓, rotellina | Volume |
| M, clic centrale | Muto |
| F, F11, doppio clic, Alt+Invio | Schermo intero (Esc per uscire) |
| PagSu / PagGiù | File precedente / successivo |
| + − Backspace | Velocità su / giù / normale |
| . , | Fotogramma successivo / precedente |
| J / Shift+J | Cambia sottotitoli / traccia audio |
| S, Ctrl+S / Shift+S | Screenshot con / senza sottotitoli |
| Ctrl+O / Ctrl+G / Ctrl+T | Apri / vai al tempo / sempre in primo piano |
| Tasto destro, F1 | Menu completo, aiuto |

Funzionano anche i tasti multimediali della tastiera.

## Screenshot

La cattura è alla risoluzione originale del video, in PNG o JPG. Prima di catturare viene verificato che la cartella sia scrivibile, e la conferma arriva solo quando il file esiste davvero su disco.

Nella versione portatile la cartella predefinita è `Screenshots` accanto all'eseguibile; nell'installazione è `Immagini\MpvLight`. Si cambia da Menu → Screenshot.

## Installazione

`MpvLight-Setup-0.5.1-x64.exe` installa MpvLight per l'utente corrente, senza permessi da amministratore. La cartella predefinita è `%LOCALAPPDATA%\Programs\MpvLight`, la stessa della 0.3, che il setup aggiorna rimuovendo i resti di Inno Setup. Il setup:

- scarica l'ultima build di **libmpv** x64 da GitHub ([shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake)) e ne verifica l'impronta SHA-256 pubblicata da GitHub. Sui processori recenti (AVX2) sceglie la build ottimizzata `-v3`;
- estrae `libmpv-2.dll` dall'archivio `.7z` da solo, senza 7-Zip;
- crea il collegamento nel menu Start (e, se richiesto, sul desktop) e aggiunge MpvLight ad "Apri con" e alle App predefinite di Windows;
- registra la voce per la disinstallazione in Impostazioni → App.

Senza Internet si può indicare un archivio `mpv-dev-x86_64-….7z` o una `libmpv-2.dll` già scaricati. Opzioni da riga di comando: `/S` (silenzioso), `/dir "C:\percorso"`, `/uninstall`.

Il motore si aggiorna anche da MpvLight: Menu → **Aggiorna motore libmpv…**. Mentre MpvLight è aperto la DLL in uso viene rinominata e sostituita; serve un riavvio, che la finestra propone. Se il motore manca, la schermata iniziale mostra direttamente **Scarica il motore**.

## Versione portatile

Con il file `portable.flag` accanto all'eseguibile, le impostazioni vengono salvate in `settings.ini` nella stessa cartella; altrimenti in `%LOCALAPPDATA%\MpvLight`. La DLL viene cercata accanto all'eseguibile, poi in `[Engine] Library=` di settings.ini e nell'installazione in `%LOCALAPPDATA%\Programs\MpvLight`.

Vengono ricordati volume, posizione e dimensione della finestra, file recenti e opzioni.

## Motore

Tutte le chiamate a libmpv girano su un thread dedicato, con uno snapshot dello stato protetto da mutex e una coda che accorpa seek e cambi di volume e velocità.

La decodifica hardware è automatica (`hwdec=auto-safe`) su GPU Intel, AMD e NVIDIA quando GPU, driver e codec la supportano, con ripiego software. La decodifica in uso è indicata in Aiuto → Informazioni.

## Build

Eseguire `build.cmd` con Visual Studio e gli strumenti C++ installati: trova Visual Studio da solo con `vswhere` e produce

- `portable\MpvLight.exe`, la versione portatile pronta da provare;
- `dist\MpvLight-Setup-0.5.1-x64.exe`, il setup, che contiene già MpvLight.exe, README, CHANGELOG e la licenza;
- `dist\MpvLight-0.5.1-portable-x64.zip`, la versione portatile da distribuire (senza libmpv, che MpvLight scarica da solo);
- `dist\SHA256SUMS.txt`, le impronte SHA-256 dei due file da pubblicare.

La versione si cambia in un solo punto, `set "VERSION=…"` in cima a `build.cmd`, insieme a `kVersion` nei sorgenti e alle risorse `.rc`/manifest. Se nella cartella del progetto o nell'installazione esiste già una `libmpv-2.dll`, viene copiata in `portable\`. Il decoder 7z (`third_party\lzma`, pubblico dominio) viene compilato insieme.

L'icona si rigenera con `python assets\make_icon.py` (richiede Pillow).

## Licenze

Il codice di MpvLight è distribuito sotto GPL-2.0-or-later (testo in `LICENSE-GPL.txt`). L'header libmpv conserva la licenza ISC; l'unica modifica locale disattiva `dllexport` per il caricamento dinamico.

La DLL è una build di terzi di mpv/FFmpeg e di altre librerie, non una build ufficiale del progetto mpv:

- Build scaricata: `mpv-dev-x86_64-20261021-git-e471496c56.7z`.
- Build e ricette, con riferimenti ai sorgenti delle dipendenze: https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261021
- Sorgente mpv: https://github.com/mpv-player/mpv/tree/e471496c56

Prima di una redistribuzione pubblica occorre raccogliere e distribuire il sorgente completo e le licenze di tutte le dipendenze della build binaria.
