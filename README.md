# Emulador Ensoniq EPS-16 Plus

Emulador do Ensoniq EPS-16 Plus (versão rack), ao nível do hardware. Corre a ROM original
(EPROMs U27/U28) e arranca o sistema operativo a partir da imagem de disquete (testado com o
OS 1.30). Nada do OS é simulado por alto: o código 68000 original corre sobre CPU, periféricos
e chips de som emulados. Pensado para Windows; também compila em Linux.

## Estado

**Funciona (confirmado em testes):**
- Arranque completo: ROM, depois o OS 1.30 carregado da disquete ("LOADING SYSTEM").
- Carregar instrumentos da disquete e tocá-los por MIDI, com os efeitos do ESP.
- Sampling: entrada de áudio do computador ou de um WAV, página de nível com a barra do medidor,
  as frequências 44.6 / 29.8 / 22.3 / 17.9 kHz e a tecla de raiz.
- Sequenciador: gravar (RECORD + PLAY), guardar a pista e tocar, no andamento certo.
- SCSI (opção do EPS): detetado no arranque, CHANGE STORAGE DEVICE, carregar e FORMAT SCSI DRIVE.
- Saídas do expansor OEX-6 (AUX 1, BUS 2, BUS 3) em pares de saídas da placa de som.
- Patch Select (entrada analógica AN1) e sliders de Volume e Data Entry com a escala do OS.
- Gravar na disquete, formatar, trocar de disco com a máquina ligada.

## Hardware emulado

| Chip | Função |
|---|---|
| MC68000 a 10 MHz (Musashi) | CPU |
| ES5505 OTIS | gerador de som, 21 vozes a ~29,76 kHz (7 vozes / 89 kHz durante o sampling) |
| ES5510 ESP | efeitos; também decima a entrada do A/D durante o sampling |
| HD68440 DMAC | DMA da disquete (canal 0) e do SCSI (canal 1) |
| MC68681 DUART | MIDI (canal A), painel (canal B), timer do sequenciador (X1 = 5 MHz) |
| WD1772 | disquete 800 KB |
| WD33C93 | SCSI (opcional) |
| PCM78 + 74LS157 | A/D do sampling na entrada SER0 do ESP |

Mapa de memória: RAM do OS em 000000-07FFFF, OTIS em 200000, DMAC em 240000, ESP em 260000,
DUART em 280000, WD1772 em 2C0000, SCSI em 300000/340000, memória de som em 600000-7FFFFF
(banco 1 do OTIS), ROM em C00000. Mais detalhes em `src/eps16.h`.

## Compilar

Precisa de um compilador C++17, CMake e, para a app com janela, do **SDL2**.

**Windows (MSYS2 / MinGW64):**
```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2
cmake -S . -B build -G "MinGW Makefiles" && cmake --build build -j
```

**Compilação cruzada para Windows a partir de Linux** (llvm-mingw): copia
`mingw64.cmake.example` para `mingw64.cmake`, ajusta os caminhos e:
```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=mingw64.cmake -DCMAKE_EXE_LINKER_FLAGS="-static -s"
cmake --build build-win -j
```

**Linux:**
```sh
sudo apt install build-essential cmake libsdl2-dev libasound2-dev
cmake -S . -B build && cmake --build build -j
```
Com `-DEPS16_BUNDLED_SDL=ON` o SDL2 é compilado a partir do código e ligado estaticamente.
Opções: `-DEPS16_MIDI=OFF` (sem RtMidi), `-DEPS16_ASIO=OFF` (sem ASIO no Windows).

Resultado: `eps16` (app com janela) e `eps16-cli` (sem janela, para testes e para gravar WAV).

### Plug-in VST3

```sh
cmake -S plugin -B build-vst -DCMAKE_BUILD_TYPE=Release
cmake --build build-vst --target EPS16
```
Descarrega o SDK VST3 da Steinberg (licença MIT desde a 3.8) e o SDL2 (só para desenhar o painel).
Opções: `-DVST3_SDK_ROOT=…`, `-DSDL2_SOURCE_DIR=…`, `-DEPS16_VST_GUI=OFF` (sem editor) e
`-DEPS16_VST_TESTHOST=ON` (o `minihost`, anfitrião mínimo para testes). Windows: com o Visual Studio,
`cmake -S plugin -B build-vst -A x64`; a partir de Linux, `./build-vst-win-mingw.sh <pasta do llvm-mingw>`
(o `tools/patch_vst3sdk_mingw.py` adapta o SDK ao MinGW).

## Ficheiros necessários

Não estão incluídos. Coloca na pasta `roms` ao lado do executável:
- `eps16plus-u28.bin` (EPROM U28, bytes pares) e `eps16plus-u27.bin` (U27, bytes ímpares);
- `os130.img`, a imagem da disquete do OS (ou qualquer `.img` que seja a única da pasta).

## Utilização

Ver `LEIA-ME-WINDOWS.txt` para o guia completo. Resumo:
- **Menus da janela:** File (abrir/ejetar/gravar disco, reset) e View (teclado do ecrã, Options).
- Arrasta uma `.img` para a janela para a meter no drive (ou no SCSI, se tiver nome de disco SCSI).
- Arrasta um ou mais instrumentos `.efe` para a janela: vão para uma disquete temporária que entra
  no drive (depois é LOAD como de qualquer disquete).
- Clique direito numa seta Up/Down = as duas setas juntas: o valor salta para o meio da escala
  (0 em -99…+99, 50 em 0…99), como no aparelho.
- O `os130.img` vem com os parâmetros globais em modo MIDI MULTI, instrumentos 1-8 nos canais 9-16;
  por omissão o MIDI In é encaminhado para o "Keyboard channel" das opções.

