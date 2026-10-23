# Verifiche

## 0.5.1 — stato

Fatto:

- Controllo completo di tipi e sintassi di `src/main.cpp` e `tests/integration.cpp` con `clang++ -fsyntax-only -std=c++17 -Wall -Wextra -Wconversion -Wshadow -Wsign-conversion`, contro gli header Windows di mingw-w64 (GDI+, DWM, shell, comctl), con in più le macro che l'SDK Microsoft definisce sempre e mingw no (`small`, `hyper`, `interface`, `near`/`far`, `ERROR`…): nessun errore e nessun avviso. Il controllo verifica firme delle API, tipi e template, ma non è una compilazione MSVC con link.
- Rilettura del codice: rientranza del timer, focus, attivazione delle finestre owned/modali, cattura del mouse durante i trascinamenti, conversioni numeriche.
- Test automatici del motore invariati (stessa API, nuovo parametro di `seek` con valore predefinito).

- **Estrazione 7z e componenti, eseguiti davvero (Linux, stesso codice portabile):**
  - archivio reale `mpv-dev-x86_64-20261021-git-e471496c56.7z` (31,6 MB, SHA-256 `efc719ef…a633` uguale a quello pubblicato da GitHub): `libmpv-2.dll` estratta (121 108 480 byte), riconosciuta come DLL PE x64;
  - archivio troncato → "archivio troncato"; file non 7z → "non è un archivio 7z"; nome assente → "file non presente nell'archivio";
  - parser JSON e scelta del file su una risposta in formato GitHub API: build standard, variante `-v3-`, ffmpeg e aarch64 esclusi, impronta letta da `digest`; risposta "rate limit" → messaggio di GitHub; JSON troncato o annidato oltre 64 livelli → rifiutato;
  - `isX64Dll` rifiuta un PE senza il flag DLL e un file troppo corto.
- `packaging/setup.cpp` e `src/components.hpp`: controllo di tipi e sintassi come sopra, senza avvisi; `setup.rc` e `app.rc` compilati con `llvm-rc`.

Da fare sul PC Windows, dopo `build.cmd`:

1. Build MSVC /W4 senza avvisi.
2. `test.cmd` con la fixture multitraccia.
3. Prova a schermo:
   - comparsa e scomparsa della barra sopra il video;
   - trascinamento della timeline e del volume;
   - menu scuri;
   - schermo intero con cursore nascosto;
   - adattamento della finestra a un video piccolo e a uno 4K;
   - schermata iniziale con i recenti;
   - finestra Aiuto;
   - drag & drop di più file e di un .srt.
4. Setup:
   - installazione pulita, aggiornamento sopra la 0.3 (Inno), download del motore e annullamento a metà;
   - "Usa un file già scaricato" con un `.7z` e con una `.dll`;
   - collegamenti, "Apri con" e App predefinite;
   - disinstallazione da Impostazioni → App.
5. MpvLight → Aggiorna motore con un video aperto (sostituzione della DLL in uso e riavvio); "Scarica il motore" con la DLL mancante.
6. Verificare che `input-cursor-passthrough` sia supportato dalla build libmpv in uso: il tasto destro e la rotellina sul video devono arrivare a MpvLight anche durante la riproduzione.

## 0.3.0


Compilatore: MSVC x64, C++17, /W4 /O2 /MT. Nessun warning nella compilazione del player e dei test.

## Test automatici

Eseguire `test.cmd C:\percorso\tracks-subs.mkv`. Serve un MKV locale di 120 secondi con due tracce video H.264, due audio AAC e due sottotitoli SubRip. La fixture usata è stata generata con ffmpeg testsrc2, due toni sinusoidali e due file SRT; non usa contenuti esterni.

Risultati effettivamente ottenuti:

- Parsing e validazione mm:ss / hh:mm:ss / secondi, decimali; rifiuto input errati e clamp timeline.
- Enumerazione delle sei tracce, selezione della seconda traccia per ciascun tipo e disattivazione sottotitoli.
- Seek precisi 35 → 90 → 1 → 60 secondi mantenendo la pausa.
- Velocità 1.5×, volume 43%, mute, ripresa della riproduzione.
- Sei cicli stop / riapertura dello stesso file.
- 150 seek concorrenti con 1000 letture dello snapshot e controllo della posizione finale.
- Fine del video e successivo seek a 30 secondi mantenendo la pausa.
- Segnalazione file inesistente, successiva riapertura valida e chiusura del thread.

## Verifica a schermo

Effettuata usando Computer Use sulla GUI reale: apertura MKV, riproduzione hardware D3D11VA, sottotitoli italiani, menu audio e selezione traccia inglese, pulsante Pausa, click timeline, trascinamento, campo Vai con 00:35, screenshot PNG scritto su disco, F11 ed Esc. Le verifiche visuali hanno individuato la necessità di invalidare tutta la traccia colorata degli slider, correggere il pulsante Play a vuoto e usare menu a tendina scuri con la selezione evidenziata.

Limiti: l'integrazione automatica usa uscite audio/video nulle per essere ripetibile. Le prove GUI usano un solo sistema Windows e una sola configurazione GPU; non dimostrano compatibilità con ogni codec, driver o scala DPI.

## Verifiche aggiuntive 0.3

- Cartella screenshot non valida: errore esplicito e nessun falso esito positivo (test automatico sul percorso di un file usato come cartella).
- GUI reale: pulsante Cattura genera un PNG; selezione JPG dal menu genera un JPEG; entrambi sono stati riaperti e validati con Pillow, dimensioni 640×360 come la sorgente.
- Il tasto destro sul video apre il menu con tracce audio/video/sottotitoli, velocità e screenshot.
- La finestra Vai al tempo accetta 00:35 e sposta il fotogramma al secondo 35 anche dopo EOF.
- La build finale ripristina il focus al player dopo il menu e dopo Cattura per mantenere disponibili le scorciatoie.
