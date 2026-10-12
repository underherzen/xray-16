# ARCHITECTURE.md — Архитектурное руководство порта OpenXRay → WebAssembly

> Документ-источник технических решений. Роадмап этапов — в [PLAN.md](PLAN.md).
> База: OpenXRay/xray-16 `dev@3917383bd`, ветка `wasm-port` (github.com/underherzen/xray-16).
> Все пути к коду — относительно корня репо `~/dev/xray-16` (WSL).
> Составлено 2026-10-11 по результатам ревизии кодовой базы (три аудита: сборка, платформа, Lua).

---

## 0. Резюме для принимающего решения

| Вопрос | Ответ |
|---|---|
| Реалистичен ли порт? | Да. Три самые тяжёлые части уже подготовлены: рендер (WebGL2-заготовка в апстриме), Lua-замена (встроенная опция), линковка (всё статически) |
| Что самое тяжёлое осталось | Виртуальная ФС (стриминг 11 ГБ по HTTP) и производительность Lua-интерпретатора |
| Первая задача | Ветка платформы `__EMSCRIPTEN__` в `Platform.hpp` + CMake-тулчейн |
| Ключевое ограничение | wasm32: жёсткий потолок 4 ГБ памяти |
| Стратегия потоков | Фаза 1 — однопоточно (движок это позволяет), фаза 2 — pthreads |
| Стратегия синхронного IO | **Факт (M3–M5):** sync-XHR на главном потоке из FS-бэкенда XRFS (без ASYNCIFY). Цель: движок в Worker (`PROXY_TO_PTHREAD` + OffscreenCanvas), где блокирующий IO легален и UI не замерзает |

---

## 1. Границы проекта

**Цель:** xr_3da (CoP) запускается в браузере Chrome/Firefox с локального HTTP-сервера, загружает уровень, играбельно.

**Не-цели (явно):**
- мультиплеер (GameSpy/master-server — вырезаем заглушками)
- мобильные браузеры / тач-управление
- публикация ассетов в сети (только локальная разработка, лицензия GSC)
- SoC/Clear Sky (ветка только CoP; движок поддерживает CS, но не тестируем)

---

## 2. Что уже сделано за нас (находки аудита)

### 2.1 WebGL2-заготовка в апстреме (коммит `985a70116`, окт 2026, в `dev`)
- `#ifdef XR_PLATFORM_WEB` в ~32 файлах рендерера
- Создание WebGL2-контекста: `src/Layers/xrRenderGL/glHW.cpp:96-129` (`emscripten_webgl_create_context("#canvas")`, gladLoadGLES2, расширение `WEBGL_draw_instanced_base_vertex_base_instance`)
- ESSL-шейдеры: `res/gamedata/shaders/gl/*` + совместимость `src/Layers/xrRenderPC_GL/rgl_essl_web.h` (338 строк)
- Линтер шейдеров: `misc/web/tools/essl_check.{cpp,sh}` + `essl_report.py` (glslangValidator по всем gl-шейдерам)
- Fallback'и: occlusion query (`QueryHelper.h:67`), sync point (`r__sync_point.cpp:20`), частичный unmap буферов (`R_DStreams.cpp`), VAO-эмуляция (`BufferUtils.h:180-227`)

**Чего НЕ хватает:** макрос `XR_PLATFORM_WEB` никем не определяется; CMake не знает про Emscripten; CI-сборки нет. Это наша работа.

### 2.2 Замена LuaJIT встроена (коммит `6948f58d2`)
- `option(XRAY_USE_LUAJIT ON)` в корневом `CMakeLists.txt:21`; OFF → `find_package(Lua51)` (`Externals/CMakeLists.txt:1-4`)
- `#if XRAY_USE_LUAJIT` гварды уже стоят в xrScriptEngine (bit/ffi/jit-либы, escape-секвенции, профайлер)
- Fallback'и `bit_and/or/xor/not` на C уже в движке (`src/xrScriptEngine/ScriptEngineScript.cpp:75-90`)
- **Единственный** незагuardенный compile-break: `src/xrGame/Level.cpp:601` — `LUA_GCTIMEOUT` (форк-специфичная константа)
- luabind (deboostified 0.9) с чистым Lua 5.1 совместим

