# THA
Tough Hacker Attack

# ToughHA: правила, роли и пример партии

ToughHA - это LAN-игра на 2 игроков про атаку и защиту сервиса. Один игрок играет за hacker-а, второй за defender-а. Игра идет через TCP: сервер поднимает игровую инфраструктуру, defender подключается как администратор, hacker подключается как обычный клиент среди множества похожих фейковых клиентов.

## Как запускать

Один исполняемый файл поддерживает несколько режимов:

```powershell
ToughHA.exe server [port] [--seed N] [--duration seconds] [--bots N]
ToughHA.exe defender <server-ip> [port]
ToughHA.exe hacker <server-ip> [port]
ToughHA.exe sim
```

Пример обычной LAN-партии:

```powershell
# ПК с сервером
ToughHA.exe server 7777 --duration 600 --bots 12

# ПК defender-а
ToughHA.exe defender 192.168.1.10 7777

# ПК hacker-а
ToughHA.exe hacker 192.168.1.10 7777
```

`192.168.1.10` здесь - пример IP компьютера, на котором запущен сервер.

## Можно ли играть: server + defender на одном ПК, hacker на другом

Да, можно. Это нормальная схема.

На первом компьютере запускаются два окна:

```powershell
ToughHA.exe server 7777
ToughHA.exe defender 127.0.0.1 7777
```

На втором компьютере запускается hacker:

```powershell
ToughHA.exe hacker <IP-первого-ПК> 7777
```

Defender в этом случае подключается к серверу локально через `127.0.0.1`, а hacker - по LAN IP первого компьютера. Главное, чтобы firewall Windows разрешал входящие TCP-подключения на выбранный порт, например `7777`.

## Главная идея партии

Сервер генерирует игровой мир:

- C++-скрипт сервиса с логическими ошибками.
- Базу данных с главным секретом.
- Файл правил firewall/service overlay.
- Логи активности.
- Fake clients, которые создают шум.
- Случайные ключи, токены, пароль финального vault-а и параметры уязвимостей.

Defender видит инфраструктуру и активность клиентов. Hacker видит только свою Linux-подобную консоль обычного клиента и должен через пакеты найти цепочку ошибок в сервисе.

## Условия победы

Hacker побеждает, если успевает получить главный секрет и вызвать финальный export:

```text
/core/export password=<final-password>
```

Defender побеждает, если:

- hacker не успел взломать данные до конца таймера;
- defender правильно вычислил hacker-а и забанил его по ID или hardware ID.

Есть дополнительный риск для defender-а: слишком жесткие правила, частые ложные баны и нагрузка ухудшают сервис. Если жалобы доходят до лимита, защита проигрывает из-за collapse/reputation failure.

## Игровой процесс hacker-а

Hacker играет из консоли:

```text
hacker@toughha:~$
```

Он может отправлять обычные и кастомные пакеты:

```powershell
send standard /api/help
send standard /api/profile user_id=5
custom /api/profile user_id=-1&debug=1
burst 30 standard /api/ping client=test
```

Важные команды hacker-а:

```powershell
help
send standard <endpoint> k=v k=v
custom <endpoint> k=v&x=y
burst <count> <standard|custom> <endpoint> k=v
ls tmp
cat tmp/<file>
meta tmp/<file>
rot <text> <shift>
batch <file>
quit
```

Ответы сервера, которые похожи на файлы, сохраняются в настоящую папку:

```text
runtime/hacker_tmp
```

То есть hacker может получить, например, `avatar_raw_....thaimg`, открыть его через `cat tmp/...` или посмотреть метаданные через `meta tmp/...`.

Главная задача hacker-а - не просто спамить сервер, а читать ответы, находить закономерности и собирать exploit-chain:

1. Найти endpoint, который странно реагирует на кастомные данные.
2. Получить файл/ответ с метаданными.
3. Достать первый ключ.
4. Использовать ошибку в auth-валидации.
5. Получить session/route к vault.
6. Получить зашифрованный пароль.
7. Расшифровать его.
8. Забрать crown data через `/core/export`.

## Игровой процесс defender-а

Defender играет из админ-консоли:

```text
defender@toughha:/srv$
```

Он видит активность всех клиентов, но не получает прямой надписи “это hacker”. Среди клиентов есть fake clients, которые тоже иногда ошибаются, отправляют кастомные запросы и создают шум.

В таблицах и логах все клиенты показываются нейтрально: без `BOT`, без `HACK`, без колонки `fake/tcp`. Defender должен вычислять игрока по поведению: цепочка endpoint-ов, количество custom-запросов, ошибки, подозрительность и прогресс.

