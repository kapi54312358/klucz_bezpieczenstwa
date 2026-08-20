#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"

#ifndef LED_GREEN
#define LED_GREEN 2
#endif

#ifndef LED_RED
#define LED_RED 3
#endif

#ifndef LED_BLUE
#define LED_BLUE 5
#endif

// Numer, pod ktorym zapisujemy pierwszy palec w pamieci czujnika.
#define ID_PALCA 1

//--------------------------------------------------------------------+
// Dioda
//--------------------------------------------------------------------+

// Wspolna anoda: stan niski zapala kolor, stad negacja.
static void led_rgb(bool r, bool g, bool b)
{
    gpio_put(LED_RED,   !r);
    gpio_put(LED_GREEN, !g);
    gpio_put(LED_BLUE,  !b);
}

static void mrugaj(bool r, bool g, bool b, int ile)
{
    for (int i = 0; i < ile; i++)
    {
        led_rgb(r, g, b);   sleep_ms(150);
        led_rgb(0, 0, 0);   sleep_ms(150);
    }
}

// Test diod przy starcie: czerwony, zielony, niebieski, kazdy po sekundzie.
// Sluzy do sprawdzenia okablowania niezaleznie od reszty programu.
static void test_diod(void)
{
    led_rgb(1, 0, 0);  sleep_ms(1000);
    led_rgb(0, 1, 0);  sleep_ms(1000);
    led_rgb(0, 0, 1);  sleep_ms(1000);
    led_rgb(0, 0, 0);  sleep_ms(500);
}

//--------------------------------------------------------------------+
// Protokol Grow / ZFM
//--------------------------------------------------------------------+
//
//   EF 01          naglowek
//   FF FF FF FF    adres modulu
//   XX             typ pakietu: 01 = komenda, 07 = odpowiedz
//   HH LL          dlugosc = tresc + 2 bajty sumy, starszy bajt pierwszy
//   ...            tresc
//   HH LL          suma kontrolna
//
// Suma obejmuje typ pakietu, oba bajty dlugosci i cala tresc.

struct Odpowiedz
{
    bool    odebrana;    // czy przyszla poprawnie zbudowana ramka
    uint8_t kod;         // kod potwierdzenia, 0x00 = sukces
    uint8_t dane[32];    // bajty tresci PO kodzie potwierdzenia
    int     ile_danych;
};

// Gdy wlaczone, wyslij_komende() wypisuje surowe bajty ramek przez USB.
// Wlaczamy to tylko wokol podejrzanej komendy, zeby nie zalac konsoli.
static bool debug_ramki = false;

// Ile czekamy na poczatek odpowiedzi. AutoEnroll trwa kilkanascie sekund,
// bo w jego trakcie uzytkownik kilka razy przyklada palec.
static uint32_t limit_odpowiedzi_ms = 2000;

static void wypisz_bajty(const char *opis, const uint8_t *dane, int ile)
{
    printf("%s (%d B):", opis, ile);
    for (int i = 0; i < ile; i++) printf(" %02X", dane[i]);
    printf("\n");
}

static int odbierz(uint8_t *bufor, int ile, uint32_t limit_us)
{
    int n = 0;

    while (n < ile && uart_is_readable_within_us(uart0, limit_us))
    {
        bufor[n] = (uint8_t) uart_getc(uart0);
        n++;
    }

    return n;
}

