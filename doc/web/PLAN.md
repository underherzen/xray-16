# План: порт OpenXRay (S.T.A.L.K.E.R.: Call of Pripyat) на WebAssembly

> Рабочий документ. Обновляется по мере прохождения этапов.
> Технические решения, точки изменения в коде и реестр рисков — в [ARCHITECTURE.md](ARCHITECTURE.md).
> Дата старта: 2026-10-11. Базовая линия: Linux-сборка движка работает (1904/1904),
> движок запускается, emsdk установлен и проверен.

---

## Исходное состояние (что уже есть)

| Компонент | Путь | Статус |
|---|---|---|
| Исходники движка (OpenXRay/xray-16, ветка dev) | WSL: `~/dev/xray-16` | ✅ собрано |
| Игровая раскладка (симлинки на CoP 11 ГБ) | WSL: `~/dev/cop` | ✅ запускается |
| Сборка ninja/Release | WSL: `~/dev/xray-16/build` | ✅ |
| Emscripten SDK | WSL: `~/emsdk` | ✅ hello-world wasm работает |
| Игра CoP 1.6.02 (+ релизный OpenXRay) | Win: `C:\Program Files (x86)\S.T.A.L.K.E.R. - Зов Припяти` | ✅ |

Цикл разработки: `правка кода → ninja → вкладка браузера (F12 = лог) → правка`.

---

## Архитектура порта (что во что превращается)

| В движке | Станет в wasm | Сложность |
|---|---|---|
| LuaJIT (вся игровая логика) | **Интерпретатор Lua 5.1** — JIT в wasm запрещён | высокая, обязательна |
| Файлы: .db-архивы + gamedata | **Стриминг по HTTP** (range-запросы, лениво) | высокая, обязательна |
| OpenGL рендерер | **WebGL2** через Emscripten | высокая |
| Потоки (пул на 16 ядер) | Сначала **однопоточный режим**, потом Emscripten pthreads | средняя |
| SDL2 (окно, ввод) | SDL2-порт Emscripten (canvas + события) | низкая |
| OpenAL (звук) | OpenAL Emscripten (поверх WebAudio) | низкая |
| Главный цикл while(1) | `emscripten_set_main_loop` (браузер рулит кадром) | средняя |
| Сейвы в Documents | IDBFS (IndexedDB) | низкая |

Ключевая развилка, на которую смотреть всегда: **wasm32** (потолок 4 ГБ памяти) —
стартуем на нём; wasm64 не трогаем до этапа F.

---

## ФАЗА A — «Компилируется» ✈️ фронт компиляции

**Цель:** весь движок собирается `emcmake`+`emmake` в `xr_3da.wasm`. Пока не запускается — просто линкуется.

Шаги:
1. **Git-гигиена**: форк OpenXRay на GitHub, ветка `wasm-port` от `dev`. Все правки — мелкими
   коммитами с осмысленными сообщениями. Настроить `ccache` для ускорения пересборок.
2. **Замена LuaJIT → Lua 5.1** (первая, т.к. LuaJIT вообще не соберётся):
   - сабмодуль/копия PUC-Lua 5.1 в Externals, CMake-переключатель `XRAY_LUA_IMPLEMENTATION`
   - проверить API-несовместимости в xrScriptEngine/xrLuabind (LuaJIT-специфика: `lj_*`,
     `jit.*`, расширения FFI если используются)
   - контроль: **нативная Linux-сборка с Lua 5.1 всё ещё работает и игра запускается**
     (это наш регресс-тест после каждого большого шага!)
3. **emcmake configure**: заставить CMake движка принять Emscripten-тулчейн
   (определение платформы, проверки компилятора, флаги `-sUSE_SDL=2` и т.п.).
4. **Итеративная сборка по библиотекам** в порядке зависимостей:
   `xrCore → xrCDB → xrAPI → xrSound → xrUICore → xrPhysics/xrODE/xrOPCODE → xrParticles →
   xrNetServer → xrServerEntities → xrScriptEngine → xrAICore → xrGame → xrEngine/xr_3da`.
   Типовые правки: отсутствующие `#include`, WinAPI-остатки, inline-asm, интринсики,
   64-битные предположения, заглушки `#ifdef __EMSCRIPTEN__`.