Важные команды defender-а:

```powershell
help
status
clients
logs 20
inspect <id>
ban <id>
banhw <HWID>
rules
rule add type_guard
rule add media_sanitizer
rule add auth_fullmatch
rule add backup_acl
rule del <rule>
cipher <0-25>
ls /srv
cat /srv/service/generated_service.cpp
cat /srv/db/main.db
cat /srv/config/firewall.rules
cat /srv/logs/activity.log
tail service
events 20
watch on
watch off
quit
```

Defender может читать настоящие файлы сервера через игровую консоль:

```powershell
cat /srv/service/generated_service.cpp
cat /srv/db/main.db
cat /srv/logs/activity.log
```

Физически эти файлы лежат в:

```text
runtime/server
```

## Правила защиты

Defender может включать правила:

```powershell
rule add type_guard
rule add media_sanitizer
rule add auth_fullmatch
rule add backup_acl
```

Что они делают:

| Правило | Эффект |
|---|---|
| `type_guard` | Блокирует опасные profile-запросы с debug/плохим `user_id`. |
| `media_sanitizer` | Чистит метаданные в image-ответах. |
| `auth_fullmatch` | Закрывает prefix-ошибку в `/auth/check`. |
| `backup_acl` | Закрывает обход ACL в `/vault/read`. |

Каждое включенное правило делает сервис медленнее и добавляет жалобы. Это важно: defender не может просто включить все и забыть. Ему нужно балансировать между безопасностью и качеством сервиса.

Дополнительно defender может менять шифрование vault-а:

```powershell
cipher 9
```

Это регенерирует C++-скрипт сервиса и меняет ROT-шифр, которым отдается vault blob. Такая смена тоже добавляет жалобы.

## Fake clients

Fake clients нужны, чтобы defender-у было не скучно и не слишком легко:

- они делают обычные запросы;
- иногда ошибаются;
- иногда отправляют кастомные данные;
- могут создавать burst/noise;
- имеют свои ID, IP и hardware ID;
- могут выглядеть подозрительно.

Если defender банит fake client-а, это считается ложным баном и увеличивает жалобы.

При этом интерфейс не пишет defender-у “это fake”. Если бан оказался ошибочным, игра отвечает как про обычного клиента и добавляет жалобы. Это сделано специально, чтобы бан был решением с риском.

Live-уведомления о запросах fake clients не печатаются поверх ввода по умолчанию. Они складываются в локальный файл `runtime/defender_tmp/events.log` и смотрятся командой:

```powershell
events 20
```

Серверную историю активности можно смотреть командой:

```powershell
logs 20
```

Если нужна старая живая лента, ее можно временно включить:

```powershell
watch on
```

И снова выключить:

```powershell
watch off
```

## Случайные события

Во время партии сервер может создавать события:

- временный сетевой jitter;
- routing storm;
- cache rollback;
- рост жалоб из-за нестабильности;
- частичная потеря прогресса hacker-а при rollback-событии.

Это делает партию менее линейной: hacker иногда вынужден повторять шаг, defender получает дополнительный шум в логах.

## Разбор одной игровой партии

Ниже пример партии, где hacker побеждает. Конкретные ключи, route и параметры каждый раз генерируются случайно, поэтому значения в реальной партии будут другими.

### 1. Старт

Сервер запускается:

```powershell
ToughHA.exe server 7777 --duration 600 --bots 12
```

Сервер генерирует:

```text
runtime/server/service/generated_service.cpp
runtime/server/db/main.db
runtime/server/config/firewall.rules
runtime/server/logs/activity.log
```

Defender подключается:

```powershell
ToughHA.exe defender 127.0.0.1 7777
```

Hacker подключается:

```powershell
ToughHA.exe hacker 127.0.0.1 7777
```

### 2. Defender изучает сервер

Defender смотрит статус:

```powershell
status
clients
logs 10
```

Потом читает сгенерированный C++-сервис:

```powershell
cat /srv/service/generated_service.cpp
```

Он видит, что сервис содержит подозрительные ветки: debug profile, image metadata, prefix auth, backup route. Defender может включить одно правило, например:

```powershell
rule add media_sanitizer
```

Но если он включит слишком много правил, сервис станет медленнее и жалобы начнут расти.

### 3. Hacker исследует API

Hacker начинает с обычного запроса:

```powershell
send standard /api/help
```

Потом проверяет нормальный profile:

```powershell
send standard /api/profile user_id=5
```

