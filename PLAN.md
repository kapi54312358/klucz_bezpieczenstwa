# Klucz bezpieczeństwa na RP2040 — plan nauki i budowy

## Postęp (stan na 17.08.2026)

- **Etap 0 — zrobione.** Płytka rozpoznana: klon Pro Micro `HW-467A8`, wyprowadza GPIO10–13, 18, 24, 25. `5V` to trzeci pad od USB w prawej kolumnie.
- **Etap 1 — zrobione.** Dioda RGB na 5 V, czerwona na GP3, zielona na GP2. Mieszanie kolorów i PWM jeszcze nie.
- **Etap 1.5 — pominięte.** Woltomierz z ADC niezbudowany. Wróć do tego, jeśli w Etapie 4 zabraknie narzędzia do pomiarów.
- **Etap 2 — zrobione.** Suwak na GP4 z `gpio_pull_up()`, wykrywanie zbocza w `switch_triggered()`.
- **Etap 3 — zrobione.** Projekt `klucz_b3`: TinyUSB HID, wpisywanie hasła znak po znaku z Enterem na końcu, sygnalizacja diodą.
- **Etap 4 — zrobione.** Projekt `klucz4`: UART 57600 8N1 na GP0/GP1, czujnik odpowiada. Gotowa funkcja `wyslij_komende()` buduje ramki ZFM i odbiera odpowiedzi o zmiennej długości. Potwierdzone `VerifyPassword` i `TemplateCount`.
- **Etap 5 — zablokowany przez wadliwy czujnik.** Kod rejestracji i rozpoznawania jest napisany i przetestowany (`klucz4`), ale egzemplarz R558S odrzuca **każdą** komendę utrwalającą wzorzec:
  - `Store 0x06` → `0x35` (kod spoza dokumentacji)
  - `UpChar 0x08` → `0x01` (nieznana komenda)
  - `AutoEnroll 0x31` → `0x25` / `0x01`

  Działa natomiast wszystko inne: `VerifyPassword`, `ReadSysPara`, `TemplateCount`, `DeleteAll`, `GetImage`, `Image2Tz`, `RegModel`, `CheckSensor`, `ReadIndexTable`, `Match`. Ramki i sumy kontrolne poprawne w obie strony. Pole „system identifier code" zwraca `0x0000` zamiast wymaganego przez specyfikację ZFM `0x0009` — przesłanka, że firmware nie jest oryginalny. **Czujnik do reklamacji.**

- **Etap 6 — zrobione w zakresie możliwym bez czujnika.** `klucz_b3` jest działającym kluczem: suwak wyzwala wpisanie hasła przez USB HID, dioda sygnalizuje stan. Po wymianie czujnika wystarczy przenieść warstwę protokołu z `klucz4` i podmienić wyzwalacz z suwaka na palec — reszta zostaje bez zmian. Uwaga na później: odczyty UART trzeba wtedy przerobić na nieblokujące, inaczej zaduszą `tud_task()`.

- **Etap 8 — obudowa z druku 3D.** Do zaprojektowania po wyborze wyzwalacza: okienko na diodę, dostęp do suwaka, gniazdo USB-C, ewentualnie miejsce na czytnik RFID. Decyzja o czytniku musi zapaść **przed** projektowaniem obudowy.

- **Wyzwalacz zamiast czujnika palca** (analiza 19.08.2026): magnes odpada, bo otwiera go dowolny magnes. Rekomendacja: **RC522 RFID ok. 12 zł z kartą i brelokiem** — tanio, daje token fizyczny i uczy SPI. Uwaga: karty płatnicze często losują UID przy każdym zbliżeniu, więc trzeba użyć karty z zestawu. Opcja zerokosztowa: sekretny wzór przestawień suwaka. Żadna z opcji nie zwiększa realnego bezpieczeństwa, bo flash RP2040 i tak da się zrzucić.

- **Etap 7 v1 — zrobione.** Hasło wyprowadzone do `haslo_prywatne.h`, plik objęty `.gitignore`, wzorzec w `haslo_przyklad.h`. Kolejny krok, gdy będzie ochota: hasło w ostatnim sektorze flash, ustawiane komendą przez USB CDC.

Do zrobienia przed Etapem 4: przenieść hasło do `haslo_prywatne.h` i dopisać plik do `.gitignore`.

## Context