**Teclado do computador** (não toca notas: usa um teclado MIDI ou o teclado do ecrã):

| Tecla | Função |
|---|---|
| 1–8, F1–F8 | botões de instrumento 1–8 |
| teclado numérico | botões de página como impressos: 1 Env 1 … 4 Pitch … 9 Layer, 0 Track |
| R | RECORD (mantém-no e carrega em Espaço para gravar) |
| Espaço | PLAY |
| Setas | setas do painel (Up + Down juntas = meio da escala) |
| Enter / Esc | YES•ENTER / NO•CANCEL |
| F11 / F12 | Patch Select esquerdo / direito |
| F9, Ctrl+O | teclado do ecrã, opções |
| Ctrl+E / Ctrl+S / Ctrl+R | ejetar / gravar disquete / reset |
| Ctrl+F, Ctrl+I | efeitos on/off, entrada de áudio on/off |

### Discos SCSI

Imagens com os nomes do ZuluSCSI (`HD0.img` disco no ID 0, `CD5.iso` CD-ROM no ID 5…) numa pasta.
Escolhe a pasta em **View → Options → SCSI DRIVES → Drive images folder → Choose folder...** (fica em
`eps16.cfg`, `scsi_dir=`), ou dá-a com `--scsi-dir`. Sem escolha, usa a pasta `scsi` ao lado do programa.
Depois de mudar de pasta, reinicia o EPS (File → Reset) para o OS ver as drives novas.

**Criar um disco novo:** em **SCSI DRIVES**, escolhe **New drive size** (50 MB a 2 GB) e carrega em
**New drive → Create HDn.img**: fica na pasta com o primeiro ID SCSI livre, já formatado como o FORMAT
SCSI DRIVE do EPS o deixa (não é preciso formatar). Depois File → Reset para o OS o ver.

### Plug-in VST3 (`EPS16.vst3`)

- Copia a pasta `EPS16.vst3` para `C:\Program Files\Common Files\VST3` e as ROMs e o `os130.img` para
  `%APPDATA%\Ensoniq\EPS16emu\roms` (a mesma pasta que a app usa).
- Saídas Main + AUX 1, BUS 2, BUS 3 (as do OEX-6; ativa-as no anfitrião para saírem à parte); entrada
  estéreo para samplar; MIDI In/Out; "Relógio MIDI do host" para o sequenciador seguir o projeto.
- **MIDI → canal**: o `os130.img` está em MIDI MULTI (instrumento 1 no canal 9), por isso as notas do
  anfitrião vão por omissão para o canal 9; o menu da barra (ou o parâmetro "Canal MIDI") escolhe outro
  canal ou deixa os canais do anfitrião.
- Barra: Disquete…, Ejetar, Disco SCSI…, Desligar SCSI, **Pasta SCSI…**, Reset. A pasta SCSI fica
  guardada no projeto e como omissão para novas instâncias e para a app. Arrastar `.img` ou `.efe` para
  o painel funciona como na app; clique direito numa seta = cima + baixo.
- O projeto guarda volume, relógio, canal MIDI, pasta SCSI e os caminhos dos discos; ao abrir, o EPS
  arranca de novo com eles.

## Estrutura do código

| Ficheiro | Conteúdo |
|---|---|
| `src/eps16.*` | a máquina: mapa de memória, interrupções, ligações entre chips, áudio |
| `src/es5505.*`, `src/es5510.*` | OTIS e ESP |
| `src/hd63450.*`, `src/wd1772.*`, `src/scsi.*`, `src/duart.*` | DMAC, disquete, SCSI, DUART |
| `src/panel.*`, `src/vfd.*` | protocolo do painel e mostrador VFD |
| `src/sdl_main.cpp`, `src/rack_ui.*`, `src/ui_text.*`, `src/native_menu.*` | app com janela |
| `src/cli.cpp` | versão de linha de comandos (testes) |
| `src/efe_disk.*` | põe ficheiros `.efe` numa disquete temporária (largar na janela) |
| `src/rack_offscreen.*` | desenha o painel fora do ecrã (editor do VST3) |
| `plugin/` | plug-in VST3: `eps16_plugin.*`, `eps16_engine.*` (máquina e partilha do 68000), `eps16_editor.*` |
| `tools/` | geradores da arte do painel, fontes, ícones; `efe2img.py` junta ficheiros EFE a uma imagem |

## Licenças e créditos

- **Musashi**, núcleo 68000, de Karl Stenerud: ver `musashi/LICENSE-Musashi.txt`.
- **ES5505 e ES5510:** algoritmos derivados do MAME, licença BSD-3-Clause
  (`LICENSES/BSD-3-Clause-MAME.txt`).
- **RtMidi** e **RtAudio**, de Gary P. Scavone: ver `third_party/RtMidi-LICENSE.txt` e
  `third_party/rtaudio/RtAudio-LICENSE.txt`.
- **ASIO:** os ficheiros do ASIO SDK da Steinberg (`third_party/rtaudio/include/asio*`) estão
  sob GPLv3, por isso um executável compilado com ASIO fica sob GPLv3. ASIO é marca registada da
  Steinberg Media Technologies GmbH.
- **SDL2**: licença zlib.
- **VST3 SDK** e **VSTGUI**, da Steinberg: licença MIT (SDK 3.8). VST é marca registada da Steinberg
  Media Technologies GmbH.
- Legendas do painel e das opções compostas com **GNU FreeSans** (GPLv3 com exceção para fontes);
  só estão incluídas imagens já desenhadas.
- A ROM e o sistema operativo do EPS-16 Plus são propriedade da Ensoniq/E-mu e **não** estão
  incluídos.
