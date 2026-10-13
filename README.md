# QPSK SDR Lab — учебный код

Практикум на C++17 для **QPSK SDR Lab с обязательными 2 RX и 2 TX**.
Работа с платой — напрямую через **libiio** и **libad9361-iio**, без SoapySDR и UHD.

Сейчас реализованы программы к главам 2, 5 и 6. Остальные главы будут дополняться
после апробации; обещанных, но отсутствующих RF-программ в этом репозитории нет.
PDF методического пособия — [docs/manual.pdf](docs/manual.pdf) (рабочая версия).
LaTeX-исходники и рабочие материалы авторов сюда не входят.

## Начать без платы

Установите Git, компилятор C++17, CMake (не ниже 3.20) и Make штатным менеджером
пакетов вашей ОС. Названия пакетов и сборка libiio — в
[docs/build-libiio.md](docs/build-libiio.md).

```bash
git clone https://github.com/MCHelectronics/LETI_MIR_accelerator.git
cd LETI_MIR_accelerator
cmake -S . -B build/ch02 -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/ch02 --parallel
ctest --test-dir build/ch02 --output-on-failure
./build/ch02/env_report
mkdir -p build/ch02/results
./build/ch02/iq_tone build/ch02/results/tone.cf32 1000000 100000 0.01 0.5
wc -c build/ch02/results/tone.cf32
```

Ожидается 6 тестов, 10000 комплексных отсчётов и 80000 байт. Формат:
little-endian IEEE-754 float32, чередование I,Q; рядом JSON с параметрами.
Генератор не перезаписывает существующие файлы — для повтора задайте другое имя.

## Затем — libiio и проверка каналов

1. Соберите локальные libiio и libad9361-iio по [инструкции](docs/build-libiio.md).
2. Настройте подключение по [docs/connection.md](docs/connection.md).
3. Запустите `qpsk_channels` с явным URI платы. Он читает описание IIO,
   проверяет обе пары I/Q в RX и TX, не создаёт потоковых буферов, не пишет
   атрибуты и не запускает RF-передачу.

Наличие 2RX2TX в описании — только первый этап, не доказательство одновременного
приёма/передачи с требуемой скоростью. Это предстоит проверить на стенде.
Не перепрошивайте плату автоматически, если проверка каналов не прошла.

## Глава 3: состояние платы и предел потока

```bash
cmake -S . -B build/ch03 -DCMAKE_BUILD_TYPE=Release \
  -DLAB_WITH_IIO=ON -DLAB_IIO_PREFIX="$PWD/build/iio-install"
cmake --build build/ch03 --parallel
mkdir -p build/ch03/results
./build/ch03/board_state ip:192.168.2.1 --json build/ch03/results/board.json
./build/ch03/rx_benchmark --uri ip:192.168.2.1
```

`board_state` только читает атрибуты: режим каналов, гетеродины, цепочку
частот, усиление, состояние генераторов DDS, температуры и питание.
`rx_benchmark` только принимает: для каждой частоты дискретизации измеряет
поток, дошедший до ПК, и читает флаг переполнения DMA; в конце возвращает
исходную частоту дискретизации. Автоматических тестов у них нет — результат
зависит от платы и линии связи.

## Глава 5: приём, спектр и FM

Нужны FFTW3 и OpenCV: `sudo dnf install fftw-devel opencv-devel` (RED OS) или
`sudo apt install libfftw3-dev libopencv-dev` (Debian/Ubuntu). Затем, после
локальной сборки libiio и libad9361-iio:

```bash
cmake -S . -B build/ch05 -DCMAKE_BUILD_TYPE=Release \
  -DLAB_WITH_IIO=ON -DLAB_IIO_PREFIX="$PWD/build/iio-install" -DLAB_WITH_DSP=ON
cmake --build build/ch05 --parallel
ctest --test-dir build/ch05 --output-on-failure
mkdir -p build/ch05/results
./build/ch05/rx_capture --uri ip:192.168.2.1 --freq 98.35e6 --rate 1e6 \
  --gain 40 --samples 3000000 --out build/ch05/results/fm98.cf32
./build/ch05/spectrum_view build/ch05/results/fm98.cf32
./build/ch05/fm_demod build/ch05/results/fm98.cf32 build/ch05/results/fm98.wav --offset -250000
```

Ожидается 12 тестов; они работают без платы. `rx_capture` только принимает:
TX-буфер не создаётся. Частоту станции и смещение `--offset` подберите по
спектру своей записи.

## Глава 6: передача TX→RX, тон, AM и FM

Только после решения GO по главе 1: кабельный тракт TX1 → DC-блокер →
аттенюатор 30 дБ → RX1 собран при выключенном передатчике, антенны с этих
разъёмов сняты.

```bash
cmake -S . -B build/ch06 -DCMAKE_BUILD_TYPE=Release \
  -DLAB_WITH_IIO=ON -DLAB_IIO_PREFIX="$PWD/build/iio-install" \
  -DLAB_WITH_DSP=ON -DLAB_WITH_TX=ON
cmake --build build/ch06 --parallel
ctest --test-dir build/ch06 --output-on-failure
mkdir -p build/ch06/results
./build/ch06/sig_gen --type tone --out build/ch06/results/tone.cf32
./build/ch06/tx_play --uri ip:192.168.2.1 --freq 433.92e6 \
  --in build/ch06/results/tone.cf32 --tx-gain -40 --seconds 60 --go
# во втором терминале, пока идёт передача:
./build/ch06/rx_capture --uri ip:192.168.2.1 --freq 433.92e6 --rate 1e6 \
  --gain 20 --samples 1000000 --out build/ch06/results/rx_tone.cf32
./build/ch06/spectrum_view build/ch06/results/rx_tone.cf32 --nfft 1000
```

Ожидается 15 тестов. `tx_play` без `--go` к плате не подключается; при
старте и при выходе (в том числе по Ctrl+C) ставит максимальное ослабление
−89,75 дБ и выключает гетеродин TX.

## Файлы

- `examples/ch02/` — отчёт о компиляторе, генератор тона и инспектор каналов.
- `examples/ch03/` — снимок состояния платы и измерение предела потока.
- `examples/ch05/` — приём в файл, спектр и водопад, FM-демодулятор.
- `examples/ch06/` — генератор тона, AM и FM и передача из циклического буфера.
- `include/lab/`, `src/` — общий код: IQ-файлы с метаданными, ЦОС, графики.
- `tests/` — программные тесты, включая синтетические IIO-контексты 1RX1TX/2RX2TX.
- `vendor/libiio/`, `vendor/libad9361-iio/` — полные исходники выбранных
  libiio и libad9361-iio с upstream-лицензиями.
- `docs/` — PDF пособия, инструкции и рисунки в полном размере (`docs/img/`).
- `build/` — локальные результаты сборки/установки; Git их игнорирует.

Системные пакеты устанавливаются вручную. Скриптов установки пакетов,
изменения репозиториев ОС, автоматической прошивки и offline-комплектов нет.