### 2.3 Модель линковки благоприятна
- `xr_3da` линкует `xrCore xrAPI xrEngine xrRender_GL xrGame` **напрямую** (`src/xr_3da/CMakeLists.txt:27-34`), dlopen только для опциональных тулз (renderdoc и т.п. — fail-soft)
- Полностью статическая конфигурация (`BUILD_SHARED_LIBS=OFF`) гоняется в их CI (AppImage)
- Рендерер выбирается из compile-time массива модулей (`src/xr_3da/entry_point.cpp:24-30`) — в wasm уходим в один модуль без переделок

---

## 3. Карта движка (что чем является)

```
xr_3da (exe) ── entry_point.cpp: main() → CApplication::Run()
 ├── xrCore      — память, FS (CLocatorAPI), TaskManager, LZO/PPMd, XML, crypto
 ├── xrAPI       — GEnv: глобальная таблица функций (инверсия зависимостей)
 ├── xrEngine    — CRenderDevice (окно/кадр), консоль, ввод (SDL2), xrTheora
 ├── xrRender_GL — OpenGL-рендерер (namespace render_gl), R2/R3 feature levels
 ├── xrGame      — вся логика CoP + xrServerEntities (+ 97 *_script.cpp биндингов)
 ├── xrScriptEngine — CScriptEngine (VM Lua), потоки-корутины скриптов
 └── (звук xrSound/OpenAL, физика xrPhysics+ODE+OPCODE, xrCDB, xrAICore, xrUICore, xrNetServer, xrGameSpy)
```

Ключевые факты исполнения (из аудита):
- **Главный цикл**: `while (!SDL_QuitRequested()) { … }` — `src/xrEngine/x_ray.cpp:372-437`. Тело цикла = готовый колбэк `emscripten_set_main_loop`
- **Кадр**: `CRenderDevice::ProcessFrame()` `src/xrEngine/device.cpp:261-303` — стадийная загрузка (`g_loading_events`, одна стадия/кадр), параллельные задачи `seqParallel`/`seqFrameMT`, рендер `seqRender` — **весь GL на главном потоке**
- **Потоки**: пул `TaskManager` (`src/xrCore/Threading/TaskManager.cpp:163`, `hardware_concurrency()-1` воркеров безусловно); главный поток умеет исполнять задачи сам (`ExecuteOneTask` при `Wait`). Всего 6 мест создания потоков: пул, splash (`x_ray.cpp:466`), CDB (`xrCDB.cpp:63`), net-sync (`NET_Client.cpp:1078`), GameSpy browser (`GameSpy_Browser.cpp:173`), configs_dumper (Win-only)
- **FS**: `CLocatorAPI` (`src/xrCore/LocatorAPI.cpp`) — индексация `Recurse`→`ProcessOne` (loose) + `ProcessArchive` (.db); чтение синхронное, три пути: `CVirtualFileReader` (mmap файла ≥16КБ, `FS.cpp:543-579`), `CStreamReader` (оконное mmap, `stream_reader.h`), `file_from_archive` (mmap-регион .db + распаковка LZO/PPMd, `LocatorAPI.cpp:1440-1507`)
- **Звук**: OpenAL, очередь 4 буферов/источник, декод Ogg инлайн (`src/xrSound/SoundRender_TargetA.cpp:108-122`); EAX — отдельная опция, EFX не реализован
- **Таймеры**: `std::chrono` + `SDL_GetPerformanceCounter` (`src/xrCore/_math.cpp:22-36`) — переносимо

---

## 4. Целевая архитектура в браузере

