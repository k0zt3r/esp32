# ESP32 · проекты и эксперименты

Проекты для **ESP32 DevKit V1 / WROOM**. Проверены на ESP32-D0WD-V3 rev. 3.1, Flash 4 МиБ, Arduino-ESP32 3.3.12.

## Что уже работает

| Проект | Возможности | Проверено |
|---|---|---|
| [SerialTest](SerialTest/SerialTest.ino) | Первый скетч и вывод в Serial | На плате |
| [WiFiLed](WiFiLed/WiFiLed.ino) | Яркость LED, дыхание, Wi-Fi сканер, локальный сайт | На плате |
| [DS18B20Test](DS18B20Test/DS18B20Test.ino) | Поиск датчика, CRC, настоящая температура | На плате |
| [ThermoDashboard](ThermoDashboard/README.md) | Сайт «ТЕРМО», график, CSV, статистика, LED, сканер, автоматический MQTT | На плате и через брокер |
| [SleepLab](experiments/sleep/README.md) | Light-sleep, Deep-sleep, таймеры и RTC-память | На плате; ток не измерен |
| [MQTT-лаборатория](mqtt/README.md) | Брокер Mosquitto, публикация, подписка, веб-панель | Настоящие показания ESP32 каждые 2 секунды |

[Результаты сна](experiments/sleep/RESULTS.md). Измерения тока и отдельный контролируемый тест восстановления Wi-Fi остаются невыполненными.

## Подключение

- DS18B20: питание **3,3 В**, общий GND, DQ → **D4 / GPIO4**.
- Подтяжка **4,7 кОм** между DQ и 3,3 В.
- Встроенный LED: **GPIO2**. OLED не требуется.
- Распиновку проверь по документации именно своего датчика/модуля.

## Подготовка

Нужны Python 3, Arduino CLI, драйвер USB-UART платы. Для локального Mosquitto на Windows нужен 7-Zip. Python-инструменты сборки и MQTT работают без pip-зависимостей.

Все команды ниже выполняются **из корня этого репозитория**:

```powershell
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "OneWire@2.3.8" "DallasTemperature@4.0.6" "PubSubClient@2.8.0"
arduino-cli board list
New-Item -ItemType Directory -Force .esp32
Copy-Item config.example.json .esp32/config.json
```

В `.esp32/config.json` укажи свой порт вместо примерного COM6. FQBN: `esp32:esp32:esp32doit-devkit-v1`.

## Обычные скетчи

```powershell
python tools/board.py flash serial
python tools/board.py flash wifi
python tools/board.py flash temperature
python tools/board.py flash sleep
python tools/board.py monitor
```

Выбирай нужный скетч: каждая прошивка заменяет предыдущую. `build` собирает без записи; `flash` сначала собирает, затем записывает. Serial — 115200 бод, выход — Ctrl+C.

## Термометр с автоматическим MQTT

1. Распакуй официальный Mosquitto в проект:
   `powershell -ExecutionPolicy Bypass -File mqtt/install-mosquitto.ps1`.
2. Найди IPv4 компьютера командой `ipconfig`. Запусти брокер с **собственным** адресом, например:
   `python mqtt/lab.py --lan-ip 192.168.1.110`.
   Оставь терминал работающим. Брокер создаст пароль устройства и приватный `ThermoDashboard/mqtt_private.h` для сборки.
3. В другом терминале выполни `python tools/board.py flash dashboard`.
4. В PowerShell **от администратора** разреши MQTT из локальной подсети, подставив свой IP:
   `powershell -ExecutionPolicy Bypass -File mqtt/allow-device.ps1 -BrokerIP 192.168.1.110`.
   Скрипт ограничивает доступ программой Mosquitto и TCP 1884 из LocalSubnet. Конфликтующие входящие запреты этой программы сохраняет в резервный список и отключает.
5. На телефоне подключись к **ESP32-DevKit**, пароль **esp32test**, открой **http://192.168.4.1/setup**. Введи домашний Wi-Fi 2,4 ГГц и IP брокера. Пароль Wi-Fi сохраняется на плате.
6. На компьютере открой **http://127.0.0.1:8770**. Настоящие показания приходят в `lab/esp32/temperature` с `demo:false`; сообщения формы — отдельные тестовые данные.

Термометр доступен по **http://192.168.4.1** в сети платы и по выданному роутером IP в домашней сети. HTML редактируется в `ThermoDashboard/ui.html`, заголовок страницы генерируется при сборке. Брокер должен работать, пока нужны сообщения.

## Файлы для Git

Исходники, инструкции и результаты экспериментов подготовлены к коммиту. `.gitignore` исключает пароли, приватный MQTT-заголовок, локальную конфигурацию платы, заметки, логи, диагностику Windows, сборки, установщики, кеши и личные TickTick-скрипты. Эти файлы остаются локально; на новом компьютере настройки создаются заново.

Лицензия пока не выбрана. Публикация и настройка GitHub remote выполняются отдельно.


## Навык для работы с проектом

[skills/esp32-lab/SKILL.md](skills/esp32-lab/SKILL.md) — локальный навык с инструкциями подключения, сборки, OLED/Неона, MQTT и экспериментов. Для установки скопируй папку `skills/esp32-lab` в каталог навыков Codex.

## Электрическая цепь: закон Ома

[Зелёный LED и резистор 1 кОм](experiments/ohms-law/README.md): схема, измеренные напряжения и расчёт тока. Прямое измерение тока пока не выполнено.

Полный проект [Неон](https://github.com/k0zt3r/neon) опубликован отдельно; кадры для OLED входят в этот проект.