Cel: urządzenie USB, które po przyłożeniu palca do czytnika R558S wpisuje hasło do menedżera haseł (płytka udaje klawiaturę USB HID). Prawdziwym celem jest jednak **nauka elektrotechniki po drodze** — projekt jest pretekstem, więc plan jest ułożony tak, żeby każdy etap dokładał jedno nowe pojęcie i kończył się czymś, co widać i co da się zmierzyć multimetrem.

Punkt startowy:
- C++ znasz z poziomu matury rozszerzonej (`E:\CPP4`) — pętle, tablice, pliki. To wystarczy; brakującą częścią jest sprzęt, nie język.
- Masz już postawione środowisko: VS Code + wtyczka Raspberry Pi Pico, projekt `E:\Projekty\embedeb\haslo1` (Pico SDK 2.3.0). Zostajemy przy tym.
- Sprzęt: płytka stykowa, lutownica + goldpiny, przełącznik suwakowy, przewody, rezystory 20/47/100 Ω. **Multimetr jest niesprawny — planujemy bez niego.** Zamiast niego budujesz przyrząd pomiarowy z samego RP2040 (Etap 1.5).
- Hasło: zaczynamy od najprostszego, zabezpieczenia dokładamy etapami (Etap 7).

## Trzy fakty, które ustaliłem, zanim zacznieszcokolwiek podłączać

**1. Zielony i niebieski nie zaświecą się z 3,3 V.** Karta katalogowa Twojej diody: czerwony Vf = 2,0 V, **zielony i niebieski Vf = 3,5 V**, If = 25 mA. Rail 3V3 na płytce ma 3,3 V — to mniej niż napięcie przewodzenia. Żaden rezystor tego nie naprawi. Wspólną anodę trzeba wpiąć w **5 V (pin RAW/VBUS)**, a nie w 3V3.

To bezpieczne dla RP2040: gdy GPIO jest w stanie wysokim (3,3 V), na diodzie zostaje 5 − 3,3 = 1,7 V, czyli poniżej Vf → dioda nie świeci i prąd nie płynie do pinu.

**2. Czujnik R558S jest wyłącznie na 3,3 V.** Podanie 5 V na jego VCC prawdopodobnie go zabije. Komunikacja: UART, 57600 8N1, protokół Grow/ZFM (ramki `0xEF01`). Dopasowanie odcisku robi sam czujnik — RP2040 dostaje tylko „pasuje / nie pasuje + ID". Nie masz do czynienia z obrazem palca.

**3. `haslo1.cpp` się nie kompiluje** — `#include "hardware_pio.h"` (ma być `"hardware/pio.h"`) i `#include "tinyusb_device"` (to nazwa biblioteki CMake, nie nagłówek). Poprawimy w Etapie 3.

## Docelowe podłączenie

Anoda diody → **5 V (RAW/VBUS)**, katody → GPIO przez rezystor. Wspólna anoda = **logika odwrócona**: stan niski na pinie zapala kolor.

| Kolor | Vf | Rezystor | Prąd: (5 − Vf − 0,3) / R |
|---|---|---|---|
| Czerwony | 2,0 V | **200 Ω** (2× 100 Ω szeregowo) | ~13 mA |
| Zielony | 3,5 V | **100 Ω** | ~12 mA |
| Niebieski | 3,5 V | **100 Ω** | ~12 mA |

0,3 V to spadek na tranzystorze wyjściowym GPIO. Rezystory 20 Ω i 47 Ω zostają w pudełku — przy tym railu dałyby 25–60 mA, czyli powyżej i limitu diody (25 mA), i zalecanych 12 mA na pin RP2040.

Propozycja pinów (numery GPIO potwierdź w Etapie 0):

| GPIO | Do czego | Uwagi |
|---|---|---|
| GP0 | UART0 TX → **RX czujnika** | krzyżowo! |
| GP1 | UART0 RX → **TX czujnika** | krzyżowo! |
| GP2 | TOUCH/WAKEUP z czujnika | wejście |
| GP3 / GP4 / GP5 | katoda R / G / B | przez rezystory z tabeli |
| GP6 | przełącznik suwakowy → GND | pull-up wewnętrzny |
| 3V3 | VCC czujnika + 3.3VT (zasilanie dotyku) | **nigdy 5 V** |
| GND | masa czujnika + masa diody | wspólna masa jest obowiązkowa |

---

## Etapy

### Etap 0 — Rozpoznanie sprzętu — **ZROBIONE**

