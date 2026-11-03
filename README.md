# windows-game-to-mac

Claude Code skill for running Windows PC games on Apple Silicon Macs as native-looking `.app` bundles
with a free Wine stack (WineCX or Gcenx Wine, DXMT, GStreamer), including Windows Steam inside Wine.
The skill itself is in English; this README is in Russian.

Скилл Claude Code: как запустить Windows-игру на Маке с Apple Silicon в виде нормального приложения
`.app` на бесплатном стеке Wine. Он основан на трёх реальных проектах:
- Northgard Battlegrounds — работает;
- Test Drive Unlimited 2 — упёрлись в защиту SecuROM;
- Three Kingdoms: Fate of the Dragon — ремастер через свою DLL.

## Установка

В Claude Code 2.1.275 или новее:

```
/plugin install windows-game-to-mac --marketplace i7ok/Macdows-skills
```

То же в два шага, для любой версии:

```
/plugin marketplace add i7ok/Macdows-skills
/plugin install windows-game-to-mac@macdows
```

Нужен `git`. На Маке он ставится вместе с Xcode Command Line Tools: `xcode-select --install`.

Обновления: `/plugin marketplace update macdows`. Или включите автообновление один раз:
`/plugin` → Marketplaces → macdows → Enable auto-update.

## Как пользоваться

Скилл включается сам, когда просишь, например, «перенеси игру X с Windows на Мак», «запусти виндовую
игру на маке» или «в игре не работают клики». Claude сначала проверит, пойдёт ли игра вообще (DirectX,
движок, защита от копирования, Steam), потом соберёт комплект: установщик, лаунчер, документацию.

Шаблон комплекта `~/Macdows/Northgard-Mac` в репозиторий не входит: в нём файлы игры и сборки Wine.
Без него Claude соберёт комплект по `reference/app-bundle.md`.

## Что внутри

| Путь | Что |
|---|---|
| `SKILL.md` | Порядок работы, правила, таблица «симптом → причина → исправление» |
| `reference/triage.md` | Как понять, пойдёт ли игра и каким путём (DirectX, движок, защита, Steam) |
| `reference/stack.md` | Компоненты: WineCX 24 (Sikarugir), Gcenx Wine 11, DXMT, GStreamer; версии и особенности |
| `reference/app-bundle.md` | Как устроены комплект и `.app`: установщик, лаунчер, окружение Wine, «сторож» Steam |
| `reference/steam-in-wine.md` | Steam для Windows внутри Wine: установка, вход, ошибка транспорта, безопасное закрытие |
| `reference/unity.md` | Игры на Unity: Direct3D 11, логи, клики и прокрутка в Unity 6 (прослойка ngfix) |
| `reference/input-window-quit.md` | Клавиатура Мака в Wine, ⌘Q, выход через Dock, фокус, курсор |
| `reference/dll-and-patching.md` | Как менять поведение без исходников: DLL-прослойки, патчи байтов |
| `reference/diagnostics.md` | Логи, трассировки Wine, утилиты, метод отладки, ловушки оболочки |
| `reference/case-studies.md` | Три проекта: что ломалось и как починили |
| `scripts/win32/` | Утилиты для окон Windows внутри Wine (исходники и готовые `.exe`) |
| `scripts/pe/` | Анализ Windows-бинарников на Python (импорты, вызовы, ссылки на строки) |
| `scripts/wine/` | Запуск утилит в окружении игры, патч `nsiproxy.so` для Steam |
| `assets/ngfix/` | Прослойка ввода для игр на Unity 6 (клики, прокрутка, ⌘Q) |
| `.claude-plugin/` | Манифесты плагина и маркетплейса `macdows` |

## Как обновлять скилл

Скилл лежит прямо в корне репозитория: `SKILL.md` и папки рядом с ним. Рабочая копия автора —
`~/Macdows/.claude/skills/windows-game-to-mac`, там это проектный скилл папки `~/Macdows`. После правок —
коммит и `git push`. Поля `version` в манифесте нет, поэтому каждый коммит
становится новой версией для всех, кто обновит маркетплейс.