Ответ нормальный. Тогда hacker пробует кастомный profile:

```powershell
custom /api/profile user_id=-1&debug=1
```

Если параметры совпали с текущей генерацией мира, сервер возвращает debug dump и сохраняет его как tmp-файл.

Hacker смотрит файлы:

```powershell
ls tmp
cat tmp/profile_dump_....txt
```

Внутри он находит подсказку на `/cdn/image` и специальный формат.

### 4. Hacker получает метаданные

Hacker отправляет кастомный image-запрос:

```powershell
custom /cdn/image asset=avatar&format=raw_frame&width=0
```

Сервер возвращает файл:

```text
avatar_raw_....thaimg
```

Hacker читает метаданные:

```powershell
meta tmp/avatar_raw_....thaimg
```

Внутри находится первый ключ:

```text
EXIF-Comment=stage1=K1-...
EXIF-Auth=/auth/check
EXIF-Bug=token prefix accepts overflow suffix
```

### 5. Defender видит подозрительность

В это время defender смотрит логи:

```powershell
logs 30
clients
inspect <id>
```

Он видит, что один клиент часто использует `custom`, получает ошибки и ходит в странные endpoint-ы. Но fake clients тоже ошибаются, поэтому банить сразу рискованно.

Если defender уверен, он может сделать:

```powershell
ban <id>
```

Если ID принадлежит hacker-у, defender сразу выигрывает. Если это fake client, растут жалобы.

### 6. Hacker ломает auth

Hacker использует prefix-ошибку:

```powershell
custom /auth/check token=K1-ABCDEF::overflow
```

Если defender не включил `auth_fullmatch`, сервер возвращает session и backup route:

```text
session=S2-...
route=backup_...
next=/vault/read
```

Hacker сохраняет/читает этот tmp-файл:

```powershell
cat tmp/auth_session_....tmp
```

### 7. Hacker читает vault

Hacker вызывает:

```powershell
custom /vault/read session=S2-...&route=backup_...
```

Сервер возвращает encrypted vault blob:

```text
cipher=rot9
data=AXXC-...
hint=decrypt with negative shift
```

Hacker расшифровывает:

```powershell
rot AXXC-... -9
```

Получает финальный пароль:

```text
ROOT-...
```

### 8. Финальный export

Hacker отправляет:

```powershell
custom /core/export password=ROOT-...
```

Если пароль правильный, сервер возвращает crown data:

```text
flag=TOUGHHA{ROOT-...}
status=hacker_win
```

Игра завершается победой hacker-а.

## Как defender мог остановить эту партию

Defender мог победить несколькими способами:

1. Найти hacker-а по telemetry:

```powershell
clients
inspect <id>
ban <id>
```

2. Закрыть конкретный слой атаки:

```powershell
rule add media_sanitizer
```

Это помешало бы hacker-у получить stage1 из image metadata.

3. Закрыть auth-ошибку:

```powershell
rule add auth_fullmatch
```

Это остановило бы prefix overflow на `/auth/check`.

4. Закрыть vault route:

```powershell
rule add backup_acl
```

Это остановило бы чтение `/vault/read`.

Но defender должен учитывать цену защиты: каждое правило увеличивает latency и жалобы. Если включить все слишком рано и еще ошибочно банить fake clients, можно проиграть по качеству сервиса.

## Рекомендуемая стратегия

Для hacker-а:

- сначала использовать `/api/help`;
- читать каждый tmp-файл;
- проверять `meta`;
- не спамить слишком рано, иначе defender быстрее заметит;
- использовать `burst` как маскировку или давление, но осторожно;
- помнить, что ROT можно расшифровать командой `rot`.

Для defender-а:

- сразу открыть `clients`, `logs`, `cat /srv/service/generated_service.cpp`;
- не включать все правила без причины;
- смотреть не только ошибки, но и последовательность endpoint-ов;
- fake client обычно ошибается хаотично, hacker движется по цепочке;
- банить только после `inspect`;
- менять `cipher`, если есть признаки доступа к vault;
- закрывать именно тот слой, к которому hacker уже близко.

## Локальный автотест

Для проверки всей игры есть режим:

```powershell
ToughHA.exe sim
```

Он локально поднимает сервер, подключает defender-а и hacker-а через TCP, проверяет чтение файлов, правила защиты, смену шифра, fake clients, burst-пакеты, tmp-файлы, exploit-chain и завершение партии.

# ToughHA: команды, пакеты и функции игроков

