> Developer notes (in Russian) written while making the port. Folder names refer to the author's working
> folder, not to this repository; log numbers refer to console logs that are not published.

# Call of Duty Classic (XBLA, TID 58410957): статическая рекомпиляция

Состояние на 21.10.2026:
- PC/Linux-сборка на ReXGlue v0.10.0 доходит до геймплея первой миссии (Camp Toccoa).
- Модули SP: default.xex (0x82000000), x360shared.xdl (0x8A000000), ui_sp.xdl (0x89000000),
  game_sp.xdl (0x87000000), cgame_sp.xdl (0x85000000). MP-модули (cod_mp.xex, *_mp.xdl) ещё не трогали.
- Используется базовая версия 1.0. TU1 (в extracted/tu1) хранится как дельта-патчи, а ReXGlue их не применяет.

## Содержимое
- proj/: манифест, подсказки функций (*_hints.toml), CMakeLists, src/.
  - src/codclassic_app.h: монтирует cache: (там игра хранит конфиг и сохранения) в user_data/cache_partition.
  - src/guest_imports_*.cpp: генерирует scripts/gen_thunks.py. Это переходники для импортов между
    модулями игры (x360shared exports by ordinal), которые разрешаются во время работы. Контекст сохраняется
    через указатель без __restrict, потому что загрузка модуля исполняет его DllMain на том же контексте.
  - gen_thunks.py читает соответствие ordinal → thunk из вывода codegen (cg4.out, копия в proj/ordinal_map.txt).
- sdk-patches/rexglue-v0.10.0-codclassic.patch: правки SDK.
  - Нужны: FindRecompiledModule ищет модуль по имени файла (kernel_state.cpp); dynlib_posix
    добавляет lib*.so; phase_register печатает адрес thunk; function_dispatcher печатает lr и r3/r4 при FATAL.
  - Временный отладочный код, убрать: MNKDBG в mnk_input_driver.cpp, CODDBG (verbose XAM log) в xam_*.cpp.
- scripts/: инструменты тестового стенда (Xvfb + lavapipe, xdotool, автоисправление недостающих функций).

