# QPSK SDR Lab — учебный код

Практикум на C++17 для **QPSK SDR Lab с обязательными 2 RX и 2 TX**.
Работа с платой — напрямую через **libiio** и **libad9361-iio**, без SoapySDR и UHD.

Сейчас реализованы программы к главам 2 и 5. Остальные главы будут дополняться
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

## Файлы

- `examples/ch02/` — отчёт о компиляторе, генератор тона и инспектор каналов.
- `examples/ch05/` — приём в файл, спектр и водопад, FM-демодулятор.
- `include/lab/`, `src/` — общий код: IQ-файлы с метаданными, ЦОС, графики.
- `tests/` — программные тесты, включая синтетические IIO-контексты 1RX1TX/2RX2TX.
- `vendor/libiio/`, `vendor/libad9361-iio/` — полные исходники выбранных
  libiio и libad9361-iio с upstream-лицензиями.
- `docs/` — PDF и инструкции; не содержит исходников методички.
- `build/` — локальные результаты сборки/установки; Git их игнорирует.

Системные пакеты устанавливаются вручную. Скриптов установки пакетов,
изменения репозиториев ОС, автоматической прошивки и offline-комплектов нет.