Этот файл объясняет игру проще и практичнее, чем общий файл правил. Здесь разобрано, что именно вводить в консоль, что такое endpoint, зачем нужны `=`, какие параметры бывают в пакетах и как играть за hacker-а и defender-а.

## Самое важное за 1 минуту

В игре hacker не нажимает кнопки в меню. Он отправляет пакеты на сервер.

Пакет состоит из трех вещей:

```text
mode + endpoint + payload
```

Пример:

```powershell
custom /api/profile user_id=-1&debug=1
```

Что здесь что:

```text
custom              режим пакета: кастомный
/api/profile        endpoint: куда на сервере отправляем пакет
user_id=-1&debug=1  payload: данные внутри пакета
```

Endpoint - это адрес функции на сервере. Например `/api/profile` означает: “отправить пакет в функцию профиля пользователя”.

`key=value` - это параметр. Слева имя, справа значение.

```text
user_id=5
```

Значит:

```text
имя параметра: user_id
значение: 5
```

Если параметров несколько, они разделяются символом `&`:

```text
user_id=5&debug=1&format=raw_frame
```

Это значит:

```text
user_id = 5
debug = 1
format = raw_frame
```

## Обычный пакет и кастомный пакет

Есть два основных режима:

```text
standard
custom
```

`standard` - обычный пакет, который похож на нормальное поведение обычного клиента.

Пример:

```powershell
send standard /api/profile user_id=5
```

`custom` - пакет, который hacker собирает сам. Через него можно отправлять странные значения, лишние параметры, неправильные типы данных и искать ошибки в логике сервиса.

Пример:

```powershell
custom /api/profile user_id=-1&debug=1
```

Важно: custom-пакеты часто повышают подозрительность. Defender видит, что клиент отправляет странные запросы.

## Как писать payload

Payload можно писать двумя способами.

Способ 1: через `&`.

```powershell
custom /cdn/image asset=avatar&format=raw_frame&width=0
```

Способ 2: через пробелы в команде `send`.

```powershell
send standard /cdn/image asset=logo format=jpg width=128
```

Внутри игра превратит это примерно в:

```text
asset=logo&format=jpg&width=128
```

То есть эти две идеи похожи:

```powershell
send standard /api/profile user_id=5
custom /api/profile user_id=5
```

Разница в режиме: первый пакет выглядит обычным, второй - кастомным.

## Команды hacker-а

Консоль hacker-а выглядит примерно так:

```text
hacker@toughha:~$
```

### `help`

Показывает базовые команды hacker-а.

```powershell
help
```

### `send standard <endpoint> <payload>`

Отправляет обычный пакет.

Пример:

```powershell
send standard /api/help
```

Пример с параметром:

```powershell
send standard /api/profile user_id=5
```

Пример с несколькими параметрами:

```powershell
send standard /cdn/image asset=logo format=jpg width=128
```

Когда использовать: чтобы вести себя как обычный клиент и изучать нормальные ответы сервера.

### `send custom <endpoint> <payload>`

Отправляет кастомный пакет через полную форму команды.

```powershell
send custom /api/profile user_id=-1 debug=1
```

Можно также писать так:

```powershell
custom /api/profile user_id=-1&debug=1
```

### `custom <endpoint> <payload>`

Короткая команда для кастомного пакета.

```powershell
custom /api/profile user_id=-1&debug=1
```

Когда использовать: чтобы искать уязвимости.

Примеры исследований:

```powershell
custom /api/profile user_id=abc
custom /api/profile user_id=-1
custom /api/profile user_id=-1&debug=1
custom /cdn/image asset=avatar&format=raw_frame&width=0
custom /auth/check token=test
```

Если сервер отвечает `ERR wrong type`, значит тип данных неправильный. Например `user_id=abc` плохой, потому что `user_id` должен быть числом.

### `std <endpoint> <payload>`

Короткая форма обычного standard-пакета.

```powershell
std /api/profile user_id=5
```

То же самое, что:

```powershell
send standard /api/profile user_id=5
```

### `burst <count> <standard|custom> <endpoint> <payload>`

Отправляет сразу много пакетов.

```powershell
burst 20 standard /api/ping client=test
```

Это отправит 20 обычных ping-пакетов.

Можно отправлять custom-burst:

```powershell
burst 10 custom /api/profile user_id=-1&debug=1
```

Когда использовать:

- для нагрузки;
- для маскировки среди шума;
- для проверки поведения сервера при повторных запросах.

Минус: defender увидит много запросов.

### `ls tmp`

Показывает файлы, которые hacker получил от сервера.

```powershell
ls tmp
```

