# Decoder 7z (LZMA SDK)

File presi da 7-Zip di Igor Pavlov (https://github.com/ip7z/7zip, cartella `C/`, commit b88c6bc0 del 21/10/2026): solo quelli necessari a leggere archivi `.7z` (LZMA, LZMA2, PPMd, BCJ/BCJ2, Delta).

Ogni file dichiara nell'intestazione lo stato di **pubblico dominio** ("Igor Pavlov : Public domain"), quindi non aggiunge vincoli di licenza a MpvLight.

MpvLight li usa per estrarre `libmpv-2.dll` dagli archivi `mpv-dev-x86_64-*.7z` pubblicati su https://github.com/shinchiro/mpv-winbuild-cmake (vedi `src/sevenzip.hpp` e `src/components.hpp`). `build.cmd` li compila con `/DZ7_PPMD_SUPPORT /DZ7_EXTRACT_ONLY`.