Goldpiny wlutowane, płytka w płytce stykowej, wariant płytki i pinout rozpoznane. Pomiary napięć odpadły (niesprawny multimetr) — wracasz do nich w Etapie 1.5, już własnym przyrządem.

### Etap 1 — Dioda RGB (2–3 wieczory) — to jest właściwa lekcja elektrotechniki

Uczysz się: prawo Ohma, napięcie przewodzenia Vf, ograniczanie prądu, rezystory szeregowo/równolegle, wspólna anoda vs katoda, GPIO jako źródło i jako odbiornik prądu.

Bez miernika ten etap robisz „na oko" — pomiary dorobisz w Etapie 1.5. To i tak dobra kolejność: najpierw zobacz, że działa, potem zmierz, dlaczego.

1. Zanim cokolwiek podłączysz — policz sam prądy z tabeli wyżej i zrozum, skąd się bierze wzór `I = (Uzas − Vf) / R`.
2. **Rozpoznanie nóżek bez przyrządów.** Najdłuższa nóżka to wspólna anoda → na 5 V (RAW/VBUS, wystarczy wpięte USB, płytka nie potrzebuje żadnego programu). Rezystor **200 Ω** jednym końcem do GND, drugim dotykaj kolejno pozostałych trzech nóżek i zapisz, która daje który kolor. 200 Ω jest bezpieczne dla wszystkich trzech (czerwony 15 mA, zielony i niebieski po ~7,5 mA — przygaszone, ale widoczne).
3. Sprawdź eksperymentalnie fakt #1: przepnij anodę z 5 V na 3V3 i zobacz, że zielony i niebieski gasną, a czerwony świeci dalej. To jest ta lekcja o Vf, tylko pokazana zamiast zmierzonej.
4. Docelowe rezystory: 200 Ω czerwony, 100 Ω zielony i niebieski.
5. Kod: miganie każdego koloru, potem PWM (`hardware/pwm.h`), potem mieszanie kolorów. Ustaw `gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA)`.
6. Zwróć uwagę: niebieski ma 20 mcd, zielony 800 mcd — na oko zielony zmiażdży resztę. Wyrównanie jasności robi się właśnie PWM-em, nie rezystorem.

Weryfikacja: żółty i biały wyglądają jak żółty i biały, a nie jak zielony z domieszką.

### Etap 1.5 — Zrób sobie przyrząd pomiarowy z RP2040 (2–3 wieczory)

Zastępuje niesprawny multimetr i uczy więcej, niż nauczyłoby jego używanie. RP2040 ma 12-bitowy przetwornik ADC na GP26/GP27/GP28 — czyli potrafi zmierzyć napięcie i wypisać je przez USB.

Uczysz się: przetwornik analogowo-cyfrowy, rozdzielczość i napięcie odniesienia, dzielnik napięcia, uśrednianie zaszumionych pomiarów, rezystancja wejściowa i efekt obciążenia obwodu.

**Woltomierz.** `hardware/adc.h`, `adc_read()` zwraca 0–4095 dla zakresu 0–3,3 V. Napięcie = `odczyt * 3.3 / 4095`. Wynik wypisuj przez CDC (Etap 3) albo mrugnięciami diody, dopóki CDC nie masz.

**Pomiar Vf i prądu diody jednym odczytem.** Obwód to 5 V → dioda → węzeł → rezystor → GND. Podepnij ADC do węzła między diodą a rezystorem. Wtedy z jednego pomiaru `Uw` masz oba:
- `Vf = 5 − Uw`
- `I = Uw / R`

Dla czerwonego z 200 Ω węzeł powinien mieć ~3,0 V, dla zielonego z 100 Ω ~1,5 V. Porównaj z obliczeniami z Etapu 1.

**Tester ciągłości** (potrzebny w Etapie 4, patrz ostrzeżenie tam). Wolne GPIO jako wejście z `gpio_pull_up()`, drugi badany koniec do GND. Odczyt 0 = połączenie, 1 = przerwa. Sygnalizuj kolorem diody. Używaj **wyłącznie na obwodzie bez zasilania**.