// Buduje ramke wokol podanej tresci, wysyla i czeka na odpowiedz.
// Dlugosc i sume kontrolna liczy sama.
static Odpowiedz wyslij_komende(const uint8_t *tresc, int dlugosc_tresci)
{
    Odpowiedz wynik = { false, 0xFF, { 0 }, 0 };

    uint8_t  ramka[64];
    int      i = 0;
    uint16_t dlugosc = dlugosc_tresci + 2;

    ramka[i++] = 0xEF;  ramka[i++] = 0x01;
    ramka[i++] = 0xFF;  ramka[i++] = 0xFF;
    ramka[i++] = 0xFF;  ramka[i++] = 0xFF;
    ramka[i++] = 0x01;
    ramka[i++] = (uint8_t)(dlugosc >> 8);
    ramka[i++] = (uint8_t)(dlugosc & 0xFF);

    for (int j = 0; j < dlugosc_tresci; j++)
        ramka[i++] = tresc[j];

    uint16_t suma = 0;
    for (int j = 6; j < i; j++)
        suma += ramka[j];

    ramka[i++] = (uint8_t)(suma >> 8);
    ramka[i++] = (uint8_t)(suma & 0xFF);

    while (uart_is_readable(uart0)) uart_getc(uart0);
    uart_write_blocking(uart0, ramka, i);

    if (debug_ramki) wypisz_bajty("  -> wyslano", ramka, i);

    // Szukamy poczatku ramki (EF 01) zamiast zakladac, ze zaczyna sie od razu.
    // Dzieki temu spoznione bajty z poprzedniej odpowiedzi nie rozjezdzaja
    // calej dalszej transmisji - program sam sie z nia synchronizuje.
    uint8_t b = 0, poprzedni = 0;
    bool    znaleziony = false;
    absolute_time_t koniec = make_timeout_time_ms(limit_odpowiedzi_ms);

    while (!time_reached(koniec))
    {
        if (!uart_is_readable_within_us(uart0, 100000)) continue;

        poprzedni = b;
        b = (uint8_t) uart_getc(uart0);

        if (poprzedni == 0xEF && b == 0x01) { znaleziony = true; break; }
    }

    if (!znaleziony) return wynik;

    // Reszta naglowka: 4 bajty adresu, typ pakietu, 2 bajty dlugosci.
    uint8_t naglowek[7];
    if (odbierz(naglowek, 7, 500000) != 7) return wynik;

    uint16_t dl_odp = (uint16_t)((naglowek[5] << 8) | naglowek[6]);
    if (dl_odp < 3 || dl_odp > sizeof(wynik.dane) + 3) return wynik;

    uint8_t reszta[64];
    if (odbierz(reszta, dl_odp, 1000000) != (int) dl_odp) return wynik;

    if (debug_ramki)
    {
        printf("  <- naglowek: EF 01");
        for (int j = 0; j < 7; j++) printf(" %02X", naglowek[j]);
        printf("\n");
        wypisz_bajty("  <- reszta  ", reszta, dl_odp);
    }

    wynik.odebrana   = true;
    wynik.kod        = reszta[0];
    wynik.ile_danych = dl_odp - 3;

    for (int j = 0; j < wynik.ile_danych; j++)
        wynik.dane[j] = reszta[1 + j];

    return wynik;
}

// Skrot dla komend, przy ktorych interesuje nas tylko kod potwierdzenia.
static uint8_t komenda(const uint8_t *tresc, int dlugosc)
{
    Odpowiedz o = wyslij_komende(tresc, dlugosc);
    return o.odebrana ? o.kod : 0xFF;
}

//--------------------------------------------------------------------+
// Operacje na palcu
//--------------------------------------------------------------------+

// Czeka, az czujnik zrobi zdjecie palca. Zwraca kod potwierdzenia.
// 0x02 znaczy "brak palca" - to nie blad, tylko sygnal, ze mamy czekac dalej.
// Chwilowy brak odpowiedzi (0xFF) tez nie przerywa od razu - probujemy ponownie.
// Gdy 'sygnalizuj' jest wlaczone, zielona dioda mruga: "przylóz palec".
static uint8_t czekaj_na_palec(int prob, bool sygnalizuj)
{
    const uint8_t cmd[1] = { 0x01 };   // GetImage
    int bledy = 0;

    for (int i = 0; i < prob; i++)
    {
        if (sygnalizuj) led_rgb(0, (i % 4) < 2, 0);

        uint8_t kod = komenda(cmd, 1);

        if (kod == 0x00)
        {
            // Pierwsze zdjecie lapie palec w chwili dotkniecia, gdy przylega
            // jeszcze tylko czubkiem. Dajemy mu dolgnac i robimy zdjecie ponownie -
            // dzieki temu oba obrazy przy rejestracji obejmuja podobny obszar.
            sleep_ms(400);
            return komenda(cmd, 1);
        }

        if (kod == 0x02) { bledy = 0; sleep_ms(100); continue; }   // brak palca

        if (kod == 0xFF)                              // cisza - sprobuj jeszcze raz
        {
            if (++bledy > 10) return 0xFF;
            sleep_ms(100);
            continue;
        }

        return kod;                                   // prawdziwy blad czujnika
    }

    return 0x02;
}