Физически файлы лежат в:

```text
runtime/hacker_tmp
```

### `cat tmp/<file>`

Открывает полученный tmp-файл прямо в консоли.

```powershell
cat tmp/avatar_raw_12345.thaimg
```

Так hacker читает ответы, дампы, vault blob и другие данные.

### `meta tmp/<file>`

Показывает строки, похожие на метаданные:

```powershell
meta tmp/avatar_raw_12345.thaimg
```

Команда ищет строки с:

```text
EXIF
META
cipher=
session=
route=
data=
```

Это удобно, если файл большой и нужно быстро найти секретную строку.

### `inbox`

Показывает список файлов, которые были сохранены из ответов сервера.

```powershell
inbox
```

### `events [n]`

Показывает последние локально сохраненные уведомления, которые раньше печатались прямо поверх ввода.

```powershell
events
events 30
```

Теперь live-уведомления по умолчанию не мешают печатать команды. Они сохраняются в отдельный файл:

```text
runtime/hacker_tmp/events.log
runtime/defender_tmp/events.log
```

Для defender-а это особенно важно: постоянные сообщения фоновых клиентов и запросах больше не перебивают строку ввода. Смотреть их можно через:

```powershell
events 20
```

А серверную ленту активности defender всё еще может смотреть так:

```powershell
logs 20
```

### `events clear`

Очищает локальный буфер уведомлений.

```powershell
events clear
```

### `watch on|off`

Включает или выключает старое поведение, когда уведомления печатаются сразу в консоль.

```powershell
watch on
watch off
```

По умолчанию стоит:

```text
watch off
```

### `rot <text> <shift>`

ROT-шифр. Нужен для расшифровки vault blob.

Пример:

```powershell
rot URYYB -13
```

Если в файле написано:

```text
cipher=rot9
data=AXXC-VRN...
```

то расшифровать можно так:

```powershell
rot AXXC-VRN... -9
```

То есть если шифр `rot9`, для расшифровки нужен сдвиг `-9`.

### `batch <file>`

Запускает локальный файл с командами, почти как простой batch-скрипт.

```powershell
batch attack.txt
```

Пример содержимого `attack.txt`:

```text
send standard /api/help
sleep 300
send standard /api/profile user_id=5
sleep 300
custom /api/profile user_id=-1&debug=1
```

Строки `sleep 300` делают паузу в миллисекундах.

Комментарии можно писать так:

```text
# comment
rem comment
```

### `quit`

Выход из клиента.

```powershell
quit
```

## Endpoint-ы сервера

Endpoint - это “функция” на сервере. Hacker отправляет пакет в endpoint, а сервис решает, что вернуть.

## `/api/help`

Публичная справка API.

Команда:

```powershell
send standard /api/help
```

Payload не нужен.

Что дает: список известных endpoint-ов и примерные параметры.

## `/api/ping`

Проверка связи.

Команда:

```powershell
send standard /api/ping client=my-test
```

Параметры:

```text
client=<любое имя>
```

Ожидаемый смысл ответа:

```text
OK pong
```

Это безопасный endpoint. Через него удобно проверять, что соединение живое.

## `/api/profile`

Профиль пользователя.

Нормальный запрос:

```powershell
send standard /api/profile user_id=5
```

Параметры:

```text
user_id=<целое число>
```

Примеры:

```powershell
send standard /api/profile user_id=1
send standard /api/profile user_id=25
custom /api/profile user_id=-1
custom /api/profile user_id=abc
```

Если отправить:

```powershell
custom /api/profile user_id=abc
```

сервер может ответить:

```text
ERR wrong type: user_id must be int
```

Это значит, что `user_id` должен быть числом.

Возможная уязвимость: debug-ветка. Иногда сервис генерируется так, что особый `user_id` и особый debug-параметр возвращают dump.

Пример идеи:

```powershell
custom /api/profile user_id=-1&debug=1
```

Но конкретные значения генерируются случайно. Это может быть не `debug=1`, а например:

```text
trace=true
diag=dump
verbose=full
```

И `user_id` тоже может быть другим:

```text
-1
-7
-404
00000000000000000000
```

Hacker должен подобрать или вывести это по поведению сервиса. Defender может увидеть точные значения в:

```powershell
cat /srv/service/generated_service.cpp
```

## `/cdn/image`

Получение картинки/медиа-файла.

Нормальный запрос:

```powershell
send standard /cdn/image asset=logo format=jpg width=128
```

Параметры:

```text
asset=<имя картинки>
format=<формат>
width=<число>
```

Примеры:

```powershell
send standard /cdn/image asset=logo format=jpg width=128
custom /cdn/image asset=avatar&format=raw_frame&width=0
custom /cdn/image asset=avatar&format=meta_raw&width=0
```

Возможная уязвимость: если подобрать специальный `format` и `width=0`, сервер может вернуть файл с метаданными.

После ответа:

```powershell
ls tmp
meta tmp/avatar_raw_....thaimg
cat tmp/avatar_raw_....thaimg
```

В метаданных можно найти первый ключ:

```text
stage1=K1-...
```

## `/auth/check`

Проверка токена.

Обычная форма:

```powershell
custom /auth/check token=K1-....
```

Параметры:

```text
token=<токен>
```

Возможная уязвимость: prefix-сравнение. Это значит, что сервис может проверять не весь токен, а только начало.

Если в файле найдено:

```text
stage1=K1-ABCDEF123456
```

можно проверить идею:

```powershell
custom /auth/check token=K1-ABCDE::overflow
```

В текущей реализации обычно используется первые 8 символов stage1-ключа плюс лишний хвост:

```powershell
custom /auth/check token=<первые-8-символов-stage1>::overflow
```

Если уязвимость сработала, ответ даст:

```text
session=S2-...
route=backup_...
next=/vault/read
```

Потом:

```powershell
cat tmp/auth_session_....tmp
```

## `/vault/read`

Чтение vault blob.

Команда:

```powershell
custom /vault/read session=S2-...&route=backup_...
```

Параметры:

```text
session=<сессия из /auth/check>
route=<route из /auth/check>
```

Если все правильно, сервер вернет зашифрованный blob:

```text
cipher=rot9
data=...
hint=decrypt with negative shift
```

Расшифровка:

```powershell
rot <data> -9
```

Если cipher другой, например `rot4`, значит:

```powershell
rot <data> -4
```

## `/core/export`

Финальный endpoint.

Команда:

```powershell
custom /core/export password=ROOT-...
```

Параметры:

```text
password=<расшифрованный пароль>
```

Если пароль правильный, hacker получает crown data и выигрывает.

## Как начать играть за hacker-а: простой маршрут

Это учебный маршрут. В реальной партии defender может закрывать правилами отдельные шаги.

### Шаг 1. Проверить связь

```powershell
send standard /api/ping client=hacker
```

Если есть `OK pong`, соединение работает.

### Шаг 2. Посмотреть публичный API

```powershell
send standard /api/help
```

Ты узнаешь, какие endpoint-ы вообще существуют.

### Шаг 3. Проверить обычный profile

```powershell
send standard /api/profile user_id=5
```

Это нормальный запрос. Он нужен, чтобы понять обычный ответ.

### Шаг 4. Искать странное поведение profile

Проверяй разные значения:

```powershell
custom /api/profile user_id=abc
custom /api/profile user_id=-1
custom /api/profile user_id=-7
custom /api/profile user_id=-404
custom /api/profile user_id=-1&debug=1
custom /api/profile user_id=-7&debug=1
custom /api/profile user_id=-404&debug=1
custom /api/profile user_id=-1&trace=true
custom /api/profile user_id=-1&diag=dump
custom /api/profile user_id=-1&verbose=full
```

Цель - получить не просто `ERR`, а файл или dump с подсказкой.

### Шаг 5. Читать tmp-файлы

После интересного ответа:

```powershell
ls tmp
cat tmp/<имя-файла>
```

Если файл похож на картинку или blob:

```powershell
meta tmp/<имя-файла>
```

### Шаг 6. Использовать подсказку на image

Если dump говорит попробовать `/cdn/image`, отправляй:

```powershell
custom /cdn/image asset=avatar&format=<format-из-dump>&width=0
```

Потом:

```powershell
ls tmp
meta tmp/<image-file>
```

Ищи:

```text
stage1=...
```

### Шаг 7. Проверить auth

Если stage1 выглядит так:

```text
K1-ABCDEF123456
```

пробуй первые 8 символов и хвост:

```powershell
custom /auth/check token=K1-ABCDE::overflow
```

Если ответ содержит `session=` и `route=`, ты прошел следующий слой.

### Шаг 8. Прочитать vault

```powershell
custom /vault/read session=<session>&route=<route>
```

Потом:

```powershell
cat tmp/<vault-file>
```

Найди:

```text
cipher=rotN
data=...
```

### Шаг 9. Расшифровать пароль

Если написано:

```text
cipher=rot9
data=ABC...
```

пиши:

```powershell
rot ABC... -9
```