5. **Линковка xr_3da** с заглушками на всё, что не резолвится.

**Сделано, когда:** `build-wasm/xr_3da.js + xr_3da.wasm` существуют; `node xr_3da.js`
пишет в консоль первые строки лога движка (или падает на первом syscalls — уже победа).

Оценка: 1–3 недели вечерних сессий. Это самая нудная фаза — «сгребание ошибок».

---

## ФАЗА B — «Загружается» 🚀

**Цель:** движок в браузере проходит инициализацию: FS, скрипты, конфиги — до попытки создать окно.

Шаги:
1. **Главный цикл** → `emscripten_set_main_loop` (разрезать кадр на колбэк).
2. **Файловая система — стриминг**:
   - мини HTTP-сервер (python) раздаёт `~/dev/cop` с CORS + range-запросами
   - генератор индекса файлов (JSON) для CLocatorAPI вместо сканирования директорий
   - кастомный бэкенд Emscripten FS: open/read/seek → fetch с Range
   - контроль памяти: ничего не грузим целиком, только запрошенные куски
3. **Лог** → console.log (обычно работает из коробки).
4. Прогон до первого рантайм-краша, разбор стека (source maps в DevTools).

**Сделано, когда:** в консоли браузера виден лог загрузки: FS init → system.ltx →
выполнение стартовых Lua-скриптов.

Оценка: 1–2 недели.

---

## ФАЗА C — «Главное меню» 🖼️

**Цель:** в canvas видно главное меню CoP, работает мышь.

Шаги:
1. SDL2-бэкенд Emscripten: окно → canvas, ввод → события.
2. **Рендерер GL → WebGL2**: профили GLSL, шейдерные вариации, FBO, расширения.
   Начинаем с минимального пути, который нужен только меню.
3. Дебаг чёрного экрана: рендер-док Calls в DevTools — наш главный инструмент.

**Сделано, когда:** скриншот меню в браузере. Сравниваем со скриншотом нативной
сборки — визуально похоже.

Оценка: 2–4 недели (рендер — самое упрямое место).

---

## ФАЗА D — «Уровень» 🗺️

**Цель:** загрузка локации (Затон) в браузере, можно ходить.

Шаги: полный путь рендера (статика, лайтмапы, деталь), стриминг геометрии/текстур
по сети, память в бюджетах (цель < 2.5 ГБ), физика коллизий, ввод клавиатура+мышь,
capture pointer. Сейвы в IDBFS.

**Сделано, когда:** 5 минут непрерывной ходьбы по Затону без краха и OOM.

Оценка: 3–6 недель.

---

## ФАЗА E — «Игра» 🎮

**Цель:** играбельно: NPC, бой, звук, инвентарь, квесты (это всё Lua — проверка замены LuaJIT).

Шаги: OpenAL поверх WebAudio (3D-позиционирование, EFX), ИИ толпы/афлайн,
network-free проверка, стабильность на длинных сессиях, сохранение/загрузка.

**Сделано, когда:** принят один квест от Сыча. (Серьёзно — это интеграция всего.)

Оценка: 3–6 недель.

---

## ФАЗА F — «Оптимизация» ⚡ (бесконечная)

Приоритеты по мере необходимости:
1. Потоки: Emscripten pthreads (SharedArrayBuffer + COOP/COEP заголовки сервера)
2. `-msimd128` (SIMD для математики движка), `-flto`, `-O3`
3. Текстуры: конвертация в Basis/KTX2 на лету сервером — меньше сеть/память
4. Время загрузки: кэш ассетов (Cache API), предзагрузка, сжатие (brotli на сервере)
5. Если упрёмся в 4 ГБ — тогда смотреть wasm64

---

## Правила работы (дисциплина)

1. **После каждого большого шага — регресс-тест**: нативная Linux-сборка собирается
   и игра запускается. Портируем, не ломая.