// Czeka, az palec zostanie zdjety z czujnika. Czerwona dioda: "zdejmij palec".
static void czekaj_na_zdjecie_palca(void)
{
    const uint8_t cmd[1] = { 0x01 };

    led_rgb(1, 0, 0);

    for (int i = 0; i < 300; i++)
    {
        if (komenda(cmd, 1) == 0x02) return;   // palca juz nie ma
        sleep_ms(100);
    }
}

// Przerabia ostatni obraz na wzorzec i wklada go do wskazanego bufora (1 albo 2).
static uint8_t obraz_do_wzorca(uint8_t bufor)
{
    const uint8_t cmd[2] = { 0x02, bufor };   // Image2Tz
    return komenda(cmd, 2);
}

// Pelna rejestracja: dwa przylozenia, polaczenie i zapis pod numerem ID_PALCA.
// Zwraca 0x00 przy powodzeniu.
// Komendy, ktore w nowszych modulach Grow sluza do sprawdzenia stanu modulu
// albo odblokowania go po starcie. Jesli ktoras zwroci 0x00, jest szansa,
// ze zapis wzorca przestanie byc odrzucany.
static void proby_odblokowania(void)
{
    struct Proba { const char *nazwa; uint8_t tresc[4]; int dlugosc; };

    const Proba proby[] = {
        { "HandShake      0x53", { 0x53 },                     1 },
        { "CheckSensor    0x36", { 0x36 },                     1 },
        { "ReadIndexTable 0x1F", { 0x1F, 0x00 },               2 },
        { "GetImageEx     0x28", { 0x28 },                     1 },
        { "Match          0x03", { 0x03 },                     1 },
    };

    printf("--- proby odblokowania ---\n");
    debug_ramki = true;

    for (unsigned i = 0; i < sizeof(proby) / sizeof(proby[0]); i++)
    {
        printf("%s\n", proby[i].nazwa);
        printf("   -> kod 0x%02X\n", komenda(proby[i].tresc, proby[i].dlugosc));
        sleep_ms(200);
    }

    debug_ramki = false;
    printf("--- koniec prob ---\n");
}

static void test_upchar(void);   // definicja nizej

