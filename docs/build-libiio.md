# Сборка libiio, libad9361-iio и программ главы 2

## 1. Системные инструменты

libiio — клиентская библиотека доступа к Linux IIO, а не прошивка платы.
IIO-драйвер AD936x и сервер iiod работают внутри QPSK SDR Lab. На ПК нужны
компилятор, libiio и библиотеки выбранного транспорта. Поэтому «только libiio»
означает отсутствие дополнительной SDR-обёртки, а не отсутствие libc/libusb/libxml2.

RED OS 8 (проверяйте наличие пакетов в своих репозиториях):

```bash
dnf info git cmake gcc gcc-c++ make pkgconf-pkg-config libusb1-devel libxml2-devel usbutils
sudo dnf install git cmake gcc gcc-c++ make pkgconf-pkg-config libusb1-devel libxml2-devel usbutils
```

Debian/Ubuntu — соответствующие роли пакетов:

```bash
sudo apt update
sudo apt install git cmake build-essential pkg-config libusb-1.0-0-dev libxml2-dev usbutils
```

Если пакет не найден, ищите его эквивалент в документации своего дистрибутива.
Не подключайте случайные репозитории и не собирайте проект через sudo.
FFTW/OpenCV/ZeroMQ понадобятся по мере появления использующего их кода, не для
программ этой главы.

## 2. Локальные libiio v0.26 и libad9361-iio v0.3

Все команды выполняются из корня `LETI_MIR_accelerator`:

```bash
cmake -S vendor/libiio -B build/iio \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/iio-install" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_RPATH="$PWD/build/iio-install/lib" \
  -DWITH_LOCAL_BACKEND=OFF -DWITH_IIOD=OFF \
  -DWITH_NETWORK_BACKEND=ON -DWITH_USB_BACKEND=ON \
  -DWITH_XML_BACKEND=ON -DHAVE_DNS_SD=OFF \
  -DWITH_TESTS=ON -DWITH_EXAMPLES=OFF \
  -DWITH_DOC=OFF -DWITH_MAN=OFF \
  -DPYTHON_BINDINGS=OFF -DINSTALL_UDEV_RULE=OFF
cmake --build build/iio --parallel
cmake --install build/iio
```

В libiio флаг `WITH_TESTS=ON` также включает диагностические утилиты
`iio_info`, `iio_attr` и т. п. Сервер iiod на ПК не собирается — он уже должен
работать на плате. Отключён автоматический поиск сетевых устройств через
DNS-SD; для сети указываем IP явно, для USB доступно сканирование.
XML-backend используется в тестах без оборудования. Установка udev-правила
намеренно вынесена в отдельную ручную операцию. `CMAKE_INSTALL_RPATH`
записывает путь к локальной библиотеке в установленные утилиты, поэтому
`iio_info` не подхватит другую libiio, установленную в системе.

libad9361-iio — надстройка Analog Devices над libiio для трансиверов AD936x.
Она рассчитывает и загружает согласованные FIR-фильтры и тактирование при
смене частоты дискретизации (`ad9361_set_bb_rate()`); без такого фильтра
трансивер не работает ниже примерно 2,08 Мвыб/с. Собираем её против только что
установленной libiio, указывая пути явно, чтобы CMake не нашёл системную копию:

```bash
cmake -S vendor/libad9361-iio -B build/ad9361 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/iio-install" \
  -DCMAKE_INSTALL_RPATH="$PWD/build/iio-install/lib" \
  -DLIBIIO_INCLUDEDIR="$PWD/build/iio-install/include" \
  -DLIBIIO_LIBRARIES="$PWD/build/iio-install/lib/libiio.so" \
  -DWITH_DOC=OFF -DENABLE_PACKAGING=OFF
cmake --build build/ad9361 --parallel
cmake --install build/ad9361
```

При CMake 4, если он отклоняет старый minimum policy version upstream, добавьте
к обеим командам конфигурирования `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (без
редактирования vendor). Каноническая проверка выполнена с CMake 3.30.6.

## 3. Инспектор каналов

```bash
cmake -S . -B build/ch02-iio \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLAB_WITH_IIO=ON \
  -DLAB_IIO_PREFIX="$PWD/build/iio-install"
cmake --build build/ch02-iio --parallel
ctest --test-dir build/ch02-iio --output-on-failure
```

CMake ищет `libiio` и `libad9361` только в `LAB_IIO_PREFIX` и останавливается с
ошибкой, если их там нет. Ожидается 9 тестов. Три аппаратно-независимых теста
проверяют распознавание синтетической разметки 2RX2TX, отказ для 1RX1TX и
отсутствие TX. Это **не** результаты работы настоящей платы. При смене префикса
библиотеки выбирайте новый каталог сборки: CMake сохраняет найденные пути в кэше.

## 4. Явный выбор библиотеки при запуске

```bash
LD_LIBRARY_PATH="$PWD/build/iio-install/lib" \
  ldd ./build/ch02-iio/qpsk_channels
ldd build/iio-install/lib/libad9361.so.0
LD_LIBRARY_PATH="$PWD/build/iio-install/lib" \
  ./build/iio-install/bin/iio_info --version
LD_LIBRARY_PATH="$PWD/build/iio-install/lib" \
  ./build/iio-install/bin/iio_info -s
LD_LIBRARY_PATH="$PWD/build/iio-install/lib" \
  ./build/ch02-iio/qpsk_channels ip:192.168.2.1
```

В обоих выводах `ldd` строка `libiio.so.0` должна указывать на
`build/iio-install/lib`. `LD_LIBRARY_PATH` — переменная окружения со списком
каталогов, в которых загрузчик ищет `.so` раньше системных. Путь к локальным
библиотекам и так записан в программы (RUNPATH), но `LD_LIBRARY_PATH` имеет
более высокий приоритет: если она уже задана в `.bashrc` и указывает на другую
libiio, будет загружена та. Явное значение перед командой действует на один
запуск и исключает такую подмену; `.bashrc` и `/etc/ld.so.conf.d` не меняются.
Это локальный набор SDR-зависимостей, не контейнер: системные libusb, libxml2,
libc и компилятор остаются общими.

## Проверено авторами

В RED OS 8 проверяются сборка и программные тесты.

Подключение и одновременный
поток 2RX/2TX считаются непроверенными, пока не проведён отдельный опыт с платой.
Имена Debian/Ubuntu-пакетов приведены для соответствия; чистая установка на
Ubuntu в рамках этой проверки не выполнялась.
