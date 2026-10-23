<p align="center">
  <img src="assets/mpvlight.png" alt="Icona di MpvLight" width="128" height="128">
</p>

<h1 align="center">MpvLight</h1>

<p align="center">
  Lettore multimediale leggero per Windows, basato su libmpv.<br>
  Interfaccia scura, controlli sul video e nessun framework UI aggiuntivo.
</p>

<p align="center">
  <strong>0.5.1 · Windows x64 · C++ / Win32 · GPL-2.0-or-later</strong>
</p>

<p align="center">
  <a href="https://github.com/Kyo89-sudo/MpvLight/releases/latest">Scarica MpvLight</a> ·
  <a href="CHANGELOG.md">Novità</a> ·
  <a href="TESTING.md">Test</a> ·
  <a href="https://github.com/Kyo89-sudo/MpvLight/issues">Segnala un problema</a>
</p>

## Download

| Versione | Download | Come usarla |
|---|---|---|
| **Setup** | [MpvLight-Setup-0.5.1-x64.exe](https://github.com/Kyo89-sudo/MpvLight/releases/download/v0.5.1/MpvLight-Setup-0.5.1-x64.exe) | Installa per l’utente corrente, crea i collegamenti e configura “Apri con”. Non richiede permessi da amministratore. |
| **Portatile** | [MpvLight-0.5.1-portable-x64.zip](https://github.com/Kyo89-sudo/MpvLight/releases/download/v0.5.1/MpvLight-0.5.1-portable-x64.zip) | Estrai lo ZIP e avvia `MpvLight.exe`. Mantieni `portable.flag` per salvare le impostazioni nella stessa cartella. |
| **Verifica integrità** | [SHA256SUMS.txt](https://github.com/Kyo89-sudo/MpvLight/releases/download/v0.5.1/SHA256SUMS.txt) | Confronta l’impronta SHA-256 del file scaricato con quella pubblicata. |

**Il motore libmpv non è incluso nei pacchetti:** il setup lo scarica durante l’installazione; la versione portatile propone **Scarica il motore** al primo avvio.

## Inizia a usarlo

1. Installa il setup oppure estrai la versione portatile.
2. Lascia scaricare il motore, se richiesto.
3. Premi **Apri file…**, usa `Ctrl+O` o trascina un file sulla finestra.

Per aggiungere sottotitoli, trascina il relativo file sul video in riproduzione. La schermata iniziale mostra anche i file recenti e le scorciatoie principali.

## Funzioni

| Area | Cosa offre |
|---|---|
| **Interfaccia** | Video a tutta finestra, tema scuro e controlli semitrasparenti sopra il video. |
| **Riproduzione** | Play/pausa, velocità regolabile, avanzamento per fotogrammi e navigazione tra i file della stessa cartella. |
| **Timeline** | Ricerca nel video con anteprima del tempo al passaggio del mouse. |
| **Audio e sottotitoli** | Selezione delle tracce, volume, muto e caricamento dei sottotitoli tramite trascinamento. |
| **Schermo intero** | Attivabile da tastiera, pulsante o doppio clic. |
| **Finestra** | Adattamento alle proporzioni del video, fino all’80% dello schermo; opzione disattivabile. |
| **Screenshot** | PNG o JPG alla risoluzione originale, con o senza sottotitoli. |
| **Preferenze** | Memorizza volume, posizione e dimensione della finestra, file recenti e opzioni. |
| **Motore** | Download e aggiornamento di libmpv dall’app, con verifica SHA-256 della build scaricata. |
| **Accelerazione hardware** | Automatica quando GPU, driver e codec la supportano, con ripiego software. |

### Controlli sul video

Muovi il mouse per mostrare la barra. Per impostazione predefinita scompare dopo **2 secondi**, anche in pausa. Resta visibile mentre punti un pulsante, trascini un controllo, a fine file e con contenuti solo audio.

Da **Menu → Finestra** puoi scegliere 2 s, 3,5 s, 6 s o la visualizzazione permanente. Un clic sul tempo passa dalla durata al tempo rimanente. I messaggi di stato compaiono in alto a sinistra e svaniscono automaticamente.

## Scorciatoie

| Azione | Tasti / mouse |
|---|---|
| Riproduci / pausa | `Spazio`, `K` |
| Indietro / avanti di 5 secondi | `←` / `→` |
| Salti di 1 / 30 secondi | `Shift+←/→` / `Ctrl+←/→` |
| Volume | `↑` / `↓`, rotellina |
| Muto | `M`, clic centrale |
| Schermo intero | `F`, `F11`, `Alt+Invio`, doppio clic |
| Esci dallo schermo intero | `Esc` |
| File precedente / successivo | `PagSu` / `PagGiù` |
| Aumenta / riduci / ripristina velocità | `+` / `−` / `Backspace` |
| Fotogramma successivo / precedente | `.` / `,` |
| Cambia sottotitoli / traccia audio | `J` / `Shift+J` |
| Screenshot con sottotitoli | `S`, `Ctrl+S` |
| Screenshot senza sottotitoli | `Shift+S` |
| Apri file | `Ctrl+O` |
| Vai al tempo | `Ctrl+G` |
| Sempre in primo piano | `Ctrl+T` |
| Menu completo / aiuto | Tasto destro / `F1` |

Sono supportati anche i tasti multimediali della tastiera.

## Installazione e impostazioni

| Voce | Setup | Versione portatile |
|---|---|---|
| Avvio | Collegamento nel menu Start; desktop facoltativo | `MpvLight.exe` nella cartella estratta |
| Cartella del programma | `%LOCALAPPDATA%\Programs\MpvLight` per impostazione predefinita | Cartella scelta dall’utente |
| Impostazioni | `%LOCALAPPDATA%\MpvLight` | `settings.ini` accanto all’eseguibile, se presente `portable.flag` |
| Screenshot predefiniti | `Immagini\MpvLight` | `Screenshots` accanto all’eseguibile |
| Associazioni file | Aggiunta ad “Apri con” e alle App predefinite | Nessuna installazione richiesta |
| Rimozione | Impostazioni di Windows → App | Elimina la cartella quando non serve più |

La cartella degli screenshot si cambia da **Menu → Screenshot**. Prima della cattura viene verificata la scrivibilità; la conferma arriva solo quando il file esiste su disco.

Il setup aggiorna la cartella usata dalla versione 0.3 e rimuove i resti del precedente installer Inno Setup.

### Installazione senza Internet e opzioni del setup

Puoi fornire un archivio `mpv-dev-x86_64-….7z` oppure una `libmpv-2.dll` già scaricata. L’estrazione del motore è integrata e non richiede 7-Zip.

| Opzione | Effetto |
|---|---|
| `/S` | Installazione silenziosa |
| `/dir "C:\percorso"` | Cartella di destinazione personalizzata |
| `/uninstall` | Disinstallazione |

## Motore libmpv

| Aspetto | Comportamento |
|---|---|
| Download | Build x64 da [shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake), verificata con l’impronta SHA-256 pubblicata da GitHub |
| Processori AVX2 | Selezione della build ottimizzata `-v3` quando supportata |
| Aggiornamento | **Menu → Aggiorna motore libmpv…**, seguito dal riavvio proposto dall’app |
| Decodifica | `hwdec=auto-safe` su GPU Intel, AMD e NVIDIA compatibili; ripiego software |
| Diagnostica | **Aiuto → Informazioni** mostra la decodifica in uso |

La DLL viene cercata accanto all’eseguibile, poi nel percorso `[Engine] Library=` di `settings.ini` e nell’installazione in `%LOCALAPPDATA%\Programs\MpvLight`.

Le chiamate a libmpv vengono eseguite su un thread dedicato. Lo stato è protetto da mutex e i comandi passano attraverso una coda che accorpa seek e variazioni di volume e velocità.

## Compilazione

| Requisito | Utilizzo |
|---|---|
| Windows x64 | Piattaforma del programma |
| Visual Studio con strumenti C++ | Compilatore MSVC e risorse Windows; individuato automaticamente con `vswhere` |
| Supporto C++17 | Compilazione del codice dell’app |
| `tar.exe` di Windows | Creazione dello ZIP portatile; disponibile da Windows 10 1803 |
| Python e Pillow, facoltativi | Rigenerazione dell’icona con `python assets\make_icon.py` |

Dalla cartella del progetto esegui:

```bat
build.cmd
```

| File generato | Contenuto |
|---|---|
| `portable\MpvLight.exe` | Eseguibile compilato |
| `dist\MpvLight-Setup-0.5.1-x64.exe` | Setup con app, README, changelog e licenza incorporati |
| `dist\MpvLight-0.5.1-portable-x64.zip` | Pacchetto portatile con documentazione e `portable.flag`, senza libmpv |
| `dist\SHA256SUMS.txt` | Impronte SHA-256 di setup e ZIP |

Se disponibile nella cartella del progetto o nell’installazione locale, `libmpv-2.dll` viene copiata in `portable\`. Il decoder 7z incluso in `third_party/lzma` viene compilato insieme al progetto.

Per cambiare versione, aggiorna `VERSION` in `build.cmd`, `kVersion` nei sorgenti e le versioni nelle risorse `.rc` e nei manifest.

### Struttura del progetto

| Percorso | Contenuto |
|---|---|
| `src/` | App Win32, interfaccia, riproduzione e gestione del motore |
| `src/mpv/` | Header dell’API libmpv |
| `assets/` | Icona PNG/ICO e script di generazione |
| `packaging/` | Sorgenti e risorse del setup |
| `third_party/lzma/` | Decoder 7z del LZMA SDK |
| `tests/` | Test di integrazione |
| `build.cmd` | Compilazione e preparazione dei pacchetti |
| `test.cmd` | Compilazione e avvio dei test di integrazione |

## Test e segnalazioni

Le procedure e i requisiti dei test sono descritti in [TESTING.md](TESTING.md). Per i test di integrazione:

```bat
test.cmd C:\percorso\video-con-tracce-e-sottotitoli.mkv
```

I binari della release 0.5.1 sono stati compilati con Visual Studio; i test di riproduzione non sono stati eseguiti durante il caricamento su GitHub.

Per [segnalare un problema](https://github.com/Kyo89-sudo/MpvLight/issues), indica versione di MpvLight, versione di Windows, comportamento osservato e passaggi per riprodurlo. Aggiungi formato del file e dettagli del motore quando pertinenti.

## Licenze e componenti

| Componente | Licenza / riferimenti |
|---|---|
| MpvLight | **GPL-2.0-or-later** — [testo della licenza](LICENSE-GPL.txt) |
| Header libmpv | ISC, conservata nell’header; modifica locale per disattivare `dllexport` nel caricamento dinamico |
| Decoder 7z / LZMA SDK | Pubblico dominio — [note del componente](third_party/lzma/README.md) |
| Motore scaricato | Build di terzi di mpv, FFmpeg e altre librerie; licenze e sorgenti dipendono dalla build |

Riferimenti della build del motore documentata per questa versione:

| Riferimento | Collegamento |
|---|---|
| Archivio | `mpv-dev-x86_64-20261021-git-e471496c56.7z` |
| Build e ricette delle dipendenze | [Release 20261021](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261021) |
| Sorgenti mpv | [Commit e471496c56](https://github.com/mpv-player/mpv/tree/e471496c56) |

Il motore proviene da una build di terzi, non da una build ufficiale del progetto mpv. I pacchetti qui pubblicati non includono la DLL. Per redistribuire una build binaria del motore occorre raccogliere e distribuire il sorgente completo e le licenze di tutte le relative dipendenze.

Ulteriori note: [SORGENTI.md](SORGENTI.md) · [Changelog completo](CHANGELOG.md).
