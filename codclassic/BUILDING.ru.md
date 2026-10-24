# Call of Duty Classic на Switch: сборка и установка

## Что на SD-карте

```
sdmc:/switch/codclassic/
    codclassic.nro
    codclassic.toml          необязательно: настройки (cvars), читаются при запуске
    game_root/               файлы игры из пакета XBLA (базовая версия 1.0)
        default.xex
        x360shared.xdl  game_sp.xdl  cgame_sp.xdl  ui_sp.xdl
        main/*.pk3
        shaders/
        splash/
    cache_partition/         создаётся сама: конфиг и сохранения игры (cache:)
```

MP-модули (cod_mp.xex, *_mp.xdl) пока не нужны: в порт входит только одиночная игра.

Запуск: из Homebrew Menu в режиме title takeover (зажать R при запуске любой игры) или через форвардер.
Из альбома приложение не получит достаточно памяти.

## Как собиралось (облачный контейнер Ubuntu 24.04, 2 ядра, 7 ГБ)

1. devkitA64 + libnx + портлибы: слои образа `devkitpro/devkita64` с Docker Hub, распакованные в `/opt/devkitpro`
   (pkg.devkitpro.org из контейнера закрыт).
2. Основа: github.com/StevensND/nfsmw-nx (`sdk/` = ReXGlue v0.10.0 со слоем Horizon).
   `tools/fetch_thirdparty.py` + пустые `.git/HEAD` в `sdk/thirdparty/*`, иначе проверка подмодулей и git describe SDL падают.
3. NVK: danfromtico/mesa-switch @ 1a8c1a66d6f + `mesa/mesa-switch-nfsmw.patch`, шаги `build-switch.sh` без Docker:
   apt llvm-18/libclc-18/libllvmspirvlib-18, bindgen-cli 0.71.1 и cbindgen 0.29.4 (готовые бинарники с GitHub),
   Rust-крейты из `build-deps/mesa-crates.tar` в `subprojects/packagecache/` (crates.io из контейнера закрыт).
   SDK = `lib/libvulkan.a` + `include/vulkan`.
4. Хостовый rexglue: clang-20, `-march=x86-64-v2`.
5. `rexglue codegen codclassic_manifest.toml` → `generated/` (5 модулей).
6. Кросс-сборка: `switch-devkitA64.cmake`, `REXGLUE_SWITCH_NVK_SDK=<mesa-sdk>/opt/devkitpro/portlibs/switch`.

## Правки SDK для этого порта (sdk-patches/nfsmw-nx-sdk-codclassic.patch)

- `dynlib_switch.cpp` + `dynlib.h`: реестр статических библиотек `RegisterStaticLibrary()`; `Load()` ищет модуль по
  `shared_lib_name` в реестре, а не на диске (на Horizon нет dlopen).
- шаблон `dll_targets_cmake.inja`: на Switch модули `.xdl` собираются как STATIC; зависимость от `*_codegen` только если цель есть.
- из прежнего патча: поиск модуля по имени файла (`kernel_state.cpp`), адрес thunk в ошибке ordinal,
  lr/r3/r4 в FATAL `function_dispatcher.cpp`, `lib*.so` в `dynlib_posix.cpp`.

## Приложение (app/)

- `CMakeLists.txt`: на Switch точки входа каждого модуля переименовываются
  (`ReXModule_Register` → `ReXModule_Register__codclassic_<mod>`), модули линкуются в NRO.
- `src/static_modules_switch.cpp`: публикует их в реестре до `main()`.
- `src/codclassic_app.h`: `game_root/` рядом с исполняемым файлом, `gpu_plugin=xenos` и `license_mask=1` по умолчанию,
  `cache:` → `cache_partition/`.