2. Коммиты мелкие, ветка `wasm-port`, в PROGRESS.md записываем: что сделали / что сломалось / что дальше.
3. Правки стараться оформлять как `#ifdef __EMSCRIPTEN__` / платформо-независимые —
   чтобы апстрим принимал (наш вклад в OpenXRay) и merge с их `dev` не болел.
4. Ничего не публиковать онлайн кроме кода; ассеты игры — только локально.
5. Сомневаешься — сверяйся с нативной сборкой: «как это работает там?»

## Ближайшие конкретные задачи (следующая сессия)

- [x] Форк + ветка `wasm-port` (сделано 2026-10-11); ccache — установлен 2026-10-12
- [x] **M0.5: нативный запуск с модом Atmosphere — эталон-оракул для wasm** ✅ 2026-10-12
  - мерж в `~/dev/cop/gamedata`: мод (5.6 ГБ, 5271 файл) + движковый `res/gamedata`
    поверх (движок побеждает, как инсталлятор релиза) — верифицировано
  - новая игра на Затоне стартует: автозапуск `-start 'server(all/single/alife/new)'`
    (важно: для новой игры maps-параметр = `all` = `spawns/all.spawn`, не имя уровня;
    `zaton` = режим отладки карты без alife — на нём FATAL «Can't find spawn file»)
  - **GPU в WSL**: `/dev/dxg` + `GALLIUM_DRIVER=d3d12` (mesa) → GL 4.6 Core на
    RTX 3070 Ti. llvmpipe больше не используем: он держал текстуры в ОЗУ (пик 4.35 ГБ)
    и портит метрики памяти
  - **шейдеры мода под GL: 0 ошибок**; в `gamedata/shaders/gl` мода 444 файла,
    62 отличаются от движковых + 1 мод-only (`shadow_direct_base.ps`) — списки:
    `~/dev/cop/_m05/gl_*.list` (вход для `essl_check` W5)
  - **память на реальном GPU: пик 1.37 ГБ, анонимная куча 1.1 ГБ** — бюджет wasm32
    (4 ГБ) выглядит достижимым; текстуры уйдут в GPU-процесс браузера
  - шум мода: 755 «Section of upgrade not exist» (ваниль: 0) — некритично
  - попутно починено в движке: `DebuggerIsPresent()` на Linux вешал процесс
    (PTRACE_TRACEME без отката; коммит 93c68d5af); для WSLg/X11 нужен
    `vid_refresh_rate 60` в `_appdata_/user.ltx`
  - артефакты прогона: `~/dev/cop/_m05/` (engine.log, mem2.log, shots/)
- [x] **M1: Lua 5.1 + Emscripten-тулчейн + `ninja xrCore` под emcc** ✅ 2026-10-12
  - **LuaJIT → PUC Lua 5.1.5**: вендорен в `Externals/lua51` (таргет `Lua51`).
    Системный путь мёртв: Ubuntu 26.04 больше не пакует lua5.1, а upstream-ветка
    `XRAY_USE_LUAJIT=OFF` звала `find_package(Lua)` без версии (подтянула бы 5.4)
    и без include-путей. Фиксы по пути: `LUA_OK` — это 5.2+ (в 5.1 успех = 0);
    `LUA_GCTIMEOUT` (расширение LuaJIT) — гвард по `XRAY_USE_LUAJIT`;
    **краш найден gdb**: `luaL_register(L, "lua_extensions", nullptr)` — LuaJIT
    терпит NULL-список, PUC разыменовывает → заменено на `lua_newtable+setglobal`
    (субмодуль xrLuaFix); luabind линкует таргет `Lua51` (субмодуль luabind)
  - **платформа**: `Platform.hpp` — `__EMSCRIPTEN__` → `XR_PLATFORM_WEB` (POSIX-подобная),
    `__wasm32__` → `XR_ARCHITECTURE_WASM`; `PlatformWeb.inl` переиспользует Linux-слой
  - **CMake**: гварды `-fdelayed-template-parsing`/stdlib-детекта/`-msse3` для
    Emscripten; вместо них `-msimd128` + `-sUSE_SDL=2` (фейковые SDL-заголовки
    sysroot требуют порт); INTERFACE-заглушки `SDL2::*`, `OpenAL::OpenAL`,
    `Ogg/Vorbis/Theora/JPEG` (настоящее придёт из портов `-sUSE_*` при линковке);
    **LZO 2.10 вендорен** в `Externals/lzo` (порта emscripten нет, а xrCore сжимает
    им пакеты/уровни)
  - **xrCore портабельность**: `_finddata_t` → `_finddata64i32_t` для wasm32;
    SSE flush-zero стабы (как ARM); `RECOMMENDED_TASK_SIZE=64`; убран лишний
    intrinsics-include в TaskManager
  - **результат**: `emcmake cmake` — exit 0; `ninja -C build-wasm xrCore` — зелёный
    (`build-wasm/src/xrCore/xrCore.a`, бонусом `libxrMiscMath.a`, `libLZO.a`)
  - **регресс**: нативная JIT-сборка зелёная; игра на Lua 5.1 доходит до in-game
    Затона (145+ скриптов, 6464 спавна, уровень, рендер) — под gdb стабильно;
    без gdb ловится флейк-крач mesa/d3d12 в `hw_Render_dump` (detail-трава),
    тот же наблюдался однажды на JIT в M0.5 — окружение, не Lua
  - **ещё два корневых бага найдены и починены** (диагностика: коры+gdb,
    valgrind, ASAN+UBSan):
    - **luabind GPF**: `object_rep` со `std::aligned_storage<32>` кладётся в
      userdata, а у PUC Lua 5.1 заголовок Udata = 40 байт на x86-64 (у LuaJIT —
      16) → payload всегда 8-в-16 → SSE-записи конструктора = General Protection
      Fault. Это и были все «бродячие» сегфолты. Фикс: убрать over-alignment
      класса + ручное выравнивание буфера держателя (субмодуль luabind, a9efb40)
    - **профайлер-мок**: ванильные скрипты начинаются с
      `if jit == nil then profiler.setup_hook() end` — под PUC ветка выполняется,
      модуль ещё не загружен → runtime error в `_g.script` → каскад умирает →
      `CALifeSimulator` не находит `start_game_callback` (FATAL). Фикс: no-op
      `profiler.setup_hook` при `!XRAY_USE_LUAJIT` в init движка (bb1deaca3)
  - вердикт по «бродячим» крашам: движковые — устранены двумя фиксами выше;
    остался только драйверный mesa/d3d12 (`memmove` в NULL из gallium при
    рендере, ловился и на JIT) — окружение WSLg, для wasm не существует
  - **субмодули** luabind (`a9efb40`) и xrLuaFix (`9609600`) — локальные ветки
    `wasm-port`, НЕ запушены (репозитории не форкнуты) — форкнуть при публикации
  - **находка для M2**: emcc имеет compat-слой x86 SSE поверх `wasm_simd128.h`:
    `-msimd128 -D__SSE__` → `__m128`/`_mm_*` работают (проверено хеллоуворлдом);
    это план для xrCDB (`ISpatial_q_ray.cpp` требует `__m128`)
- [x] **M2: SSE-compat + вся цепочка библиотек + линковка xr_3da.wasm** ✅ 2026-10-12
  - **SSE**: `-msimd128 -D__SSE__ -D__SSE2__` — emcc имеет compat-слой x86 SSE
    поверх `wasm_simd128.h`; арх-цепочки интринсиков приняли `XR_ARCHITECTURE_WASM`
    (xrCDB `ISpatial_q_ray`/`xrCDB_ray`, xrParticles `noise`/`particle_actions_collection`);
    `Platform.hpp` теперь определяет и апстримовские `XR_ARCHITECTURE_WASM32/64`
    (на них ссылается заготовка 985a70116 в DetailManager/ParticleEffect)
  - **медиа-порты**: `-sUSE_VORBIS=1 -sUSE_LIBJPEG=1` на компиле (заголовки);
    **libtheora 1.1.1 вендорена** (`Externals/theora`, порта emsdk нет);
    OpenAL — заголовки из sysroot `include/AL` (порт убран из emsdk 6.x,
    сама либа есть: линкер получает `-lal`, WebAudio-реализация)
  - **GameSpy** (субмодуль, ветка wasm-port): `__EMSCRIPTEN__` → `_LINUX/_UNIX`
    (тот же BSD-sockets API, компил-онли)
  - **xrAICore include-цикл**: `graph_engine.h` включает problem_solver-заголовки
    посреди определения `CGraphEngine`, и `solve()` трогал незавершённый тип.
    GCC откладывает проверку до инстанциации, clang — нет (эмсripten = clang).
    Фикс: зависимый шим `solve_graph_search` (объявлен в problem_solver_inline.h,
    определён в graph_engine_inline.h)
  - **линковка xr_3da**: линк-флаги портов в `src/xr_3da/CMakeLists.txt`
    (`-sUSE_SDL=2 -sUSE_VORBIS=1 -sUSE_LIBJPEG=1 -sALLOW_MEMORY_GROWTH=1
    -sMAX_WEBGL_VERSION=2` — последний переключает GL-либу на WebGL2-вариацию,
    где живёт `glDrawElementsInstancedBaseVertexBaseInstanceWEBGL`);
    `pthread_setname_np` отключён на однопоточном wasm
  - **результат**: `bin/x86/Release/xr_3da.js` (271 КБ) + `xr_3da.wasm` (23.3 МБ);
    под node движок входит в `main()` и доходит до опроса экрана SDL (падение на
    браузерном `screen` API — норма вне браузера; фаза A «компилируется» закрыта)
  - цепочка: xrCore → xrCDB → xrAPI → xrSound → xrUICore → xrPhysics/xrODE/xrOPCODE →
    xrParticles → xrNetServer/xrGameSpy → xrScriptEngine → xrAICore → xrGame →
    xrRender_GL/xrImGui/xrMaterialSystem → **xr_3da** — все зелёные под emcc;
    xrGame собрался с нуля без единой правки
  - регресс: нативные сборки Lua 5.1 и JIT зелёные
  - субмодули: GameSpy (`d0974d8`, ветка wasm-port) добавлен к luabind/xrLuaFix
    как локальный не-пушенный
- [x] **M3 (фаза B): движок «Загружается» в браузере** ✅ 2026-10-12
  - **главный цикл** → `emscripten_set_main_loop`: тело while-цикла вынесено
    в `CApplication::RunApplicationFrame`, под `XR_PLATFORM_WEB` вешается
    колбэком (rAF-пейсинг, `simulate_infinite_loop=true`); нативный путь —
    тот же код в while. Попутно: под вебом не запрашиваем
    `SDL_INIT_GAMECONTROLLER` (порт Emscripten его не умеет → SDL_Init падал
    «Gamepads not supported»)
  - **FS-стриминг (XRFS)**: `tools/web/` — вендоренный в `--pre-js` бэкенд
    Emscripten FS: монтирует игровую раскладку в `/cop` + chdir до main();
    ленивое дерево по files.json (индекс 5578 файлов, 10.4 ГБ); маленькие
    файлы (<8 МБ) — целиком в кэш, большие — 1 МБ-блоками через HTTP Range
    (sync XHR трюком `charset=x-user-defined` — responseType на главном
    потоке запрещён); записи — в память поверх оверлея (COW для индексных
    файлов: логи, user.ltx, сейвы). **Важно: errno в FS-слое этого emsdk —
    WASI-коды (ENOENT=44, EACCES=2), не musl** — иначе движок ловит
    «Permission denied» из ниоткуда
  - **инфраструктура**: `serve.py` (localhost, CORS + Range; ассеты наружу
    не уходят), `make_index.py`, `index.html`, `run_node.js`/`fsprobe.js`
    (dev-харнесс; node есть в emsdk: `$EMSDK_NODE`); деплой — `~/dev/web`
    (wasm + files.json + симлинк `cop -> ~/dev/cop`)
  - **стек 16 МБ** (`-sSTACK_SIZE`): движок alloca/string4099-тяжёлый, дефолт
    64 КБ переполняется в статические данные → дикие «memory access out of
    bounds» глубоко в безобидном коде (libc++ ostream)
  - **результат в браузере** (headless Chrome): FS init (42268 файлов) →
    system.ltx → user.ltx → рендер-девайс **WebGL2** (GLSL ES 3.00) → 3417
    .thm → 16 стартовых Lua-скриптов → `Starting engine...` → кадры идут
    через rAF (видео-стрим тикает покадрово). Цель фазы B перевыполнена:
    дошли не «до окна», а до работающего рендера и главного цикла
  - **под node** движок идёт до `SDL_CreateWindow` (нужен DOM — граница
    харнесса); `screen`/`window` стабы в run_node.js ставятся ПОСЛЕ require
    (иначе клей считает окружение вебом и ломает fs)
  - **символы в трейсах**: `--profiling-funcs --emit-symbol-map` — без них
    стектрейс в браузере = бессмысленные wasm-function[N]
  - регресс-нативно: Lua 5.1 сборка зелёная; gdb-прогон — 145 скриптов →
    alife → уровень; единственный SIGSEGV — известный драйверный флейк
    mesa/d3d12 в `CDetailManager::hw_Render` (деталь-трава; ловился и на
    чистом JIT в M0.5/M1, окружение WSLg, к движку отношения не имеет)
  - коммиты: 428d017a9 (main loop + gamepad), 2f8bde914 (tools/web),
    2f39d5cd5 (emcc link plumbing)
- [x] **M4 (фаза C): главное меню CoP рендерится в браузере, мышь работает** ✅ 2026-10-12
  - **меню видно**: полный UI CoP в canvas (шрифты, кнопки, панель версий,
    видео-фон) — скриншоты `~/dev/cop/_m04/menu_browser*.png`, визуально
    совпадает с нативным (`native_menu.png`). Ни одной ошибки компиляции
    шейдеров: ESSL-переписыватель upstream (985a70116, `rgl_essl_web.h`)
    покрывает весь меню-путь
  - **мышь работает**: синтетические события на canvas (`?mouse=` в
    index.html) → SDL → движок: hover, клик по «Новая игра» открывает
    модальный диалог, курсор отрисовывается
  - **старт ≈ 2 мин → ~75 с до меню**: `pack.bin` — пакет всех файлов
    ≤512 КБ (3839 шт., 156 МиБ) одним sync-XHR в XRFS preRun; 3417 .thm
    теперь читаются из посева кэша за 12 с (было ~2 мин поштучных XHR)
  - **ГРАБЛИ stale-индекса**: `FileDownload` (xrCore/FS.cpp) сверяет
    fstat с фактом чтения — если files.json старее реальных файлов
    (нативные прогоны переписали user.ltx) → FATAL «Can't read from
    file». files.json и pack.bin генерировать и деплоить ТОЛЬКО парой;
    XRFS печатает XRFS WARNING при рассинхроне
  - **theora-видео меню**: аллокация текстуры шла с форматом GL_BGRA —
    в WebGL2 его нет → GL_INVALID_ENUM 0x500 («Invalid video stream») →
    видео отваливалось. Фикс: под XR_PLATFORM_WEB аллокируем GL_RGBA
    (data==nullptr, эквивалентно); покадровая заливка уже имела
    web-ветку (CPU-свизл BGRA→RGBA)
  - `effects\shadow_world` blender — R1-only, в R2+ рендерерах по
    дизайну nullptr: сообщение доброкачественное (есть и в нативе)
  - S3TC-текстуры работают из коробки: emscripten включает ВСЕ
    поддерживаемые WebGL-расширения при создании контекста
  - инструменты: `tools/web/probe_chrome.sh` (headless Chrome: консоль +
    PNG-скриншот canvas), `?shot=SECONDS`/`?mouse=` в index.html
  - регресс-нативно: сборка зелёная (включая GL_BGRA-гвард), 30-с
    смоук до меню чистый; SIGTERM-выход по timeout печатает стектрейс
    в usleep (ProcessFrame) — это штатный graceful-quit, не краш
- [ ] **M5 (фаза D «Уровень») — В РАБОТЕ (2026-10-12, вечер)**: автостарт новой игры
  работает, Затон грузится в браузере, к ~240 с актёр в игре (HUD, болт в руках) —
  но сцена тёмная из-за MRT-дропов (разбор ниже). Артефакты первого прогона:
  `~/dev/cop/_m5/` (console.log + 4 скриншота + gltest2.html)
  - **сделано (M5-1)**: `?newgame=1`/`?args=` в index.html (Module.arguments +=
    `-start server(all/single/alife/new)` — та же строка, что в нативе M0.5);
    `?shot=` список через запятую (`CANVAS_SHOT[at_Ns]:...`); `?keys=down@sec:w`
    (синтетические KeyboardEvent key/code/keyCode, пузырь до document — там слушает
    SDL-клей); `?mem=N` — лог heap СЕЙЧАС СЛОМАН (Module.HEAPU8 не виден из
    index.html — нужен fallback на глобальный `wasmMemory`); probe_chrome.sh пишет
    артефакты в OUT_DIR (console.log + все shot_*.png; раньше trap их удалял)
  - **первый прогон** (`?newgame=1&mem=15&shot=90,240,480,780`, 900 с): «New game is
    successfully created!» (6464 спавнов Lua 5.1 в браузере), фазы уровня, HOM,
    к 240 с — в игре: HUD, миникарта, болт; кадры идут, 0 FATAL/SIGSEGV. В конце
    упал GPU-процесс Chrome (SwiftShader, 0xC0000005) — движок продолжил; следить
  - **ГЛАВНЫЙ БЛОКЕР — MRT**: «GL_INVALID_OPERATION: glDrawElements: Active draw
    buffers with missing fragment shader outputs» (тысячи) → WebGL2 дропает дрои:
    в desktop GL можно писать меньше SV_Target, чем активных аттачментов, в WebGL2 —
    ошибка. G-buffer вешает 3 цели (phase_scene_begin: u_setrt(rt_Position, rt_Normal,
    rt_Color); максимум в GL-бэкенде = 3, set_pass_targets), часть шейдеров пишет
    меньше. **ФИКС (спроектирован, не сделан)**: в essl::rewrite() (rgl_essl_web.h)
    для фрагментной стадии добавить недостающие dummy-выходы
    `layout(location=0..2) out vec4 xr_mrt_padN;` — вставить сразу после преамбулы
    (top-level гарантированно). Объявленный-но-не-пишущийся выход удовлетворяет
    проверке WebGL2; лишние выходы без аттачментов легальны в обоих направлениях
  - **текстуры**: «0x502 Invalid 2D texture» на 4 файлах (wpn_bolt, item_key_1,
    item_guitar, ui_actor_weapons) — ЛОЖНЫЕ срабатывания: glGetError не чистится и
    подхватывает стейл-ошибку от MRT-дроуов; texStorage2D с DXT5 при включённом
    WEBGL_compressed_texture_s3tc работает (проверено gltest2.html в headless Chrome;
    мой первый тест дал INVALID_ENUM, потому что расширение было не включено —
    emscripten включает ВСЕ расширения при создании контекста). Реальная проблема:
    water_sbumpvolume.dds — **3D-текстура DXT5, S3TC на TEXTURE_3D запрещён в
    WebGL2**. ФИКС: под XR_PLATFORM_WEB конвертить сжатые 3D в RGBA8 (gli::convert
    из convert.hpp; assert `has_decoder || !compressed` — проверить CAP_DECODER_BIT
    у DXT5) в glTexture.cpp
  - **потоки**: блокера нет — hardware_concurrency()=1 на вебе → SpawnThreads создаёт
    0 воркеров, таски выполняются инлайн в TaskManager::Wait()
  - **клавиатура/pointer lock (M5-3, не начат)**: SDL2-порт emscripten слушает
    keydown/keyup на document; движок зовёт SDL_SetRelativeMouseMode
    (xr_input.cpp:640) → порт просит pointer lock, но Chrome требует transient
    activation → в index.html ловить pointerlockerror и re-request по клику на canvas
  - **инструменты** (в `~/dev`, НЕ в репо): `db_extract` — разбор .db-архивов X-Ray
    (чанки [u32 id|0x80000000][u32 size]; индекс-чанк id=1 сжат LZHUF — декодер
    движка LzHuf.cpp через shim-stdafx; entries: u16 size + 3×u32 + name + u32 ptr;
    compr==real → файл хранится как есть, иначе rtc_decompress); `glitest` — парсинг
    DDS через gli. Компиляция gli: `-IExternals/gli -IExternals/gli/external` (glm);
    тильда после `-I` в bash НЕ раскрывается. Текстуры лежат в resources.db3/db4
  - **M5-2 (2026-10-12, ночь) — сцена освещена, ходьба работает, 0 GL-ошибок**:
    - **MRT**: вместо dummy-выходов (пишут неопределённые значения поверх
      «нетронутых» целей G-buffer) — маскирование draw buffers под выходы программы:
      перед каждой отрисовкой на вебе `glDrawBuffers` = подключённые цели ∩
      SV_Target-выходы программы (`glGetFragDataLocation`, кэш по программе),
      после — восстановление (`CBackend::MaskDrawBuffersToOutputs`, e5c0a6d8e).
      Ровно поведение десктопа/D3D. Тысячи «missing fragment shader outputs» → 0
    - **3D DXT** (water_sbumpvolume): декодирование в RGBA8 через `gli::convert`
      под XR_PLATFORM_WEB (c6192584b). Ложные «0x502 Invalid 2D texture» ушли вместе
      с MRT-ошибками (стейл glGetError)
    - **waterd**: `in int2 v_vert_uv` при подаче SHORT2 через glVertexAttribPointer —
      WebGL2 дропает дроу («Vertex shader input type does not match»). Фикс в шейдере:
      float-вход + `int2()` (как в v_water.h; 1b5bcf422). Правка и в движковом
      `res/gamedata`, и в `~/dev/cop/gamedata` (мерж-копия) → files.json+pack.bin
      перегенерированы парой
    - **память**: `-sMAXIMUM_MEMORY=4GB` — по умолчанию рост останавливался на
      **2 ГБ**, а загруженный Затон занимает **2109 МБ** кучи (старая сборка упала бы
      по OOM); `-sINITIAL_MEMORY=64MB` (явный максимум отключает авто-размер)
    - **XRFS**: кэш мелких файлов был без вытеснения — **1.6 ГБ** JS-памяти на
      загрузке уровня; теперь LRU 256 МБ (`smallCacheBytes`). Итог по вкладке:
      куча 2.1 ГБ + pack 156 МБ + small 256 МБ + blocks 96 МБ ≈ 2.6 ГБ
    - **зонды**: `?mem=N` чинен (XRFS.memStats) + FPS по rAF (avg, худший кадр);
      `CHROME_EXTRA` в probe_chrome.sh (реальный GPU: `--use-angle=d3d11
      --ignore-gpu-blocklist`)
    - **замер на реальном GPU** (RTX 3070 Ti, headless Chrome/ANGLE D3D11, Lua 5.1
      интерпретатор, стартовая зона Затона): **~135–140 FPS**, единичные хитчи до
      320 мс (стриминг). Риск R1 (медленный Lua) на старте не проявился — перепроверить
      в бою/у скоплений NPC
    - **сценарий автопрогона** до игры: `?newgame=1&mem=10&keys=down@210:space;
      up@210.3:space;down@222:space;up@222.3:space;down@232:escape;up@232.3:escape;
      down@260:w;up@320:w` — 1-й пробел = «нажмите клавишу», дальше пропуск интро.
      Скриншоты прогона: `~/dev/cop/_m5/run4/`
    - субмодули luabind/xrLuaFix/GameSpy форкнуты (underherzen/*), ветки
      `wasm-port` запушены, `.gitmodules` указывает на форки (2071fad97)
  - **ДАЛЬШЕ (M5-3)**: 1) 5-мин ходьба без краха/OOM (критерий фазы D);
    2) pointer lock в index.html (ловить `pointerlockerror`, re-request по клику) —
    проверять только в обычном (не headless) Chrome; 3) сейвы: IDBFS для
    `$app_data_root$` (сейчас запись в память XRFS — теряется на перезагрузке);
    4) хитчи 300+ мс — профиль: sync-XHR блоков на главном потоке?