// 'krok' dostaje numer etapu, na ktorym cos poszlo nie tak:
//   1 = pierwsze zdjecie palca      4 = drugi wzorzec
//   2 = pierwszy wzorzec            5 = polaczenie wzorcow
//   3 = drugie zdjecie palca        6 = zapis do pamieci
static uint8_t zarejestruj_palec(int *krok)
{
    uint8_t kod;

    // --- pierwsze przylozenie ---  zielony mruga: przylóz palec
    *krok = 1;
    kod = czekaj_na_palec(300, true);       // ok. 30 sekund
    if (kod != 0x00) return kod;

    *krok = 2;
    led_rgb(0, 1, 0);                       // zielony na stale: mam obraz
    kod = obraz_do_wzorca(1);
    if (kod != 0x00) return kod;

    // --- zdejmij palec ---  czerwony na stale
    czekaj_na_zdjecie_palca();
    sleep_ms(500);

    // --- drugie przylozenie ---  zielony znowu mruga
    *krok = 3;
    kod = czekaj_na_palec(300, true);
    if (kod != 0x00) return kod;

    *krok = 4;
    kod = obraz_do_wzorca(2);
    if (kod != 0x00) return kod;

    // --- polacz oba wzorce w jeden model ---
    *krok = 5;
    const uint8_t cmd_reg[1] = { 0x05 };    // RegModel
    kod = komenda(cmd_reg, 1);
    if (kod != 0x00) return kod;            // 0x0A = przylozenia za bardzo sie roznia

    // Wzorzec jest gotowy w buforze - sprawdzmy, czy da sie go stad wyciagnac.
    test_upchar();

    // --- zapisz w pamieci czujnika ---
    // Ten egzemplarz odpowiada na Store nieudokumentowanym 0x35, wiec probujemy
    // kilku wariantow: inny bufor, inny numer strony. Ktorys powinien przejsc.
    *krok = 6;

    const uint8_t warianty[4][4] = {
        { 0x06, 0x01, 0x00, 0x01 },   // bufor 1, strona 1
        { 0x06, 0x01, 0x00, 0x00 },   // bufor 1, strona 0
        { 0x06, 0x02, 0x00, 0x00 },   // bufor 2, strona 0
        { 0x06, 0x02, 0x00, 0x01 },   // bufor 2, strona 1
    };

    debug_ramki = true;

    for (int w = 0; w < 4; w++)
    {
        printf("  Store: bufor %d, strona %d\n", warianty[w][1], warianty[w][3]);
        kod = komenda(warianty[w], 4);
        printf("  -> kod 0x%02X\n", kod);
        if (kod == 0x00) break;
    }

    debug_ramki = false;
    return kod;
}

// UpChar (0x08) - prosi modul o oddanie wzorca z bufora do nas.
// To jest test decydujacy: jesli wzorzec da sie pobrac, pamiec czujnika
// przestaje byc potrzebna - wzorzec zamieszka we flashu RP2040, a przy kazdym
// starcie wgramy go z powrotem komenda DownChar.
static void test_upchar(void)
{
    printf("--- UpChar: proba pobrania wzorca z bufora 1 ---\n");

    const uint8_t cmd[2] = { 0x08, 0x01 };

    debug_ramki = true;
    Odpowiedz o = wyslij_komende(cmd, 2);
    debug_ramki = false;

    printf("  UpChar -> kod 0x%02X\n", o.kod);
    if (!o.odebrana || o.kod != 0x00) return;

    // Po potwierdzeniu modul sypie pakietami danych. Zrzucamy poczatek
    // i liczymy, ile bajtow lacznie przyszlo - wzorzec ma zwykle 512 bajtow.
    int     razem = 0;
    absolute_time_t koniec = make_timeout_time_ms(4000);

    printf("  pierwsze bajty danych:");

    while (!time_reached(koniec))
    {
        if (!uart_is_readable_within_us(uart0, 300000)) continue;

        uint8_t b = (uint8_t) uart_getc(uart0);
        if (razem < 48) printf(" %02X", b);
        razem++;
    }

    printf("\n  lacznie odebrano %d bajtow\n", razem);
}