**Ograniczenia, których nie wolno przeoczyć:**
- Na pin ADC **nigdy więcej niż 3,3 V** — to niszczy pin. Zanim podłączysz sondę do węzła, upewnij się, że dioda jest na miejscu; przy wyjętej diodzie węzeł skoczy do 5 V.
- Do pomiaru 5 V potrzebny jest dzielnik: dwa rezystory 100 Ω szeregowo między 5 V a GND, ADC do punktu środkowego, wynik × 2.
- Napięciem odniesienia jest sam rail 3V3, więc **nie zmierzysz nim własnego zasilania** i dokładność bezwzględna jest kiepska (±kilka %). Do porównań i sprawdzania rzędu wielkości wystarczy.
- Dzielnik z rezystorów 100 Ω mocno obciąża mierzony węzeł i zafałszuje pomiar w obwodzie o podobnej rezystancji. To jest dokładnie powód, dla którego prawdziwy woltomierz ma 10 MΩ na wejściu — dobry moment, żeby zrozumieć pojęcie rezystancji wejściowej.
- ADC w RP2040 jest znany z szumu. Uśredniaj 100–1000 odczytów.

Weryfikacja: dzielnik na 5 V pokazuje 4,8–5,2 V, a policzone Vf diody wychodzi w granicach ~0,3 V od karty katalogowej.

**Uwaga:** to jest proteza, nie zamiennik. Multimetr za 50–100 zł (z brzęczykiem ciągłości i pomiarem prądu) kupisz, jak będziesz mógł — mierzenie prądu i sprawdzanie obwodu bez zasilania to rzeczy, w których ta proteza zawsze będzie gorsza.

Pliki: nowy projekt `E:\Projekty\embedeb\led1` z wtyczki Pico (`Pico Project`), żeby nie mieszać z `haslo1`.

### Etap 2 — Przełącznik suwakowy jako fizyczna blokada (1 wieczór)

Uczysz się: stan wysoki/niski/pływający, po co są rezystory podciągające, drganie styków.

1. Suwak między GP6 a GND, w kodzie `gpio_pull_up(6)`. Zrozum, dlaczego bez pull-upa odczyt jest losowy — spróbuj bez niego i zobacz.
2. Odczyt: LOW = uzbrojony, HIGH = zablokowany. Kolor diody pokazuje stan.
3. Drgania styków: przy suwaku prawie niegroźne, ale zaimplementuj proste odfiltrowanie (dwa odczyty w odstępie 20 ms), bo przy przycisku będzie to konieczne.

Weryfikacja: przesunięcie suwaka natychmiast zmienia kolor, bez migotania.

### Etap 3 — Klawiatura USB (2–3 wieczory)

Uczysz się: czym jest deskryptor USB, HID, scancode vs znak, dlaczego nie da się jednocześnie mieć `printf` po USB i własnego urządzenia HID.

1. Skopiuj **cały** katalog `pico-examples/usb/device/dev_hid_composite` (`main.c`, `usb_descriptors.c`, `usb_descriptors.h`, `tusb_config.h`) do nowego projektu. Nie pisz deskryptorów od zera — najpierw uruchom cudze, potem obcinaj.
2. Zostaw w deskryptorze tylko klawiaturę + CDC (CDC przyda się w Etapie 5 i 7 do zarządzania odciskami przez terminal). Usuń mysz, gamepad, consumer control.
3. W `CMakeLists.txt`: `pico_enable_stdio_usb(... 0)` — obowiązkowo, inaczej SDK zrobi własne urządzenie CDC i pobije się z Twoim. Dodaj `target_include_directories` na katalog z `tusb_config.h`.
4. Wpisz na sztywno „test" wysyłane po starcie i sprawdź w Notatniku.
5. **Pułapka układu klawiatury:** HID wysyła kody klawiszy, nie znaki. Układ „Polski (programisty)" jest oparty na US, więc ASCII wpisze się poprawnie. Układ „Polski (214)" ma poprzestawiane znaki i hasło wyjdzie inne. Sprawdź, co masz w Windows.
6. Hasło stukaj z opóźnieniem ~10–20 ms między znakami — niektóre aplikacje gubią znaki wysyłane z pełną prędkością USB.

Weryfikacja: płytka po podłączeniu pojawia się w Menedżerze urządzeń jako klawiatura i wpisuje „test" do Notatnika.

### Etap 4 — UART i pierwsza rozmowa z czujnikiem (3–4 wieczory)

Uczysz się: UART, prędkość transmisji, 8N1, krzyżowanie TX/RX, poziomy logiczne, po co wspólna masa, budowa ramki binarnej, suma kontrolna.

