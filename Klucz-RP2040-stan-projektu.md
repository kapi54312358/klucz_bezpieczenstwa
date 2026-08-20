# Klucz RP2040 — stan projektu

Dokument przekazania. Zawiera wszystko, co potrzebne do wznowienia pracy bez czytania historii rozmowy. Stan na 19.08.2026.

---

## Czym jest projekt

Urządzenie USB, które po wyzwoleniu wpisuje hasło do menedżera haseł, udając klawiaturę (USB HID). Pierwotny wyzwalacz: czytnik linii papilarnych. Obecny: przełącznik suwakowy, bo czujnik okazał się wadliwy.

Drugi, równorzędny cel: **nauka elektrotechniki i systemów wbudowanych**. Punkt startowy — C++ na poziomie matury rozszerzonej, elektronika prawie od zera.

Środowisko: VS Code + wtyczka Raspberry Pi Pico, Pico SDK 2.3.0, Windows.
Katalog roboczy: `E:\1Projekty\embedeb`

---

## Stan: co działa, co nie

### Działa

- **Dioda RGB** na 5 V ze wspólną anodą, sterowana z GP2 i GP3
- **Suwak** na GP4 z podciągnięciem wewnętrznym i wykrywaniem zbocza
- **Klawiatura USB HID** — płytka widziana przez Windows jako klawiatura
- **Wpisywanie hasła** znak po znaku z Enterem na końcu, wyzwalane suwakiem
- **Sygnalizacja diodą** — zielony w trakcie i po wpisaniu, czerwony w spoczynku
- **Hasło poza kodem** — w `haslo_prywatne.h`, plik objęty `.gitignore`
- **Cała warstwa protokołu czujnika** — budowanie ramek, sumy kontrolne, synchronizacja, rejestracja, rozpoznawanie, diagnostyka po USB

### Nie działa

- **Zapis wzorca w czujniku** — wada egzemplarza, szczegóły niżej
- **Niebieski kanał diody RGB** — martwy, ale niepotrzebny; zielony i czerwony wystarczają na „wpuszczony / odrzucony"

---

## Sprzęt i okablowanie

Płytka: klon **Pro Micro RP2040**, oznaczenie na laminacie `HW-467A8`. Wyprowadza więcej pinów niż oryginał SparkFun — m.in. GPIO10–13, 18, 24, 25.

### Rozkład pinów

Prawa kolumna, licząc **od złącza USB**: GPIO24, GPIO11, **5V**, GND, RST, **3.3V**, GPIO29, GPIO28, GPIO27, GPIO26, GPIO22, GPIO20, GPIO23, GPIO21.

Lewa kolumna od USB: GPIO18, BOOT, GPIO10, GPIO0, GPIO1, **GND, GND**, GPIO2, GPIO3, GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9.

> Punkt odniesienia przy liczeniu: **para sąsiadujących padów GND** występuje tylko w jednym miejscu. Zaraz za nią idą GPIO2 i GPIO3.

### Połączenia

| Element | Pin | Uwagi |
|---|---|---|
| Anoda diody (najdłuższa nóżka) | **5V** | i nic więcej |
| Katoda czerwona | GP3 | przez 200 Ω (2× 100 Ω szeregowo) |
| Katoda zielona | GP2 | przez 100 Ω |
| Katoda niebieska | GP5 | przez 100 Ω — kanał martwy |
| Suwak, nóżka środkowa | GP4 | `gpio_pull_up()` |
| Suwak, nóżka skrajna | GND | |
| Czujnik VCC + 3.3VT | **3.3V** | nigdy 5 V |
| Czujnik TX | GP1 | krzyżowo |
| Czujnik RX | GP0 | krzyżowo |
| Czujnik GND | GND | |

### Dlaczego anoda na 5 V, a nie na 3V3