// AutoEnroll (0x31) - nowsze moduly Grow potrafia przeprowadzic cala rejestracje
// same: zbieraja kolejne obrazy, lacza je i zapisuja do pamieci, bez osobnego Store.
// Skoro Store na tym egzemplarzu odmawia, to jest droga naokolo.
//
// Konfiguracja: nadpisywanie ID dozwolone, duplikaty dozwolone, bez raportow
// posrednich (chcemy jedna odpowiedz), z prosba o zdjecie palca miedzy ujeciami.
//
// Format nie jest jednoznacznie udokumentowany - numer wzorca bywa opisywany
// jako jeden albo dwa bajty. Probujemy obu wariantow i patrzymy, ktory przejdzie.
static uint8_t auto_rejestracja(void)
{
    // Numer wzorca jest dwubajtowy - to wynika z 0x0B ("numer poza biblioteka"),
    // ktory dostalismy, gdy moduł przeczytal 01 01 jako 257.
    // Dlugosc pakietu 8 oznacza tresc 6-bajtowa: instrukcja + numer(2) + 3 bajty.
    const uint8_t warianty[3][8] = {
        { 0x31, 0x00, ID_PALCA, 0x01, 0x01, 0x00 },                    // 6 B, dlugosc 8
        { 0x31, 0x00, ID_PALCA, 0x04, 0x01, 0x01, 0x00 },              // 7 B - z liczba ujec
        { 0x31, 0x00, ID_PALCA, 0x04, 0x01, 0x01, 0x00, 0x01 },        // 8 B - pelny zestaw
    };
    const int dlugosci[3] = { 6, 7, 8 };

    limit_odpowiedzi_ms = 30000;   // uzytkownik bedzie przykladal palec kilka razy
    debug_ramki = true;

    uint8_t kod = 0xFF;

    for (int w = 0; w < 3; w++)
    {
        printf("AutoEnroll, wariant %d (tresc %d B)\n", w + 1, dlugosci[w]);
        printf("   przykladaj i zdejmuj palec, mniej wiecej co sekunde\n");
        led_rgb(0, 1, 0);

        kod = komenda(warianty[w], dlugosci[w]);
        printf("   -> kod 0x%02X\n", kod);

        if (kod == 0x00) break;

        // 0x0B = numer poza biblioteka, 0x25 = zly format - nie ma sensu czekac
        led_rgb(1, 0, 0);
        sleep_ms(1500);
    }

    debug_ramki = false;
    limit_odpowiedzi_ms = 2000;

    return kod;
}

// Awaryjna rejestracja z JEDNEGO przylozenia: oba bufory powstaja z tego samego
// obrazu, wiec RegModel nie ma czego odrzucic. Wzorzec jest nieco gorszej jakosci
// niz z dwoch roznych przylozen, ale do wlasnego klucza w zupelnosci wystarcza.
static uint8_t zarejestruj_palec_prosty(int *krok)
{
    uint8_t kod;

    *krok = 1;
    kod = czekaj_na_palec(300, true);
    if (kod != 0x00) return kod;

    led_rgb(0, 1, 0);

    *krok = 2;
    kod = obraz_do_wzorca(1);
    if (kod != 0x00) return kod;

    // Czujnik kasuje obraz po konwersji, wiec do drugiego bufora trzeba zrobic
    // NOWE zdjecie. Palec zostaje na czujniku, wiec oba obrazy beda niemal
    // identyczne i RegModel nie ma czego odrzucic.
    *krok = 3;
    kod = czekaj_na_palec(50, false);
    if (kod != 0x00) return kod;

    *krok = 4;
    kod = obraz_do_wzorca(2);
    if (kod != 0x00) return kod;

    *krok = 5;
    const uint8_t cmd_reg[1] = { 0x05 };
    kod = komenda(cmd_reg, 1);
    if (kod != 0x00) return kod;

    *krok = 6;
    const uint8_t cmd_store[4] = { 0x06, 0x01, 0x00, ID_PALCA };

    debug_ramki = true;
    kod = komenda(cmd_store, 4);
    debug_ramki = false;

    return kod;
}

// Szuka przylozonego palca wsrod zapisanych wzorcow.
// Zwraca 0x00 gdy znaleziono, 0x09 gdy nie ma dopasowania.
static uint8_t rozpoznaj_palec(uint16_t *numer, uint16_t *pewnosc)
{
    uint8_t kod = czekaj_na_palec(1, false);   // jedna proba - petla jest wyzej
    if (kod != 0x00) return kod;

    kod = obraz_do_wzorca(1);
    if (kod != 0x00) return kod;

    // Search: bufor 1, od wzorca 0, przeszukaj 100 pozycji
    const uint8_t cmd[6] = { 0x04, 0x01, 0x00, 0x00, 0x00, 0x64 };
    Odpowiedz o = wyslij_komende(cmd, 6);

    if (!o.odebrana) return 0xFF;

    if (o.kod == 0x00 && o.ile_danych >= 4)
    {
        *numer   = (uint16_t)((o.dane[0] << 8) | o.dane[1]);
        *pewnosc = (uint16_t)((o.dane[2] << 8) | o.dane[3]);
    }

    return o.kod;
}