```
┌─ Browser tab ──────────────────────────────────────────────────────┐
│  index.html (canvas#canvas, минимальный JS: загрузка, статус-бар)  │
│      │                                                             │
│  xr_3da.js (клей Emscripten) ── xr_3da.wasm (весь движок, 1 модуль)│
│      │              │                │                             │
│  SDL2→canvas    OpenAL→WebAudio   clang/llvm wasm                  │
│      │                                                             │
│  ASYNCIFY: sync-точки ввода-вывода → fetch()                       │
└──────┬──────────────────────────────────────────────────────────────┘
       │ HTTP (Range-запросы, CORS, brotli)
┌──────▼──────────────────────────────┐   ┌──────────────────────────┐
│ dev-server (python; фаза F: caddy)  │   │ fs_index.json (генерится │
│ раздаёт ~/dev/cop (11 ГБ) + wasm    │   │ оффлайн из раскладки+db) │
└─────────────────────────────────────┘   └──────────────────────────┘
```

Принципы:
1. **Один wasm-модуль.** Никаких dlopen, все модули статически.
2. **Синхронный код — асинхронная реальность** через ASYNCIFY (фаза 1). Код движка не переписываем под async.
3. **Ассеты никогда целиком в памяти.** Файловый слой — поверх Range-запросов с LRU-кэшем блоков.
4. **Браузер рулит кадром.** `emscripten_set_main_loop`; vsync/FPS-кап движка отключаем (рендерер уже так делает: `glHW.cpp:25-27,303-319`).
5. **Регресс-дисциплина:** нативная Linux-сборка собирается и запускается после каждого значимого шага.

---

## 5. Рабочие потоки (Workstreams)

Каждый: проблема → решение → точки изменения → приёмка → риск/план Б.

### W1. Платформа и тулчейн — *блокер всего*

**Проблема:** `src/Common/Platform.hpp:3-43` не знает Emscripten → `#error Unsupported platform` ещё до первого cpp. CMake: `find_package(SDL2/OpenAL/...) REQUIRED` (`cmake/XRay.Compiler.GNULike.cmake:143-152`), x86-флаги `-mfpmath=sse -msse3` (`:116-133`), нет ветки платформы WEB.