Zielony i niebieski w tej diodzie mają **Vf = 3,5 V** — więcej niż rail 3,3 V. Z 3V3 nie zaświecą się nigdy, niezależnie od rezystora. Pin `RAW`/`5V` daje realnie ok. 4,8 V (5 V z USB minus dioda Schottky'ego), co przy 100 Ω daje ~10–12 mA.

Wspólna anoda oznacza **odwróconą logikę**: stan niski na pinie zapala kolor. Odwrócenie jest schowane w jednym miejscu — w funkcji `led_set()`.

---

## Projekty na dysku

| Katalog | Zawartość |
|---|---|
| `klucz_b3` | **Gotowy klucz.** USB HID, wpisywanie hasła, suwak, sygnalizacja diodą |
| `klucz4` | Warstwa protokołu czujnika + diagnostyka po USB CDC |
| `led2`, `led1` | Ćwiczenia z diodą i suwakiem |
| `helloworld2`, `blink` | Pierwsze projekty |
| `PLAN.md` | Plan etapów z odhaczonym postępem |

Hasło siedzi w `klucz_b3/haslo_prywatne.h` (poza repozytorium). Wzorzec do skopiowania: `haslo_przyklad.h`.

**Do usunięcia:** `klucz_b3/haslo_prywatne.gitignore` — nieudana próba, plik z hasłem w środku.

---

## Sprawa czujnika R558S — nie diagnozować od nowa

Egzemplarz odrzuca **każdą** komendę, która utrwala albo eksportuje wzorzec. Ramki wysyłane są poprawne, sumy kontrolne zgadzają się w obie strony, okablowanie sprawdzone.

### Odmawia

| Komenda | Kod odpowiedzi |
|---|---|
| `Store 0x06` | `0x35` — kod spoza jakiejkolwiek dokumentacji |
| `UpChar 0x08` | `0x01` — komenda nieznana |
| `AutoEnroll 0x31` | `0x25` / `0x01` |

### Działa

`VerifyPassword 0x13`, `ReadSysPara 0x0F`, `TemplateCount 0x1D`, `DeleteAll 0x0D`, `GetImage 0x01`, `Image2Tz 0x02`, `RegModel 0x05`, `CheckSensor 0x36`, `ReadIndexTable 0x1F`, `Match 0x03`.

Nieskuteczne próby odblokowania: `HandShake 0x53` i `GetImageEx 0x28` — obie nieznane modułowi.

### Dowód wady

`ReadSysPara` zwraca pole „system identifier code" równe **`0x0000`**, podczas gdy specyfikacja ZFM przewiduje stałą **`0x0009`**. Deklarowana pojemność to 100 wzorców, `ReadIndexTable` potwierdza pustą i dostępną bibliotekę, a mimo to zapisać nie da się ani jednego wzorca.

Wniosek: firmware nieoryginalny lub uszkodzony. **Wada towaru, nie błąd użytkowania.**

Po wymianie czujnika wystarczy przenieść warstwę protokołu z `klucz4` i podmienić wyzwalacz. Uwaga: odczyty UART trzeba wtedy przerobić na **nieblokujące**, inaczej zaduszą `tud_task()` i USB przestanie działać.

---

## Fakty techniczne warte zapamiętania

**Protokół Grow/ZFM.** Ramka: `EF 01` + adres `FF FF FF FF` + typ pakietu + długość (2 B) + treść + suma (2 B). Wszystko big-endian. Długość liczy **treść + 2 bajty sumy**. Suma obejmuje typ pakietu, oba bajty długości i treść — nagłówek i adres są z niej wyłączone.

**Synchronizacja UART.** Nie zakładać, że pierwszy odebrany bajt to początek ramki. Szukać `EF 01` w strumieniu — inaczej jeden spóźniony bajt rozjeżdża całą dalszą transmisję.

**Zwłoka przed zdjęciem palca.** Czujnik robi obraz w chwili dotknięcia, gdy palec przylega jeszcze czubkiem. 400 ms zwłoki i ponowne zdjęcie usunęły systematyczne `0x0A` przy łączeniu wzorców.

**Czujnik kasuje obraz po konwersji.** Drugie `Image2Tz` z tego samego zdjęcia zwraca `0x07`. Do drugiego bufora trzeba nowego ujęcia.

**USB HID.** Klawiatura wysyła kody klawiszy, nie znaki. Jeden znak = **dwa raporty**: wciśnięcie i puszczenie. `a` i `A` mają ten sam kod, różni je bit Shift. Tabela `HID_KEYCODE_TO_ASCII` w `tusb.h` działa w kierunku kod→znak, więc trzeba ją przeszukiwać. Układ klawiatury w Windows musi być „Polski (programisty)" — „Polski (214)" przestawia znaki.

**`sleep_ms()` w pętli z USB jest groźne.** Blokuje `tud_task()`, a wtedy Windows odłącza urządzenie. Odmierzać czas znacznikami: `board_millis() - start >= X`, w tej formie, nie `board_millis() >= start + X` — przez przepełnienie licznika po ~49 dniach.

---

## Co dalej

**Etap 7** — hasło do ostatniego sektora flash RP2040, ustawiane komendą przez USB CDC zamiast przekompilowania. Niezależne od wyboru wyzwalacza, można robić od razu.

**Etap 8** — obudowa z druku 3D. Okienko na diodę, dostęp do suwaka, gniazdo USB-C. **Decyzja o wyzwalaczu musi zapaść przed projektowaniem.**

### Wybór wyzwalacza zamiast czujnika palca

| Opcja | Koszt | Ocena |
|---|---|---|
| **RFID RC522 + karta i brelok** | ~12 zł | **Rekomendowane.** Token fizyczny, uczy SPI |
| **Sekretny wzór przestawień suwaka** | 0 zł | Działa od razu, „coś, co wiesz" |
| Kontaktron / czujnik Halla | ~5 zł | Odradzane — otworzy dowolny magnes |
| Nowy czujnik palca | ~50 zł | Kod gotowy, ale ryzyko powtórki |
| ESP32-CAM, rozpoznawanie twarzy | ~45 zł | Odradzane — zdjęcie oszukuje, zła ergonomia dongla |

Uwaga do RFID: część kart płatniczych losuje UID przy każdym zbliżeniu, więc jako klucza trzeba użyć karty z zestawu.

**Rama, o której warto pamiętać:** żadna z tych opcji nie zwiększa realnego bezpieczeństwa, bo flash RP2040 da się zrzucić przez BOOTSEL. Wyzwalacz wybiera się po wygodzie, cenie i tym, czego się przy okazji uczy. RP2040 nie ma bezpiecznego rozruchu — sprzętem do tego jest RP2350.

---

## Jak pracować

**Po każdej zmianie pliku z zewnątrz: File → Revert File w VS Code.** Inaczej edytor nadpisze plik zawartością swojego bufora. Zdarzyło się to już raz i kosztowało jeden cykl.

**Budowanie:** przycisk Compile w VS Code albo z linii poleceń:

```bash
ninja -C /e/1Projekty/embedeb/klucz4/build
```

ze ścieżką rozszerzoną o `~/.pico-sdk/ninja/v1.13.2`, `~/.pico-sdk/toolchain/15_2_Rel1/bin` i `~/.pico-sdk/cmake/v4.3.4/bin`.

**Wgrywanie:** przytrzymać BOOT, wpiąć USB, przeciągnąć `.uf2` na dysk `RPI-RP2`.

**Diagnostyka `klucz4`:** ma włączone `pico_enable_stdio_usb(1)`, więc wypisuje komunikaty na port COM. Serial Monitor w VS Code, prędkość 115200. Program czeka do 15 sekund na otwarcie portu, więc startowe komunikaty nie przepadają.

`klucz_b3` ma stdio po USB **wyłączone** i tak musi zostać — inaczej SDK utworzy własne urządzenie CDC i pobije się z deskryptorami HID.

### Pułapki, które już raz kosztowały czas

- Nowy projekt z kreatora bez wybranej płytki daje `PICO_BOARD undefined` i CMake odmawia konfiguracji. Ma być `pico`.
- Żółty trójkąt przy `config_autogen.h` zaraz po utworzeniu projektu to nieaktualne ostrzeżenie — przeładować okno.
- Czerwone „cannot open source file" przy `tusb.h` oznacza brak `tinyusb_device` i `tinyusb_board` w `target_link_libraries`.
- Przeniesienie katalogu projektu unieważnia `build` — CMake trzyma tam ścieżki bezwzględne. Skasować `build` i zbudować od nowa.
- Ścieżki z polskimi znakami psują toolchain ARM. Projekty Pico trzymać poza `cpp_szkoła`.