Результат должен быть похож на:

```text
ROOT-...
```

### Шаг 10. Забрать финальные данные

```powershell
custom /core/export password=ROOT-...
```

Если пароль правильный, игра закончится победой hacker-а.

## Команды defender-а

Консоль defender-а выглядит примерно так:

```text
defender@toughha:/srv$
```

Defender - это администратор сервера. Он видит файлы, логи и клиентскую телеметрию, но не получает прямой подписи, где hacker, а где фоновый клиент.

### `help`

Показывает команды defender-а.

```powershell
help
```

### `status`

Показывает состояние партии.

```powershell
status
```

Примерные поля:

```text
time_left=542s
complaints=12/100
rules_on=2
cipher_shift=9
visible_clients=13
background_traffic=enabled
game_over=no
```

Что важно:

- `time_left` - сколько времени осталось defender-у продержаться;
- `complaints` - жалобы, нельзя довести до 100;
- `rules_on` - сколько правил включено;
- `cipher_shift` - текущий ROT-сдвиг vault-а.

### `clients`

Показывает клиентскую телеметрию. В таблице намеренно нет колонки `tcp/fake` и нет HWID вида `BOT`/`HACK`, чтобы defender искал hacker-а по поведению, а не по подсказке интерфейса.

```powershell
clients
```

Колонки:

```text
ID      actor id
NET     нейтральный сетевой токен клиента
HWID    нейтральный hardware-токен клиента
REQ     сколько запросов
ERR     сколько ошибок
CUS     сколько custom-пакетов
SUS     suspicion score
PROG    прогресс по exploit-chain
LAST    сколько секунд назад была активность
```

На что смотреть:

- много `CUS` - клиент часто отправляет custom-пакеты;
- много `ERR` - клиент тыкает в ошибки;
- высокий `SUS` - клиент подозрительный;
- высокий `PROG` - клиент уже продвинулся по цепочке взлома.

Важно: фоновые клиенты тоже могут ошибаться, поэтому нельзя банить только по одной ошибке.

### `logs [n]`

Показывает последние события.

```powershell
logs 20
```

Пример строки:

```text
[12:30:41] id=101 ip=127.0.0.1 hw=HAC...W01 mode=custom ep=/cdn/image status=OK sus+10 :: image metadata leak
```

Что читать:

- `id=101` - кого смотреть через `inspect`;
- `mode=custom` - кастомный пакет;
- `ep=/cdn/image` - endpoint;
- `sus+10` - насколько подозрительно;
- `image metadata leak` - что произошло.

Важно: обычная live-лента больше не печатается поверх ввода. Если хочешь посмотреть локально накопленные уведомления, используй:

```powershell
events 20
```

Если хочешь временно вернуть живую печать:

```powershell
watch on
```

Потом лучше снова выключить:

```powershell
watch off
```

### `inspect <id>`

Подробно показывает одного клиента.

```powershell
inspect 101
```

Используй перед баном.

### `ban <id>`

Банит actor-а по ID.

```powershell
ban 101
```

Если это hacker, defender сразу побеждает. Если это обычный фоновый клиент, это ложный бан и жалобы сильно растут.

### `banhw <HWID>`

Банит по hardware ID.

```powershell
banhw HW-7A91C2EF
```

Это полезно, если defender хочет банить не просто соединение, а конкретное “железо”. Используй HWID из `clients` или `inspect`; это публичный игровой токен, а не настоящий серийный номер компьютера.

### `rules`

Показывает текущие правила защиты.

```powershell
rules
```

Пример:

```text
type_guard=off
media_sanitizer=on
auth_fullmatch=off
backup_acl=off
cipher_shift=9
latency_penalty_ms=45
```

### `rule add <rule>`

Включает защитное правило.

```powershell
rule add type_guard
rule add media_sanitizer
rule add auth_fullmatch
rule add backup_acl
```

Правила:

```text
type_guard       закрывает странный debug/profile слой
media_sanitizer  чистит метаданные image-файлов
auth_fullmatch   закрывает prefix-ошибку в auth
backup_acl       закрывает обход vault через backup route
```

Цена: правило добавляет задержку и жалобы.

### `rule del <rule>`

Выключает правило.

```powershell
rule del media_sanitizer
```

Используй, если сервис стал слишком медленным или жалобы растут.

### `cipher <0-25>`

Меняет ROT-шифр vault-а.

```powershell
cipher 12
```

Это регенерирует service script. Может сбить hacker-а, если он уже получил старый vault blob, но тоже добавляет жалобы.