1. **Zanim podasz zasilanie:** ustal, który przewód taśmy SH1.0 to co. Karta katalogowa R558-S plus **tester ciągłości z Etapu 1.5** — prześledź, który przewód dochodzi do której nóżki wtyku. Kolory przewodów w klonach bywają przestawione, a pomyłka VCC z TX to spalony czujnik. To najbardziej ryzykowny moment całego projektu i **nie wolno go robić na domysł**. Bez działającego testera ciągłości albo multimetru — nie podłączaj czujnika w ogóle.
2. Podłącz: VCC i 3.3VT → 3V3, GND → GND, TX czujnika → GP1, RX czujnika → GP0.
3. Ramka Grow/ZFM: nagłówek `0xEF01`, adres `0xFFFFFFFF`, typ pakietu (`0x01` = komenda, `0x07` = odpowiedź), długość (2 bajty), dane, suma kontrolna (2 bajty = suma typu + długości + danych, big-endian, przeniesienie ignorowane). Wszystko big-endian.
4. Napisz warstwę transportową w C++: `wyslij_pakiet()` / `odbierz_pakiet()` z timeoutem. To jest ten fragment, w którym doświadczenie z matury (tablice, bajty, pętle) realnie się przekłada.
5. Pierwsza komenda: **VerifyPassword `0x13`** z hasłem `0x00000000` — najprostszy round-trip. Kod potwierdzenia `0x00` = sukces. Potem `ReadSysPara 0x0F` i `TemplateCount 0x1D`.
6. Diagnostyka: kod błędu wyświetlaj na diodzie RGB (mrugnięcia) albo przez CDC z Etapu 3.

Weryfikacja: `VerifyPassword` zwraca `0x00`, a `TemplateCount` zwraca 0 (pusta baza).

Referencja protokołu: manual R503/ZFM-20 (ta sama rodzina) — pełna tabela komend i kodów błędów.

### Etap 5 — Rejestracja i rozpoznawanie palca (2–3 wieczory)

1. Rejestracja: `GetImage 0x01` → `Image2Tz 0x02` (bufor 1) → drugie przyłożenie → `Image2Tz` (bufor 2) → `RegModel 0x05` → `Store 0x06` pod ID.
2. Rozpoznanie: `GetImage` → `Image2Tz` (bufor 1) → `Search 0x04` → zwraca ID i wynik dopasowania.
3. Pin TOUCH/WAKEUP: przerwanie na GP2 zamiast odpytywania czujnika w pętli. Uczysz się, czym jest przerwanie GPIO i dlaczego jest lepsze od `while` z opóźnieniem.
4. Zarządzanie odciskami przez CDC: prosty terminal, komendy `enroll <id>`, `delete <id>`, `list`, `empty`.
5. Dioda: niebieski = czekam, biały = skanuję, zielony = dopasowano, czerwony = odmowa.

Weryfikacja: Twój palec daje zielone, palec kolegi czerwone, po `delete` Twój palec też daje czerwone.

### Etap 6 — Złożenie w klucz, wersja 1 (1–2 wieczory)

Uczysz się: maszyna stanów — właściwy sposób pisania firmware zamiast zagnieżdżonych `if`-ów.

Stany: `ZABLOKOWANY` (suwak off) → `CZEKAM` → `SKANUJĘ` → `WPISUJĘ` → `COOLDOWN`.

Hasło w osobnym pliku `haslo_prywatne.h`, wpisanym do `.gitignore`, plus `haslo_przyklad.h` w repo jako wzór. Wpisanie hasła to sekwencja raportów HID z Etapu 3.

Zabezpieczenia logiczne warte dołożenia od razu: limit prób (np. 5 nieudanych → blokada na minutę), cooldown po wpisaniu, wymóg przełączenia suwaka przed kolejnym wpisaniem.

Weryfikacja: suwak on → palec → hasło ląduje w polu menedżera haseł. Suwak off → nic się nie dzieje.

### Etap 7 — Progresywne bezpieczeństwo

**v2 — hasło we flashu, nie w kodzie.** Ostatni sektor flash (`hardware/flash.h`, `flash_range_erase` / `flash_range_program`), hasło ustawiane komendą przez CDC. Efekt: hasła nie ma w kodzie ani w pliku `.uf2`, więc nie wycieknie przez repozytorium. Uczysz się organizacji pamięci flash i tego, że kod i dane leżą w tym samym układzie.

