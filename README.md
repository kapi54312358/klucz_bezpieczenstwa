# Klucz bezpieczeństwa — RP2040

Urządzenie USB, które po wyzwoleniu wpisuje hasło do menedżera haseł, udając klawiaturę (USB HID). Zbudowane na płytce RP2040 w Pico SDK, w C++.

## Jak poszedł projekt

Zaczynałem z elektroniką od zera i C++ z poziomu matury rozszerzonej. Etapy 1–3 poszły gładko: dioda RGB z samodzielnie policzonymi rezystorami, przełącznik z rezystorem podciągającym i wykrywaniem zbocza, a potem klawiatura USB HID wpisująca hasło znak po znaku wraz z Enterem. Etap 4 — komunikacja UART z czytnikiem linii papilarnych — również się udał, łącznie z własną implementacją protokołu ramek Grow/ZFM: budowanie pakietów, sumy kontrolne i synchronizacja strumienia. Etap 5 zatrzymał się na wadliwym egzemplarzu czujnika, który odrzuca każdą komendę zapisu wzorca, mimo że ramki i sumy kontrolne są poprawne w obie strony. Klucz działa dziś z przełącznikiem jako wyzwalaczem, a kod obsługi czujnika jest gotowy i czeka na sprawny moduł.

## Zawartość

| Katalog | Etap | Co zawiera |
|---|---|---|
| `klucz-bezpieczenstwa-1` | 1 | Dioda RGB — prawo Ohma, napięcie przewodzenia, wspólna anoda |
| `klucz-bezpieczenstwa-2` | 2 | Przełącznik — rezystor podciągający, wykrywanie zbocza |
| `klucz-bezpieczenstwa-3` | 3, 6, 7 | **Działający klucz** — USB HID, wpisywanie hasła, sygnalizacja diodą |
| `klucz-bezpieczenstwa-4` | 4, 5 | Protokół czytnika linii papilarnych i diagnostyka po USB CDC |

## Dokumentacja

- [PLAN.md](PLAN.md) — plan projektu z odhaczonym postępem i materiałami do nauki
- [Klucz-RP2040-stan-projektu.md](Klucz-RP2040-stan-projektu.md) — okablowanie, rozkład pinów, diagnoza czujnika, pułapki

## Sprzęt

Płytka RP2040 w formacie Pro Micro (`HW-467A8`), dioda LED RGB 5 mm ze wspólną anodą, przełącznik suwakowy, czytnik linii papilarnych R558S, rezystory 100 Ω i 200 Ω.

## Budowanie

Projekty korzystają z Pico SDK 2.3.0 i wtyczki Raspberry Pi Pico do VS Code. Otwórz wybrany katalog i użyj przycisku **Compile**, albo z linii poleceń:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

Wgrywanie: przytrzymaj **BOOT**, wepnij USB, przeciągnij plik `.uf2` na dysk `RPI-RP2`.

## Hasło

Hasło nie znajduje się w repozytorium. Skopiuj `klucz-bezpieczenstwa-3/haslo_przyklad.h` jako `haslo_prywatne.h` i wpisz własne — plik jest objęty `.gitignore`.
