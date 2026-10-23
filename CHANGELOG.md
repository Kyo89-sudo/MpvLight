# Changelog

Versioni nel formato `x.y.z`: `z` per le sole correzioni, `y` per nuove funzioni. Le risorse Windows che vogliono quattro numeri (manifest e blocco numerico VERSIONINFO) usano `x.y.z.0`.

## 0.5.1 — 2026-10-23

Correzioni.

- **Sfondo a fasce (banding)** nella schermata iniziale e nella vista solo audio: il gradiente scuro aveva solo una quindicina di gradini di colore. Ora è un bagliore radiale con dithering (rumore statico di ±1 livello, deterministico per pixel), calcolato una volta per dimensione della finestra e tenuto in cache.
- **Sfumatura sotto i controlli** con dithering dell'alpha, disegnata direttamente nei pixel premoltiplicati. I pixel completamente trasparenti restano esattamente 0, così i clic passano ancora al video.
- **Avviso C4701 di MSVC** ("variabile forse non inizializzata") nel parser JSON di `components.hpp`, incluso sia da MpvLight sia dal setup: `cp` e `lo` ora sono inizializzate. Inizializzati anche gli `HKEY` del setup.
- **Possibili avvisi C4459** (variabile locale con lo stesso nome di una globale): rinominati `px` nel dithering e alcune variabili locali di `components.hpp`/`sevenzip.hpp`. Ai controlli si aggiungono un'analisi dell'albero sintattico che emula C4459 e `clang-tidy` (`cppcoreguidelines-init-variables`): nessun caso residuo.
- `WinHttpSendRequest` riceve la lunghezza effettiva delle intestazioni invece di `-1`.
- **Produzione della release** in `build.cmd`:
  - versione in un solo punto (`set VERSION=`);
  - oltre al setup crea `dist\MpvLight-<versione>-portable-x64.zip` (MpvLight.exe, portable.flag, README, CHANGELOG, licenza; il motore si scarica dall'app) con il `tar.exe` di Windows;
  - genera `dist\SHA256SUMS.txt` con le impronte di setup e ZIP.

## 0.5.0 — 2026-10-23

Nuova interfaccia in stile mpv, installer proprio che scarica il motore, aggiornamento del motore dall'app e nuova icona.

La 0.4.0 era una build di sviluppo con la sola nuova interfaccia, che non compilava con MSVC: tutte le sue novità sono incluse qui. Il confronto è quindi con la 0.3.0.

### In breve

- Il video riempie tutta la finestra. I controlli compaiono sopra il video quando muovi il mouse e spariscono dopo 2 secondi.
- La finestra si adatta alle proporzioni del video, fino all'80% dello schermo.
- File precedente/successivo della stessa cartella, file recenti, coda di più file.
- Nuovo setup: scarica da solo l'ultima libmpv e ne verifica l'integrità, crea i collegamenti e le associazioni, e si disinstalla da Impostazioni → App.
- Menu → **Aggiorna motore libmpv…**; se il motore manca, la schermata iniziale offre **Scarica il motore**.
- Nuova icona: disco viola frontale con play in rilievo, nitida anche nella barra delle applicazioni.

### Interfaccia

- **Barra dei controlli sopra il video**:
  - contenuto: timeline, play/pausa, file precedente/successivo, volume, tempo (un clic passa al tempo rimanente), velocità, traccia audio, sottotitoli, menu e schermo intero; a schermo intero anche il titolo;
  - i pulsanti non applicabili restano visibili ma attenuati;
  - compare con il movimento del mouse e si nasconde, con dissolvenza, dopo 2 s, anche in pausa. Il tempo si sceglie in Menu → Finestra: 2 s, 3,5 s, 6 s o sempre visibile;
  - resta visibile solo con un pulsante sotto il puntatore, durante i trascinamenti, con i menu aperti, a fine file e con i file solo audio.
- **Timeline**: si ingrossa al passaggio del mouse e mostra il tempo sotto il puntatore. Il trascinamento sposta il video in tempo reale; al rilascio il salto è preciso.
- **Messaggi a schermo** in alto a sinistra: volume, salti, velocità, tracce, screenshot ed errori (in rosso).
- **Schermata iniziale**: icona, "Apri file…", scorciatoie, file recenti cliccabili, errore del motore con "Scarica il motore".
- **Vista solo audio**: disco con nota musicale e titolo.
- **Menu scuro e riorganizzato**: File, Riproduzione, Velocità, Audio/Video/Sottotitoli, Screenshot, Finestra, Aiuto.
- **Aiuto** (F1): finestra scura con le schede Scorciatoie e Informazioni (versione, motore, build installata, decodifica, cartelle, licenza, link).
- **Vai al tempo** (Ctrl+G): dialogo scuro con la durata indicata.
- **Icona**: disco viola frontale con bordo smussato, riflesso e play in rilievo. Da 16 a 32 px è disegnata apposta a quella misura.

### Finestra

- Prima apertura al 70% dello schermo in 16:9; poi posizione, dimensione e stato massimizzato ricordati.
- **Adatta la finestra al video** (Menu → Finestra, attivo di predefinito): ingrandisce anche i video piccoli; non interviene se la finestra è massimizzata o a schermo intero.
- **Sempre in primo piano** (Ctrl+T).
- Cursore nascosto a schermo intero quando il mouse è fermo.

### Riproduzione

- **Cartella come playlist**: i file multimediali della cartella in ordine naturale; precedente/successivo con pulsanti, PagSu/PagGiù o tasti multimediali. Opzione "Passa da solo al file successivo".
- Più file trascinati o selezionati insieme diventano una coda; un file di sottotitoli trascinato sul video viene caricato.
- **File recenti** (fino a 8), con rimozione automatica di quelli che non esistono più.
- Volume ricordato tra un avvio e l'altro.
- **Nuovi comandi**:
  - fotogramma successivo/precedente;
  - velocità ±0,25× e normale;
  - cambio rapido di sottotitoli e traccia audio;
  - cattura senza sottotitoli;
  - mostra il file nella cartella;
  - rotellina per il volume, clic centrale per il muto, doppio clic per lo schermo intero;
  - tasti multimediali.
- La cattura si sposta dalla barra al menu e ai tasti S, Shift+S e Ctrl+S.

### Motore libmpv: download e aggiornamento

- Il motore arriva dalle build pubbliche [shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake): ultima release, archivio `mpv-dev-x86_64-*.7z`, oppure la variante `-v3-` sui processori con AVX2.
- Impronta SHA-256 verificata contro quella pubblicata da GitHub; la DLL estratta viene controllata (PE x64 con flag DLL).
- Senza Internet si può usare un `.7z` o una `libmpv-2.dll` già scaricati.
- Aggiornamento dall'app anche con un video aperto: la DLL in uso viene rinominata in `.old` (rimossa al prossimo avvio) e MpvLight propone il riavvio, riaprendo il file corrente.
- Ricerca della DLL: `[Engine] Library=` in settings.ini, cartella dell'eseguibile (`libmpv-2.dll` o `mpv-2.dll`), cartella superiore, `%LOCALAPPDATA%\Programs\MpvLight`.
- Se il caricamento fallisce, il messaggio spiega il motivo (file assente o dipendenza mancante, DLL non a 64 bit, codice di errore) e il percorso cercato.

### Installer (sostituisce Inno Setup)

- `MpvLight-Setup-0.5.0-x64.exe` installa per l'utente corrente, senza UAC, in `%LOCALAPPDATA%\Programs\MpvLight` (o nella cartella scelta).
- **Pagine**: opzioni (cartella, menu Start, desktop, "Apri con", motore, file locale), avanzamento annullabile, fine (avvia MpvLight; se il motore non è arrivato: riprova o usa un file), errore con "Riprova".
- **Cosa installa**: MpvLight.exe, README, CHANGELOG e licenza (incorporati nel setup), libmpv scaricata, collegamenti, "Apri con" per 21 estensioni, App predefinite di Windows, voce di disinstallazione e una copia del setup come disinstallatore.
- Chiude MpvLight se è aperto; ne può girare una sola istanza alla volta.
- **Aggiornamento dalla 0.3**: stessa cartella; rimuove la voce Inno, il disinstallatore Inno, la cartella `source` e il vecchio gruppo del menu Start, ma solo se la cartella contiene davvero un'installazione Inno di MpvLight.
- **Disinstallazione** (Impostazioni → App o `MpvLightSetup.exe /uninstall`): rimuove solo i file installati (la cartella sparisce se resta vuota), i collegamenti, le associazioni e le chiavi di registro; a richiesta anche le impostazioni. Gli screenshot restano.
- **Riga di comando**: `/S` silenzioso (codice d'uscita 0 = ok, 1 = errore, 2 = installato senza motore), `/dir "percorso"`, `/uninstall`.

### Dettagli tecnici

- **Finestre**:
  - il video è un figlio `MpvLightVideo` passato a libmpv come `wid`;
  - barra e messaggi sono popup owned `WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, disegnati con GDI+ in un DIB 32 bit premoltiplicato e mostrati con `UpdateLayeredWindow` (i pixel trasparenti lasciano passare i clic);
  - i messaggi usano anche `WS_EX_TRANSPARENT`;
  - stato dell'interfaccia aggiornato da un timer di 16 ms che legge lo snapshot del motore.
- **Icone**: font di sistema Segoe Fluent Icons, con ripiego su Segoe MDL2 Assets. Icone della finestra caricate con `LoadIconWithScaleDown` alla misura dei DPI correnti (`WM_SETICON`, aggiornate su `WM_DPICHANGED`).
- **Menu scuri**: `SetPreferredAppMode(ForceDark)` e `FlushMenuThemes` (uxtheme, ordinali 135/136). Le `&` nei nomi delle tracce sono raddoppiate.
- **Motore** (`engine.hpp`, `player.hpp`):
  - `seek(time, paused, exact)`: `absolute+keyframes` durante il trascinamento (al massimo ogni 70 ms), `absolute+exact` al rilascio;
  - opzioni `input-cursor-passthrough=yes`, `cursor-autohide-fs-only=yes`, `osd-level=0`;
  - `videoW`/`videoH` da `video-out-params/dw|dh`;
  - `utf8()`/`wide()` sicure con stringhe vuote.
- **Componenti** (`components.hpp`, condiviso da app e setup):
  - API GitHub con un parser JSON minimale (escape `\u`, coppie surrogate, profondità limitata);
  - download WinHTTP con proxy di sistema, redirect, timeout, annullamento, limite di dimensione e progresso;
  - SHA-256 con BCrypt;
  - scrittura atomica (`.new` → rename). Un file in uso viene spostato su un nome `.old`, `.old1`… libero; se la sostituzione fallisce, il file originale viene rimesso al suo posto.
- **Estrazione 7z** (`sevenzip.hpp`): decoder dell'LZMA SDK (`third_party/lzma`, 18 file C in pubblico dominio) su un `ILookInStream` in memoria, con messaggi d'errore in italiano.
- **Robustezza**:
  - volume, muto e velocità impostati dall'utente prevalgono per 400 ms sullo snapshot del motore, così i cursori non tremolano;
  - message box fuori dal timer (`WM_APP + 1`), per evitare la rientranza;
  - le finestre modali riabilitano la principale prima di chiudersi;
  - `GetHDC`/`ReleaseHDC` per disegnare icone dentro GDI+;
  - buffer corretti nel drag & drop;
  - percorsi da riga di comando normalizzati, con più file accettati.
- **Build**:
  - `build.cmd` trova Visual Studio con `vswhere`, compila il decoder 7z, MpvLight e il setup (`dist\`);
  - definisce `UNICODE`/`_UNICODE`/`NOMINMAX` e collega `winhttp.lib` e `bcrypt.lib`;
  - copia `libmpv-2.dll` nella cartella portatile se ne trova una;
  - `test.cmd` usa la stessa ricerca e, se manca la DLL portatile, quella installata.

### Correzioni rispetto alla 0.4.0 di sviluppo

- **Compilazione con MSVC**: un campo chiamato `small` collideva con la macro `#define small char` di `rpcndr.h`. Il campo ora si chiama `caption`, e `min`/`max` sono resi visibili a GDI+ con `NOMINMAX`.
- **"Impossibile caricare libmpv"** con il `portable\MpvLight.exe` appena compilato: ricerca della DLL estesa e messaggio d'errore esplicito.
- **Icona sfocata** nella barra delle applicazioni con lo schermo in scala.
- **Barra dei controlli che non spariva** in pausa o con il mouse fermo in basso.
- **Setup e aggiornamento del motore**:
  - la sostituzione di un file in uso non lascia mai la cartella senza DLL;
  - "Annulla" ferma l'installazione prima del download;
  - la pulizia dei resti di Inno non tocca file che non sono di Inno.

### Verifiche

Vedi TESTING.md: controllo di tipi e sintassi con clang e gli header Windows (macro dell'SDK Microsoft comprese) senza avvisi; estrazione 7z, scelta della release e controllo della DLL eseguiti sull'archivio reale di libmpv. Le prove della GUI sul PC Windows sono elencate lì.

## 0.3.0

Interfaccia con barra fissa sotto il video, pulsanti owner-draw e trackbar nativi, motore libmpv su thread dedicato, screenshot verificati su disco, installer Inno Setup.