### `ls <path>`

Список файлов на сервере через виртуальную Linux-подобную файловую систему.

```powershell
ls /srv
ls /srv/service
ls /srv/db
ls /srv/config
ls /srv/logs
```

### `cat <path>`

Читает файл сервера.

```powershell
cat /srv/service/generated_service.cpp
cat /srv/db/main.db
cat /srv/config/firewall.rules
cat /srv/logs/activity.log
cat /srv/public/readme.txt
```

Самый важный файл:

```powershell
cat /srv/service/generated_service.cpp
```

В нем defender видит сгенерированную C++-логику сервиса и может понять, какие ошибки hacker пытается использовать.

### `tail service`

Показывает конец service script.

```powershell
tail service
```

Удобно, если весь файл слишком длинный.

### `batch <file>`

Как и у hacker-а, можно выполнять локальный файл команд.

```powershell
batch defend.txt
```

Пример `defend.txt`:

```text
status
logs 20
clients
sleep 500
rules
```

### `quit`

Выход.

```powershell
quit
```

## Как начать играть за defender-а

### Шаг 1. Проверить статус

```powershell
status
```

Смотри таймер и жалобы.

### Шаг 2. Открыть сервис

```powershell
cat /srv/service/generated_service.cpp
```

Ищи странные места:

```text
kDebugFlagName
kDebugFlagValue
kBadUserId
kImageMagic
kBackupRoute
kStage1Key
kStage2Token
kVaultPassword
kCipherShift
```

Это то, вокруг чего строится атака.

### Шаг 3. Смотреть клиентов

```powershell
clients
logs 20
```

Ищи клиента, который:

- часто использует `custom`;
- ходит по цепочке `/api/profile -> /cdn/image -> /auth/check -> /vault/read`;
- получает `metadata leak`, `auth prefix leak`, `vault encrypted password leak`;
- имеет растущий `PROG`.

### Шаг 4. Проверять подозреваемого

```powershell
inspect <id>
```

Если видно, что это настоящий hacker, бань:

```powershell
ban <id>
```

### Шаг 5. Закрывать конкретные слои

Если hacker ковыряет `/api/profile`:

```powershell
rule add type_guard
```

Если hacker дошел до image metadata:

```powershell
rule add media_sanitizer
```

Если hacker пробует auth tokens:

```powershell
rule add auth_fullmatch
```

Если hacker дошел до vault:

```powershell
rule add backup_acl
```

Если hacker уже получил encrypted blob:

```powershell
cipher 17
```

Но помни: каждое правило ухудшает сервис. Defender играет не только в “закрыть все”, но и в баланс.

## Почему hacker иногда получает ERR

`ERR` - это не всегда плохо. Ошибка тоже дает информацию.

Примеры:

```text
ERR wrong type: user_id must be int
```

Вывод: `user_id` должен быть числом.

```text
ERR format and width are required
```

Вывод: `/cdn/image` требует `format` и `width`.

```text
DENY auth_fullmatch: token must match exactly
```

Вывод: defender включил правило `auth_fullmatch`, prefix-баг закрыт.

```text
DENY backup_acl: backup route requires admin console
```

Вывод: defender закрыл vault route.

## Мини-шпаргалка hacker-а

```powershell
send standard /api/help
send standard /api/ping client=test
send standard /api/profile user_id=5
custom /api/profile user_id=-1&debug=1
custom /cdn/image asset=avatar&format=raw_frame&width=0
ls tmp
meta tmp/<file>
cat tmp/<file>
custom /auth/check token=<stage1-prefix>::overflow
custom /vault/read session=<session>&route=<route>
rot <encrypted-data> -<cipher-number>
custom /core/export password=<decrypted-password>
```

## Мини-шпаргалка defender-а

```powershell
status
cat /srv/service/generated_service.cpp
clients
logs 30
inspect <id>
rule add media_sanitizer
rule add auth_fullmatch
cipher 11
ban <id>
```

## Что делать, если вообще непонятно

Для обучения запусти локальную симуляцию:

```powershell
ToughHA.exe sim
```

Она сама проходит партию и проверяет, что механики работают.

Для ручной тренировки на одном ПК можно открыть три окна:

```powershell
ToughHA.exe server 7777 --duration 600 --bots 8
ToughHA.exe defender 127.0.0.1 7777
ToughHA.exe hacker 127.0.0.1 7777
```

Сначала играй за hacker-а по учебному маршруту из этого файла. Потом открой defender-а и посмотри, как твои действия выглядели в `logs` и `clients`.