//--------------------------------------------------------------------+

int main()
{
    stdio_init_all();

    uart_init(uart0, 57600);
    gpio_set_function(0, GPIO_FUNC_UART);            // GP0 = TX  -> RX czujnika
    gpio_set_function(1, GPIO_FUNC_UART);            // GP1 = RX  <- TX czujnika
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);

    gpio_init(LED_GREEN); gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_init(LED_RED);   gpio_set_dir(LED_RED,   GPIO_OUT);
    gpio_init(LED_BLUE);  gpio_set_dir(LED_BLUE,  GPIO_OUT);

    led_rgb(0, 0, 0);

    // Czekamy, az host otworzy port szeregowy - inaczej pierwsze wypisy przepadaja.
    // Po 15 sekundach idziemy dalej, zeby plytka dzialala takze bez komputera.
    for (int i = 0; i < 150 && !stdio_usb_connected(); i++)
    {
        led_rgb(0, 0, (i % 4) < 2);   // niebieski mruga: czekam na monitor
        sleep_ms(100);
    }

    printf("\n=== start ===\n");

    test_diod();          // czerwony, zielony, niebieski - po sekundzie kazdy
    sleep_ms(500);

    // Sprawdzenie polaczenia z czujnikiem.
    const uint8_t cmd_verify[5] = { 0x13, 0x00, 0x00, 0x00, 0x00 };
    if (komenda(cmd_verify, 5) != 0x00)
    {
        while (true)
        {
            printf("Brak odpowiedzi czujnika - sprawdz TX/RX, mase i zasilanie.\n");
            mrugaj(1, 0, 0, 1);
        }
    }

    printf("Czujnik odpowiada.\n");

    // ReadSysPara - parametry modulu. Najwazniejsze: pojemnosc biblioteki
    // i rejestr stanu. Jesli pojemnosc jest zerowa albo stan nietypowy,
    // to wyjasnia, dlaczego zapis do pamieci odmawia.
    {
        const uint8_t cmd_sys[1] = { 0x0F };
        debug_ramki = true;
        Odpowiedz o = wyslij_komende(cmd_sys, 1);
        debug_ramki = false;

        if (o.odebrana && o.kod == 0x00 && o.ile_danych >= 12)
        {
            printf("  rejestr stanu : 0x%04X\n", (o.dane[0] << 8) | o.dane[1]);
            printf("  typ czujnika  : 0x%04X\n", (o.dane[2] << 8) | o.dane[3]);
            printf("  pojemnosc     : %d wzorcow\n", (o.dane[4] << 8) | o.dane[5]);
            printf("  poziom bezp.  : %d\n",        (o.dane[6] << 8) | o.dane[7]);
            printf("  rozmiar pakietu: %d\n",       (o.dane[12] << 8) | o.dane[13]);
        }
    }

    // Czyscimy biblioteke na wypadek, gdyby byla w dziwnym stanie.
    {
        const uint8_t cmd_del[1] = { 0x0D };   // DeleteAll
        printf("DeleteAll -> kod 0x%02X\n", komenda(cmd_del, 1));
    }

    proby_odblokowania();

    // Ile palcow jest juz zapisanych? Od tego zalezy, co program teraz zrobi.
    int zapisanych = 0;
    {
        const uint8_t cmd_count[1] = { 0x1D };       // TemplateCount
        Odpowiedz o = wyslij_komende(cmd_count, 1);
        if (o.odebrana && o.kod == 0x00 && o.ile_danych >= 2)
            zapisanych = (o.dane[0] << 8) | o.dane[1];
    }

    // Pusta pamiec - rejestrujemy palec. Inaczej od razu rozpoznajemy.
    if (zapisanych == 0)
    {
        int     krok = 0;
        uint8_t kod  = 0xFF;

        // Najpierw AutoEnroll - omija Store, ktory na tym egzemplarzu odmawia.
        kod = auto_rejestracja();
        if (kod == 0x00) krok = 0;

        // Trzy podejscia klasyczna droga. Najczestszy blad (0x0A - przylozenia
        // sie roznia) wystarczy powtorzyc, nie ma powodu konczyc po pierwszej probie.
        for (int proba = 0; kod != 0x00 && proba < 3; proba++)
        {
            printf("Rejestracja, podejscie %d z 3...\n", proba + 1);

            kod = zarejestruj_palec(&krok);
            if (kod == 0x00) break;

            printf("  nieudane: krok %d, kod 0x%02X\n", krok, kod);

            // Dwa dlugie czerwone blyski = nie wyszlo, sprobujmy jeszcze raz.
            for (int i = 0; i < 2; i++)
            {
                led_rgb(1, 0, 0);  sleep_ms(600);
                led_rgb(0, 0, 0);  sleep_ms(300);
            }

            czekaj_na_zdjecie_palca();
            sleep_ms(500);
        }

        // Trzy podejscia po dwa przylozenia nie wyszly - rejestrujemy z jednego.
        if (kod != 0x00)
        {
            printf("Dwa przylozenia nie wyszly (krok %d, kod 0x%02X). Probuje z jednego.\n",
                   krok, kod);
            czekaj_na_zdjecie_palca();
            sleep_ms(500);
            kod = zarejestruj_palec_prosty(&krok);
        }

        if (kod == 0x00)
        {
            printf("Palec zapisany pod numerem %d.\n", ID_PALCA);
            led_rgb(0, 1, 0);                        // zielony: zapisano
            sleep_ms(3000);
        }
        else
        {
            // Wymrugaj, gdzie i na czym sie wysypalo:
            // najpierw zielony tyle razy, ile wynosi numer kroku,
            // potem czerwony tyle razy, ile wynosi kod bledu.
            while (true)
            {
                printf("Rejestracja nieudana: krok %d, kod 0x%02X\n", krok, kod);

                mrugaj(0, 1, 0, krok);
                sleep_ms(800);

                if (kod == 0xFF)
                {
                    // Czujnik nie odpowiedzial - trzy dlugie blyski zamiast liczenia.
                    for (int i = 0; i < 3; i++)
                    {
                        led_rgb(1, 0, 0);  sleep_ms(800);
                        led_rgb(0, 0, 0);  sleep_ms(400);
                    }
                }
                else
                {
                    mrugaj(1, 0, 0, kod);
                }

                sleep_ms(2000);
            }
        }
    }

    // Petla rozpoznawania. Zielony mruga w oczekiwaniu na palec.
    int licznik = 0;

    while (true)
    {
        led_rgb(0, (licznik % 6) < 1, 0);             // krotki blysk co ~600 ms
        licznik++;

        uint16_t numer = 0, pewnosc = 0;
        uint8_t  kod = rozpoznaj_palec(&numer, &pewnosc);

        if (kod == 0x00)
        {
            printf("Rozpoznano palec nr %u, pewnosc %u\n", numer, pewnosc);
            led_rgb(0, 1, 0);                         // zielony ciagly: rozpoznano
            sleep_ms(2000);
            czekaj_na_zdjecie_palca();
        }
        else if (kod == 0x02 || kod == 0xFF)
        {
            // Brak palca albo chwilowa cisza - czekamy dalej.
            sleep_ms(100);
        }
        else
        {
            printf("Nie rozpoznano, kod 0x%02X\n", kod);
            led_rgb(1, 0, 0);                         // czerwony: nie rozpoznano
            sleep_ms(2000);
            czekaj_na_zdjecie_palca();
        }
    }
}