**Решение:**
1. `Platform.hpp`: добавить ветку `#elif defined(__EMSCRIPTEN__)` → `#define XR_PLATFORM_WEB` (+ `XR_PLATFORM_LINUX`-подобный POSIX-слой: подключить `PlatformLinux.inl` с точечными гвардами — не полагаться на то, что emcc определяет `__linux__`)
2. `cmake/XRay.Platform.Web.cmake` (новый): включается при `CMAKE_SYSTEM_NAME STREQUAL "Emscripten"`:
   - флаги: `-msimd128 -fwasm-exceptions` (проверить совместимость с `XRAY_EXCEPTIONS=0`; иначе `-fignore-exceptions`)
   - НЕ `-msse3` (wasm-SIMD через интринсики уже совместимы — sse2neon-прецедент показывает, что интринсики решаемы; на первое время достаточно скалярных fallback'ов)
   - линковка: `-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=512MB -sMAXIMUM_MEMORY=4GB -sASYNCIFY -sEXPORTED_FUNCTIONS=... -sEXPORTED_RUNTIME_METHODS=...`
   - принудительно `BUILD_SHARED_LIBS=OFF`, `XRAY_USE_LUAJIT=OFF`, `MEMORY_ALLOCATOR=standard` (mimalloc не портирован)
3. `XRay.Compiler.GNULike.cmake`: обернуть `find_package`-блок в `if (NOT EMSCRIPTEN)`; SDL2/OpenAL придут из emscripten-портов, Ogg/Vorbis/Theora/LZO/JPEG — собрать из исходников (или `-sUSE_OGG=1 -sUSE_VORBIS=1` и т.д. — есть в ports: ogg, vorbis, theora? проверить; недостающие — vendored CMake)
4. Platform.hpp → в CMake добавить `-DXR_PLATFORM_WEB` НЕ надо (define из заголовка), но убедиться, что `xr_input.h:9`-паттерн (`__EMSCRIPTEN__`) консистентен

**Точки изменения:** `src/Common/Platform.hpp`, `cmake/*`, корневой `CMakeLists.txt`.

**Приёмка:** `emcmake cmake -S . -B build-wasm -G Ninja && ninja xrCore` собирается.

**Риски:** сторонние либы без emscripten-портов → собрать из исходников (LZO — чистый C; Theora — C; проблем нет). mimalloc → `standard` уже есть опцией.

### W2. Lua 5.1 — *критический путь, но короткий*

**Решение:**
1. Vendored Lua 5.1.5: сабмодуль `Externals/lua51` (форкнуть lua/lua ветку v5.1.5 на GitHub под underherzen), обёртка `Externals/lua51-proj/CMakeLists.txt` (по образцу `LuaJIT-proj`, тривиально: ~25 .c файлов)
2. Починить OFF-путь CMake (из аудита):
   - `Externals/xrLuaFix/CMakeLists.txt:38-42` — ссылка на несуществующий imported-target `Lua51` → наша цель `lua51`
   - `Externals/luabind/src/CMakeLists.txt:38-43` — `find_package(Lua REQUIRED)` → наша цель + распространить include-пути PUBLIC
   - include-пути Lua сейчас приходят только от `xrLuaJIT` (PUBLIC `Externals/LuaJIT/src`) — в OFF-ветке их не получает никто → сделать у `lua51` PUBLIC include
3. `src/xrGame/Level.cpp:601` — обернуть `LUA_GCTIMEOUT` в `#if XRAY_USE_LUAJIT` (runtime-fallback на `LUA_GCSTEP` уже есть в `:603-611`)
4. **Лексер-расширения форка:** LuaJIT-форк OpenXRay разрешает `//` и `/* */` комментарии в скриптах (`Externals/LuaJIT/src/lj_lex.c:360-380`). Vanilla Lua 5.1 их не парсит. План: сканер gamedata (тулза) на предмет таких комментариев; если есть — препроцессор-шим в `CScriptEngine::load_buffer` (`script_engine.cpp:313-344`, вырезание `//…` вне строк) за флагом `XRAY_WEB_LEXER_SHIM`
5. `bit.*`: движковые C-fallback'и есть; сканером проверить, не используют ли скрипты `bit.band` и т.п. напрямую (полифилл в `_g.script` при необходимости)
6. Потеря `ffi`/`jit.*` — by design; гвард `jit == nil` в `_g` существует (`script_engine.cpp:944-949`)

**Приёмка:** нативная сборка с `XRAY_USE_LUAJIT=OFF` собирается, игра **запускается и играбельна** (это одновременно кандидат в PR апстриму). Затем то же под emcc.

**Риск (главный всей фазы E):** интерпретатор Lua в 5–20× медленнее JIT на горячих AI-скриптах (GOAP-планеры на каждого NPC — `script_action_planner_wrapper.cpp:27-35`, ~38 точек `call_member`). Митигации (фаза F): снижение частоты апдейтов AI (`CScriptProcess::update` — один поток/кадр round-robin, `script_process.cpp:53-88` — уже дозировано), тюнинг GC (`lua_gc_method`), профилирование Tracy (`script_engine.cpp:28,928`).

### W3. Модель исполнения

**Решение:**
1. `src/xrEngine/x_ray.cpp:372-437`: вынести тело цикла в `void CApplication::WebFrame()`; под `XR_PLATFORM_WEB` — `emscripten_set_main_loop(WebFrame, 0, 1)`; выход (`SDL_QuitRequested`) → `emscripten_cancel_main_loop()` + shutdown-путь
2. `Sleep()`-гварды: `device.cpp:298-299` (FPS-кап — убрать: браузер пацует), `device.cpp:410-412` (busy-wait калибровки — убрать), `device.cpp:46,302`, `IGame_Level.cpp:170`, `FDemoPlay.cpp:86`; `PlatformWeb.inl`: `Sleep()` → no-op (или `emscripten_sleep` под ASYNCIFY — только для загрузочных стадий)
3. Однопоточный режим: `TaskManager::SpawnThreads()` под WEB не звать (флаг `XRAY_WEB_SINGLETHREAD`); splash-поток (`x_ray.cpp:466`) → отключить (сплэш не нужен); `NET_Client.cpp:1078`, `GameSpy_Browser.cpp:173` → заглушки (мультиплеер — не-цель)
4. `Core.Initialize` стартует задачи через TaskManager (`x_ray.cpp:237-297`) — в однопоточном режиме сериализуются автоматически через `Wait` (уже работает так)

**Приёмка:** в браузере лог доходит до инициализации рендерера без зависаний.

**Риск:** где-то скрытый блокирующий wait → найдётся зависанием; отладка через `ASYNCIFY`-стек в консоли.

### W4. Виртуальная ФС — *самая объёмная работа порта*

> **Как сделано на деле (M3–M5), отличается от плана ниже.** Движок не тронут:
> вся ФС — JS-бэкенд Emscripten FS `tools/web/xr_fs_pre.js` (XRFS, `--pre-js`),
> смонтирован в `/cop`, CLocatorAPI сканирует его как обычный POSIX-каталог.
> Индекс — `files.json` (make_index.py), стартовый пакет мелких файлов `pack.bin`
> (≤512 КБ, 156 МБ, один запрос). Чтение — **синхронный XHR** (`charset=x-user-defined`):
> файлы ≤8 МБ целиком (LRU 256 МБ), большие — 1 МБ-блоками через Range (LRU 96 блоков).
> `mmap` (.db-регионы) — копия в кучу wasm. Запись — в памяти (оверлей, COW).
> Ограничения: главный поток блокируется на каждом промахе кэша (в обычном Chrome
> 75–240 с загрузки → диалог «страница не отвечает»); сейвы не переживают
> перезагрузку. План: перенос движка в Worker (`-sPROXY_TO_PTHREAD`, OffscreenCanvas;
> sync-XHR в Worker легален) + IDBFS/OPFS для `$app_data_root$`.

**Проблема:** CLocatorAPI при старте сканирует директории (POSIX findfirst), читает .db через POSIX `open/read/lseek`, а отдаёт читателям mmap-указатели. В браузере этого нет; 11 ГБ в память не влезают.

**Решение — три слоя:**

1. **Индекс (оффлайн):** тулза `tools/web/fs_index` (C++ или python): обходит `~/dev/cop`, парсит заголовки .db (`archive_file_header`, `LocatorAPI.h:120-135` — формат уже в аудите) → `fs_index.json`: `{виртуальный путь: {loose|archive: [файл, offset, size_comp, size_real, flags]}}` + crc. Это заменяет `Recurse/ProcessOne/ProcessArchive` в рантайме
2. **Сервер:** python `tools/web/serve.py`: раздаёт `~/dev/cop` + `/index.json` + `/xr_3da.*`; заголовки: `Cross-Origin-Opener-Policy`/`Cross-Origin-Embedder-Policy` (задел под SAB), Range, brotli для текстовых. Фаза F: caddy/nginx + prefetch
3. **Рантайм-слой (в движке, за `XR_PLATFORM_WEB`):**
   - `CLocatorAPI::_initialize`: вместо скана — `fetch fs_index.json` (через ASYNCIFY), заполнение `m_files`/`m_paths` — структуры данных те же (`file{vfs, ptr, size_real, size_compressed}`)
   - Класс `CWebFile`: заменяет mmap-читатели. Семантика `IReader` (синхронный `seek/read`) поверх: block-cache (LRU, 256КБ-блоки) + `fetch(Range)`; ASYNCIFY делает это прозрачным для вызывающих
   - Три точки замены: `CVirtualFileReader` (`FS.cpp:543`), `CStreamReader` (`stream_reader.h`), `file_from_archive` (`LocatorAPI.cpp:1440-1507`) — все за интерфейсом читателей, вызывающий код не трогаем
   - Распаковка (LZO/PPMd/lz) — без изменений, из распакованного блока
   - Запись (`$app_data_root$`: сейвы, `user.ltx`, логи): mount IDBFS (`/appdata`), периодический `FS.syncfs` (по событию кадра раз в N сек + на выходе); `w_open`/`CFileWriter` не меняются

**Приёмка:** холодная загрузка Затона; в DevTools Network видны только запрошенные диапазоны; пиковая память < 2.5 ГБ.

**Риски:** латентность HTTP на случайных чтениях (сик-сторм от streaming-звука и геометрии) → укрупнение блоков, prefetch соседних, агрессивный кэш .db-заголовков; ASYNCIFY-оверлей на длинных цепочках → профилировать.

### W5. Рендер (достройка заготовки)

1. Включить путь `XR_PLATFORM_WEB` (определяется в W1), убрать потерю контекста: контекст создаётся на `#canvas` (`glHW.cpp:96-129`) — SDL2-порт Emscripten даёт canvas сам; согласовать `SDL_CreateWindow`-путь (`Device_Initialize.cpp:73` — окно создаётся, но GL-атрибуты на вебе уже no-op, `glHW.cpp:230-232`)
2. Прогнать `misc/web/tools/essl_check.sh` → починить все шейдеры, которые не компилируются как `#version 300 es` (список даст тулза; шейдеры — текст в `res/gamedata/shaders/gl/`)
3. Мелочи из аудита: `glPolygonMode`-заглушки (`glR_Backend_Runtime.h`), текстурный юнит загрузки (`glHW.cpp:183-187`), `Present()` — блит в default FB (`glHW.cpp:303-319`)
4. Консольная переменная `renderer` дефолт → r3/rgl на вебе

**Приёмка:** главное меню растянуто на canvas, скриншот ≈ нативному.

### W6. Звук
- Порт OpenAL Emscripten (`-sOPENAL=1`): устройство дефолтное, перечисление может вернуть одно — код уже терпит (`OpenALDeviceList.cpp` fail-soft)
- EAX → выключен (`XR_HAS_EAX` не определится), EFX не реализован и так
- Потоковое воспроизведение: очередь буферов ложится на WebAudio напрямую

### W7. Окно/ввод
- SDL2-порт: клавиатура/мышь/геймпад работают из коробки; canvas id=`canvas`
- Pointer Lock для обзора (взять при клике, отпускать по Esc) — маленький JS-модуль в index.html
- Изменение размера окна → `SDL_WINDOWEVENT` уже обрабатывается (`x_ray.cpp:378-380`)

### W8. Сейвы/персистентность
- `$app_data_root$` → IDBFS (см. W4.3); `user.ltx` переживает перезагрузку страницы
- Скриншоты (`$screenshots$`) — в IDBFS + кнопка «скачать» в JS (косметика, фаза F)

### W9. Производительность (фаза F, по приоритету)
1. Профилирование: DevTools Performance + Tracy (`XRAY_ENABLE_TRACY` уже в CMake)
2. `-O3` + `-flto` (LTO-ветка CMake уже есть: `XRay.Build.cmake:28-33`)
3. SIMD128 — включён с W1; проверить, что горячие SSE-места (xrCDB-рейкасты, скиннинг `SkeletonXSkinXW_SSE.cpp`, DetailManager) собираются на wasm-интринсиках
4. Потоки фазы 2: `-pthread` + COOP/COEP (заголовки уже ставим в W4.2) → вернуть `TaskManager`-пул; MSound/FS-воркеры
5. Lua: тюнинг GC, частоты апдейтов; если совсем больно — исследовать lua-wasm JIT'ы (иммature, честно пометить как эксперимент)
6. Ассеты: brotli на сервере; конвертация текстур в KTX2/Basis (только своё инфра-звено на dev-server'е, движку прозрачно через DDS-путь загрузчика)

### W10. Тестирование и CI (на форке)
1. `tools/web/regress-native.sh`: сборка native + запуск 30с + греп контрольных строк лога (FS init, renderer selected) — гонять после каждого шага
2. GitHub Actions на форке: job wasm (`emcmake` + `ninja xrCore` как smoke) + job native
3. `essl_check` в CI после каждого изменения шейдеров
4. Скриншот-сравнение main menu (native vs wasm) — ручной чеклист на вехах

---

## 6. Реестр рисков

| # | Риск | Вер-ть | Влияние | Митигация |
|---|---|---|---|---|
| R1 | Lua-интерпретатор медленный на AI | высокая | gameplay-FPS | тюнинг частот (дозировка уже встроена), Tracy-профиль, фаза F |
| R2 | ASYNCIFY-оверхед на IO | средняя | загрузка/FPS | блочный кэш, prefetch; фаза 2 — pthreads убирают ASYNCIFY |
| R3 | Память > 4 ГБ на больших уровнях | средняя | OOM | бюджет текстур (`-rt_num_pp`, pool'ы движка), стриминг W4; крайний случай — wasm64 (браузеры уже умеют) |
| R4 | Скрипты используют `//`-комментарии/`bit.*` | средняя | падение загрузки | сканер gamedata + шимы (W2.4-5) |
| R5 | ESSL-шейдеры не собираются пачкой | средняя | чёрный экран | essl_check даёт список; правки точечные в текстах шейдеров |
| R6 | Апстрим уходит (framegraph-ветка удаляет essl_web) | средняя | merge-конфликты | регулярный rebase `upstream/dev` → `wasm-port`, наши правки атомарны |
| R7 | Скрытые блокирующие вызовы в глубине | высокая | зависания | последовательная приёмка, ASYNCIFY-стеки, log-трейсинг |
| R8 | Один исполнитель выгорит | 😅 | всё | документ этот; маленькие коммиты; вехи-победы |

---

## 7. Порядок реализации (техническая последовательность)

```
M0 (сделано): окружение, нативная сборка, запуск, emsdk, форк/ветка
M1 = W1 + W2:  платформа+тулчейн+Lua51 → ninja xrCore (wasm) собирается
               регресс: натив с XRAY_USE_LUAJIT=OFF играется
M2 = W1(линк): весь движок линкуется в xr_3da.wasm (заглушки W3-W6 минимальны)
M3 = W3 + W4(MVP): главный цикл + индекс/стриминг минимальный
               → в браузере: лог инициализации до рендера
M4 = W5 + W7:  меню в canvas, мышь
M5 = W4(полный) + W8: Затон загружается, ходьба, сейвы
M6 = W6 + полировка: звук, стабильность, квест-смоук
M7 = W9 + W10: производительность, CI
```

Каждая веха = ветка/PR в `wasm-port` с ревью собственного diff'а (самопроверка: «не сломал ли натив?»).

---

## 8. Правила внесения изменений

1. **Платформо-специфика** — только через `XR_PLATFORM_WEB` (уже конвенция апстрима) или `__EMSCRIPTEN__` в платформенных заголовках. Никаких `#ifdef WASM_MY_HACK`.
2. **Натив не ломаем:** после каждого workstream-шага — `tools/web/regress-native.sh`. Правки, деградирующие натив, — только за осознанным флагом.
3. **Кандидаты в апстрим** (отдельными чистыми PR из нашей ветки): починка `Level.cpp:601`, CMake OFF-пути Lua51, гварды `Sleep()`, `LUA_GCTIMEOUT`. Это снижает цену merge'а с `upstream/dev` (риск R6).
4. Коммиты: атомарные, `module: what and why`. Перед push — self-review диффа.
5. Всё серверное (serve.py, fs_index) живёт в `tools/web/` в репо — порт самодостаточен.

---

## 9. Справочник команд (по состоянию на сегодня)

```bash
# нативная сборка (регресс-эталон)
cmake --build ~/dev/xray-16/build -j 16

# запуск нативный
cd ~/dev/cop && ./bin/xr_3da -fsltx ./fsgame.ltx

# wasm-сборка (после M1)
source ~/emsdk/emsdk_env.sh
emcmake cmake -S ~/dev/xray-16 -B ~/dev/xray-16/build-wasm -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DXRAY_USE_LUAJIT=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build ~/dev/xray-16/build-wasm -j 16

# dev-сервер + игра в браузере (после M3)
python3 ~/dev/xray-16/tools/web/serve.py ~/dev/cop --wasm ~/dev/xray-16/build-wasm
# → http://localhost:8000

# git-поток
git checkout wasm-port && git rebase upstream/dev   # синк с апстримом
```