## Флаги запуска
--gpu_plugin=xenos --mnk_mode=true --license_mask=1 (полная лицензия) --game_data_root=<папка с default.xex, main/*.pk3, shaders/, splash/>
Звук в контейнере: PulseAudio с null-sink. Голый ALSA null крутит поток SDL на 100% CPU.

## Следующий этап: Switch (Lite, Atmosphere, файлы через DBI)
Основа: github.com/StevensND/nfsmw-nx (sdk/ = ReXGlue с Horizon-слоем, mesa-switch + патч, docs/porting-another-game.md).
Нужны devkitPro (devkitA64, libnx). Хосты devkitpro.org в первой сессии были заблокированы.

## Switch: сделано 21.10.2026 (вторая сессия)
- Собран первый codclassic.nro: см. switch/SWITCH.md (сборка, раскладка на SD, правки SDK).
- Модули .xdl на Switch линкуются статически и находятся через реестр RegisterStaticLibrary (на Horizon нет dlopen).
- Графика пока через эмуляцию Xenos: по опыту nfsmw-nx это даст первые кадры, но медленно.
- Следующие шаги: запуск на Switch Lite и лог (sdmc:/switch/codclassic/), затем замер скорости,
  затем решать про нативный рендерер и предтрансляцию шейдеров (как в nfsmw-nx).
- build-deps/: крейты для mesa, bindgen 0.71.1 и cbindgen 0.29.4 (из контейнера crates.io и devkitpro.org закрыты).
- extracted/base/main/pak.pk3 собран из pak.pk3.part00+01 (проверен, 12616 файлов). Куски и chunks/c24 перенесены в ../_to_delete/.

## Первый запуск на консоли (21.10.2026)
- Из альбома: под гостевую память 16 МБ -> bad_alloc. Запускать только через удержание R на игре.
- Через R: x360shared.xdl вернул PPCImageConfig главного модуля (одинаковые глобальные имена слились при статической линковке).
  Исправлено: app/cmake/localize_module.py делает внутренние символы каждого модуля локальными (ld -r + objcopy).

## 22.10.2026
- Вылет на пулемётчике: косвенный вызов 0x820C7E58 (функция не найдена анализом). scripts/fill_gaps.py + prune_gaps.py объявляют
  непокрытый код функциями (<mod>_gaps.toml, 292 шт.), прежние 14479 функций не меняются. После codegen снова direct_calls.py.
- «Продолжить»: игра пишет s:\main\save\autosave\training.svg и cache:\main\cache\last.svg, а из меню ищет
  autosave\nomap.svg и s:\main\save\cache\last.svg. Нужны файлы с SD (sd_saves/), чтобы понять расхождение.
- ELF каждой отданной сборки хранить для расшифровки адресов из rex_crash.log.
- Регрессия «вылет на заставке»: подсказки попали на __savegprlr_14/__restgprlr_14 (спецпомощники codegen).
  scripts/prune_gaps_strict.py оставляет только подсказки, при которых C++ всех прежних функций не меняется (281 шт.).
- Сохранения на SD целые: s:\main\save\cache\last.svg = указатель "autosave/training.svg", появился после запуска 011.

## 22.10.2026, вечер
- Случайные вылеты в игре (004, 017, 023) все в sub_8218C578: копирование индексов поверхности в динамический буфер,
  указатель источника (*(surf+32)) или приёмника (*(0x82FA4204)+2*count) смотрит в невыделенную память.
  src/codclassic_guards.cpp: перехват пропускает такую поверхность и пишет "guard sub_8218C578" в лог.
- «Продолжить» = open resume_popmenu (ui/main_360.menu), A -> exec loadgame_continue. Подозрение: окно открыто, но не рисуется.
- Com_Printf = sub_8207C390 (в retail вывод вырезан): перехват пишет сообщения консоли игры в лог с префиксом [cod].
  Com_Error = sub_8207D580, va() = sub_8208C4A0, vsnprintf = sub_821EFB00.
- 024: после «Продолжить» игра жива (31 fps, 136 draw/кадр как в меню), B не помогает, файлов не открывает.
- Глюки текстур после сборки с предохранителем: вероятно ложные срабатывания (учёт страниц по зеркалам физ. памяти).
  Предохранитель переписан: проверка как в обработчике исключений (RexGmQueryProtect), при плохом источнике
  пишет вырожденные треугольники и ведёт учёт пакета вместо пропуска.

## 22.10.2026, ночь
- Вылет на 2-й миссии: Horizon 0xCE01 (ResourceExhausted) в RexGmProtect -> страница не вернулась -> запись в неё.
  Исправление в SDK: при 0xCE01 сбрасываются все отображения кусков в зеркалах (кроме текущего) и операция повторяется.
- «Продолжить»: loadgame_continue -> g_lastSaveGame="cache\last.svg" -> sub_82094BB8 читает полную копию -> "spmap cache\last.svg"
  -> sub_820944A8 добавляет "save/" -> читает 23-байтный указатель s:\main\save\cache\last.svg -> "Can't find map maps/ave/training.svg".
  Исправление: перехват Q_stricmpn (sub_8208C180) на двух проверках префикса (lr 0x820945B8/0x820945D4) для имён cache\...
- Строки игры: rexglue codegen с REXGLUE_DUMP_IMAGE=<dir> выгружает образы модулей.
- Зависание на брифинге pathfinder: взаимоблокировка во входе обработчика исключений (все 8 слотов заняты,
  поток ждал слот внутри состояния исключения ядра, владельцы не могли выйти через RexResumeTrap).
  Исправлено: при нехватке слотов возврат к инструкции без обработки (повтор).
- 031: после фикса слотов - не взаимоблокировка, а ливлок: ~300 сбросов/с, ~1800 подкачек/с, 0-2 fps,
  лимит исчерпан даже при 8 МБ отображённого -> кончаются блоки памяти ядра (куски по странице).
  Исправление: новые куски памяти выравниваются до 1 МБ; в отчёте профилировщика "trozos N".

## 2026-10-22 — сборка affc6c44a664
- pathfinder загрузился после 1 МБ гранулярности (18f364c4b3e3).
- Вылет в миссии (журнал 032): запись по 0x301047D7 в __imp__sub_8218C578, вызов через охранника (хвостовой вызов).
  Цикл копирования идёт n/6 раз через bdnz: при n<6 счётчик 0 -> 2^32 итераций, разбег по памяти.
  Охранник теперь отбрасывает n<6 и нечётные src/dst.
- Иконка: постер Call of Duty (2003), присланный пользователем (icon/icon_poster_2003.jpg, обрезка 264x264 сверху+4).
- Производительность (журнал 032, миссия): ~18-24 к/с. Main XThread 101%, из них ~50% — ожидание GPU
  (sub_82211CB0/sub_8221F800 крутятся на заборе D3D). GPU Commands 46%: kickoff/fence wait/sleep.
  Каждый кадр "nvkmd-switch: channel 2 hit native submission pressure (0xd5c = LibnxNvidia Timeout)":
  ~924 GPFIFO-записей на отправку, кольцо libnx 0x800 — ждём выполнения прошлого кадра. Нужен A/B-тест (L+R+ZL).
- NSP-обёртка для DBI: сборка nx-hbloader-forwarder отклонена системой разрешений сессии; не делалось.

## 2026-10-22 — сборка 5f145530a142 (смерть игрока)
- Журнал 035: [FATAL] Call to invalid or unregistered function 0x8707D410 (lr=0x87096484) — qsort в game_sp
  вызывает компаратор через bctrl. Компаратор не был функцией: анализатор сделал отдельный sub_8707D48C из его
  второй ветки (bge -> D48C), а начало D410 осталось непокрытым; prune_gaps_strict выкидывал подсказку,
  потому что D48C «исчезал».
- scripts/absorb_gaps.py: принимает подсказку, если оригинал только поглощается новой функцией, стоящей прямо
  перед ним, и на его имя никто не ссылается. Добавлены: default 0x821357A8, 0x821357F0; game_sp 0x87049FA8,
  0x8707D410, 0x870968F8; ui_sp 0x890316A8. Codegen обязательно с --ignore-stamp, потом direct_calls.py по всем модулям.

## 2026-10-23 — сборка 766071bc35c6
- Журнал 036: снова запись в __imp__sub_8218C578 (dst 0x55F379E0, обычная куча, хост не отображён), охранник
  пропустил. Охранник: для невиртуальных-физических куч теперь проверяет метаданные кучи (commit + PAGE write
  для записи), а READ в RexGm вне физической памяти для записи считается недоступным.
- A/B (журнал 036, тяжёлая сцена 11-15 к/с, GPU 845-920 МГц): «sin dibujados» дало только 20 к/с.
  Main XThread ~65% крутится в ожидании CP (sub_82211CB0/sub_8221F800/sub_82230A18 + savegprlr_29).
  GPU Commands без отрисовки 80%: 25% — spdlog flush на SD из колбэка Vulkan (предупреждение
  "submission pressure" каждый кадр, flush_level=warn); WAIT_REG_MEM sleep ~10%; SharedMemory::RequestRanges ->
  UploadRanges -> Protect на каждый draw; ~1400 draws/кадр, 6.5 МБ загрузки/кадр.
- SDK: колбэк Vulkan ограничивает повторы (первые 8, затем 2^n-е) — коммит в codclassic-switch.
- Идея: русская версия. Шрифты Q3: fonts/fontImage_N.dat (256 глифов x 80 байт + 72) + DDS; строки в
  localized_english_pak.pk3/localizedstrings/english/*.str (57 файлов). Нужны кириллические глифы в слотах
  CP1251 и переведённые .str.

## 2026-10-23 — сборка 8d1734d14f12
- Журнал 037: охранник sub_8218C578 (сборка 766071) отбраковывал rec 0x831Cxxxx (куча образа: метаданные
  не «commit») — 262145 пропущенных поверхностей, сломанная геометрия во 2-й миссии. Проверка метаданных
  теперь только для адресов < 0x80000000.
- Журнал 037: "COM ERROR - G_LoadGame: savegame 'cache\last.svg' not found" -> Com_Error вызвал longjmp,
  который перекомпилирован как обычный код и просто вернулся; дальше case trap'а читал 0x4. В default_hints.toml:
  setjmp_address = 0x821F0260, longjmp_address = 0x821EFF40 (кодоген выдаёт ppc_setjmp/ppc_longjmp).
- last.svg открылся как каталог (0xC00000BA) сразу после перезаписи (доступ 0x40110000, disp 5). SDK: VFS
  исправляет атрибут, если на хосте обычный файл; [svg] и ошибки NtCreateFile теперь пишут create_options.
- Русификация: loc/make_fonts.py (кириллица CP1251 в fontImage_*.dat + страницы fontImage_ru_*.dds), переводы
  в loc/ru/*.txt. Шрифты из pak.pk3 (pak_x360 > pak > main/ > localized). План: вариант — подменный pak_x360.pk3.

## 2026-10-23 — русификация (codclassic-port/localization)
- russian/ и english/ повторяют структуру SD: switch/codclassic/game_root/main/pak_x360.pk3.
- Русский pak_x360 = оригинал + fonts/fontImage_{12,16,18,24,30,32}.dat (кириллица CP1251 в слотах C0-FF,
  A8, B8, B9, а также … – — “ ”) + fonts/fontImage_ru_*.dds (DXT5 256x256) + localizedstrings/english/*.str
  (2547 строк переведено, CP1251, ключ LANG_ENGLISH сохранён). Не переводились credit.str и translationonly.str
  (там только убраны латинские буквы с диакритикой, их байты теперь кириллица).
- Исходники: localization/src (make_fonts.py, build_ru.py, ru/*.txt — переводы "КЛЮЧ<TAB>текст").
- Не проверено на консоли: отображение кириллицы движком (возможна фильтрация символов >0x7F в части путей).
- Буква «я» (0xFF) выводилась квадратом: движок не регистрирует глиф 255. «я» перенесена в слот 0x9F (шрифт и строки).

## 2026-10-23 — сборка cc9f5d6ea620: без 4x MSAA / предикатного тайлинга
- A/B: «sin dibujados» 20 -> 40 к/с. Каждый кадр: 3 resolve по 1/3 экрана, тесак ~0.3 экрана на draw.
- Игра рисует сцену в 4x MSAA и делит экран на 3 полосы 1280x256 (= ровно 10 МБ EDRAM), D3D повторяет
  все вызовы на каждую полосу: x3 вызовов и вершин, x4 сэмплов.
  sub_82191730 пишет прямоугольники полос в 0x82FAE8B4; sub_82191138 создаёт RT/глубину через
  sub_82216F38(w,h,fmt,multisample=2,...) (lr 0x82191238/0x8219126C); sub_8221BE40 = BeginTiling(dev,0,3,rects,...)
  из sub_82194120/sub_82194458.
- codclassic_perf.cpp: полосы -> один прямоугольник на весь экран, multisample 0, BeginTiling count 1.
  Вернуть оригинал: codclassic_msaa = true в codclassic.toml.
- Слот 0x9F для «я» тоже не сработал (игра меняет байты 0x80-0x9F). «я» теперь в слоте 0x60 (символ `, в строках игры не встречается).


## Сборка ec9d45b2c75f — 4-й уровень (dawnville)

- Лог 040: `COM ERROR - Max number of disconnected paths exceeded` при загрузке dawnville. Пул разрывов путей ИИ — 2048 записей по 8 байт.
  - Пул ищется по адресу 0x872F5560+4000. Инициализируют его sub_8708F348 и встроенная копия в sub_870927D8, берёт запись sub_870917B0.
  - Хуки в app/src/codclassic_game_sp_hooks.cpp (собирается в target codclassic_game_sp): пул на 8192 записи, свободный список пополняется пачками по 2048, когда исходный кончается.
  - В лог пишутся сущности, разорвавшие 64 и больше связей.
- Лог 039: `CreateThread failed C0000017`, затем `AllocFixed ... respaldo 538 MB` и вылет.
  - Причина: завершённые pthread никто не join-ил, на Switch каждый держал стек 16 МБ.
  - SDK threading_posix.cpp: поток при выходе ставит себя в очередь, Thread::Create её join-ит.
- SDK host_path_entry.cpp: rename поверх существующего файла (FAT на Switch не заменяет), сохранение temp.psw → current.psw.
- direct_calls.py для game_sp запускать со списком, где есть хуки 87xxxxxx: sub_8708F348, sub_870927D8, sub_870917B0, sub_87091AA0.

## Сборка deede4ee30e7

- Лог 043:
  - Dawnville загрузилась, разрывов путей было 3500, пул расширился до 4096.
  - Перезагрузка из сейва упала с `Hunk_ReallocateTempMemory: failed on 72213715`: hunk 128 МБ превышен на 1 байт.
  - Исправление: hook Cvar_Get (sub_8207FA78) для "com_hunkMegs" (0x82004738) ставит integer = codclassic_hunk_megs (160).
- Зависание после COM ERROR: join завершённых потоков из Thread::Create мог дать deadlock. Теперь join делает отдельный поток-reaper (threading_posix.cpp).
- Лог 042, `GAME_SAVE_INSUFFICIENT_FREE_DISK`: rename temp.psw → current.psw делается через открытый handle, а Horizon не переименовывает открытые файлы.
  - Исправление: XFile::Rename закрывает host-файл и открывает его заново (File::ReleaseHostHandle / ReacquireHostHandle).
- Лог 042, crash в sub_8218C578 (запись в 0x90B3F000): guard дополнительно требует RexGmShadowFor != NULL для не-физической памяти.

## Сборка 0bad6dd06fda

- Логи 048/049, «после второй смерти не продолжить»: зависание или crash read 0x0700B04C.
  - Причина: загрузчик сейва (sub_8704B750) восстанавливает 3500 разрывов путей своей inline-копией взятия записи, мимо hook sub_870917B0 и без проверки пустого списка. Список на 2048 записей кончался, после чего забиралась сама голова, и список портился.
  - Исправление: сразу после инициализаторов весь пул (8192 записи) вшивается в свободный список (SpliceRemaining).
- Crash в sub_8218C578 после того, как guard уже пропустил запись (лог 048): страница теряла отображение между проверкой и копированием.
  - Исправление: функция переписана в hook, record/surface/индексы читаются через shadow-алиас (RexGmShadowFor), который не падает.
- Производительность (rex_perfil, dawnville, 15–26 fps):
  - Main XThread загружен на 100%, но около 35–40% этого времени — спин-ожидание GPU в D3D: sub_82211CB0, sub_8221F800, sub_82230A18. Игра ждёт, когда CP продвинет указатель чтения ring buffer.
  - Поток GPU Commands примерно на 47% стоит в svcWaitSynchronization: ждёт fence хост-GPU, в логах есть «native submission pressure».
  - Около 190 отрисовок на кадр. Узкое место — хост-GPU и последовательное ожидание CPU→GPU.