**v3 — gdzie jest sufit, uczciwie.** RP2040 nie ma bezpiecznego rozruchu ani szyfrowania flasha. Każdy, kto weźmie płytkę do ręki, wciśnie BOOTSEL i zrzuci całą zawartość flasha przez `picotool save`. Szyfrowanie hasła kluczem trzymanym w tym samym flashu niczego nie zmienia — klucz zrzuci się razem z nim. Odcisk palca też nie jest materiałem na klucz: czujnik zwraca „pasuje/nie pasuje", nie stabilny ciąg bajtów.

Co realnie działa na tym sprzęcie:
- suwak jako fizyczna blokada (nie da się użyć klucza „w przelocie"),
- traktowanie klucza jako *coś, co masz*, a nie sejfu — hasło główne dalej chronione 2FA w menedżerze,
- świadomość, że utrata płytki = zmiana hasła.

Jeśli kiedyś zechcesz zrobić to naprawdę odpornie, sprzętem do tego jest RP2350 (bezpieczny rozruch + pamięć OTP na klucze) — dobry cel na projekt numer dwa.

---

## Czego się uczyć — lista skrócona

| Pojęcie | Kiedy jest potrzebne |
|---|---|
| Prawo Ohma, moc, rezystory szeregowo/równolegle | Etap 1 |
| Vf diody, ograniczanie prądu, wspólna anoda/katoda | Etap 1 |
| GPIO: stan wysoki/niski/Hi-Z, wydajność prądowa pinu | Etap 1–2 |
| PWM i sterowanie jasnością | Etap 1 |
| ADC: rozdzielczość, napięcie odniesienia, uśrednianie | Etap 1.5 |
| Dzielnik napięcia, rezystancja wejściowa, efekt obciążenia | Etap 1.5 |
| Rezystory podciągające, drganie styków | Etap 2 |
| Poziomy logiczne 3,3 V vs 5 V | Etap 4 (wiedza obronna) |
| UART: prędkość, 8N1, krzyżowanie, wspólna masa | Etap 4 |
| Ramki binarne, big-endian, suma kontrolna | Etap 4 |
| Przerwania GPIO | Etap 5 |
| Maszyna stanów | Etap 6 |
| Organizacja pamięci flash | Etap 7 |
| Czytanie kart katalogowych | wszystkie etapy |

Materiały: **Forbot — Kurs elektroniki I i II** (po polsku, darmowy, dokładnie ten poziom), dokumentacja *Raspberry Pi Pico C/C++ SDK*, rozdział o GPIO w karcie katalogowej RP2040, manual R503/ZFM-20 na protokół czujnika.

## Zasady, żeby nie spalić części

1. Czujnik **tylko 3,3 V**. Nigdy 5 V, nawet „na chwilę żeby sprawdzić".
2. Przed każdą zmianą okablowania **wyciągnij USB**.
3. Dioda **nigdy** bez rezystora — jedno dotknięcie goła dioda→GPIO potrafi uszkodzić pin.
4. TX idzie do RX, nie do TX. Jak nie działa — najpierw zamień te dwa przewody.
5. Wspólna masa między wszystkim. Brak wspólnej masy = objawy, których nie da się zdiagnozować.
6. Przy pierwszym podaniu zasilania na czujnik dotknij go palcem po 10 sekundach — jeśli jest gorący, natychmiast odłącz.

## Weryfikacja całości

Test końcowy po Etapie 6:
1. Wepnij klucz w USB. Menedżer urządzeń: widoczna klawiatura HID + port COM.
2. Suwak w pozycji off → dotknięcie czujnika nie robi nic, dioda czerwona.
3. Suwak on → dioda niebieska.
4. Otwórz Notatnik, przyłóż palec → zielone mrugnięcie, hasło wpisane poprawnie znak w znak (sprawdź szczególnie znaki specjalne — to test układu klawiatury).
5. Przyłóż nieznany palec → czerwone, nic nie wpisane.
6. Pięć nieudanych prób → blokada, dioda pulsuje czerwono przez minutę.
7. Wyciągnij i wepnij ponownie → działa bez przeprogramowania.

## Kolejność pracy w plikach

Każdy etap jako osobny projekt z wtyczki Pico (`led1`, `switch1`, `hid1`, `czujnik1`), a `E:\Projekty\embedeb\haslo1` zostaje jako miejsce, gdzie na Etapie 6 składasz działające kawałki w całość. Nie buduj wszystkiego w jednym projekcie od początku — przy pierwszym błędzie nie będziesz wiedział, która z czterech nowych rzeczy nie działa.
