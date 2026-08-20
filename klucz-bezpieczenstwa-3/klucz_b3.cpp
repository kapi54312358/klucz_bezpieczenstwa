/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include "usb_descriptors.h"
#include "haslo_prywatne.h"


#ifndef LED_GREEN
#define LED_GREEN 2
#endif

#ifndef LED_RED
#define LED_RED 3
#endif

#ifndef SWITCH_PIN
#define SWITCH_PIN 4
#endif

#ifndef LED_DELAY
#define LED_DELAY 100
#endif

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);

static uint32_t zielona_start = 0;   // kiedy skonczylo sie wpisywanie
static bool     zielona_akt   = false;

#if TUSB_VERSION_NUMBER > 1800
// board_millis has been removed from tinyusb. Use tusb_time_millis_api instead
#define board_millis tusb_time_millis_api
#endif

//--------------------------------------------------------------------+
// Added functions that control device
//--------------------------------------------------------------------+

// Zwraca true tylko w tym jednym cyklu, w ktorym suwak przeszedl w pozycje aktywna.
// Pull-up: suwak rozwarty daje 1, zwarty do masy daje 0 - stad negacja.
// Stan poprzedni trzymany jest w static, wiec funkcja nie potrzebuje argumentow.
static bool switch_triggered(void)
{
  static bool last = false;

  bool now  = !gpio_get(SWITCH_PIN);
  bool edge = now && !last;

  last = now;
  return edge;
}

//zapisanie hasła i indeksu który idzie przez tablice
// HASLO jest zdefiniowane w haslo_prywatne.h, ktory nie trafia do repozytorium.
static const char haslo[] = HASLO;
static size_t     haslo_idx = 0;      // przy ktorym znaku jestesmy
static bool       wpisywanie = false; // czy wpisywanie trwa

/*------------- MAIN -------------*/
int main(void)
{
  board_init();
  stdio_init_all();

  //initiation green colour of led
  gpio_init(LED_GREEN);
  gpio_set_dir(LED_GREEN, GPIO_OUT);

  //initiation red colour of led
  gpio_init(LED_RED);
  gpio_set_dir(LED_RED, GPIO_OUT);
   
  // initiation switch
  gpio_init(SWITCH_PIN);
  gpio_set_dir(SWITCH_PIN, GPIO_IN);
  gpio_pull_up(SWITCH_PIN);

  // init device stack on configured roothub port
  const tusb_rhport_init_t rh_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUD_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL
  };
  TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &rh_init));
  board_init_after_tusb();

  while (1)
  {
    tud_task(); // tinyusb device task
    led_blinking_task();

    // Odczyt suwaka i wysylanie raportow siedzi w hid_task(),
    // zeby wszystko chodzilo w jednym rytmie 10 ms.
    hid_task();
  }
}

// Zamienia znak ASCII na kod klawisza HID.
// Zwraca false, jesli znaku nie da sie wpisac na ukladzie US.
static bool char_to_key(char c, uint8_t &keycode, bool &shift)
{
  static uint8_t const conv_table[128][2] = { HID_KEYCODE_TO_ASCII };

  for (uint8_t i = 0; i < 128; i++)
  {
    if (conv_table[i][0] == c) { keycode = i; shift = false; return true; }
    if (conv_table[i][1] == c) { keycode = i; shift = true;  return true; }
  }
  return false;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  if (report_id != REPORT_ID_KEYBOARD) return;

  // Pamieta, czy w poprzednim cyklu wyslalismy wcisniecie klawisza.
  static bool has_keyboard_key = false;

  // Zbocze suwaka rozpoczyna wpisywanie od pierwszego znaku.
  // Gdy wpisywanie juz trwa, kolejne zbocza sa ignorowane.
  if ( btn && !wpisywanie )
  {
    wpisywanie = true;
    haslo_idx  = 0;
  }

  // Nic nie wpisujemy - upewnij sie tylko, ze klawisze sa puszczone.
  if ( !wpisywanie )
  {
    if (has_keyboard_key) tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
    has_keyboard_key = false;
    return;
  }

  // W poprzednim cyklu byl klawisz wcisniety - teraz go puszczamy.
  // Bez tego host widzialby jeden przytrzymany klawisz zamiast kolejnych znakow.
  if (has_keyboard_key)
  {
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
    has_keyboard_key = false;
    return;
  }

  // Koniec napisu - konczymy wpisywanie.
  if (haslo[haslo_idx] == '\0')
  {
    wpisywanie = false;
    zielona_start = board_millis();
    zielona_akt   = true;
    return;
  }

  // Wysylamy wcisniecie biezacego znaku.
  uint8_t kc;
  bool    shift;

  if ( char_to_key(haslo[haslo_idx], kc, shift) )
  {
    uint8_t keycode[6] = { 0 };
    keycode[0] = kc;

    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, shift ? KEYBOARD_MODIFIER_LEFTSHIFT : 0, keycode);
    has_keyboard_key = true;
  }

  // Znaku spoza ukladu US nie da sie wyslac - pomijamy go i idziemy dalej.
  haslo_idx++;
}

// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

  uint32_t const btn = switch_triggered() ? 1 : 0;


  // Remote wakeup
  if ( tud_suspended() && btn )
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  }else
  {
    // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
    send_hid_report(REPORT_ID_KEYBOARD, btn);
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) len;

  uint8_t next_report_id = report[0] + 1u;

  if (next_report_id < REPORT_ID_COUNT)
  {
    send_hid_report(next_report_id, board_button_read());
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      }else
      {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

void led_set(int led_number,bool on)
{
    gpio_put(led_number, !on);
}

void led_blinking_task(void)
{
  if (zielona_akt && (board_millis() - zielona_start >= 2000))
  zielona_akt = false;

  if(wpisywanie or zielona_akt)
  {
    led_set(LED_GREEN, 1);
    led_set(LED_RED, 0);
  }
  else
  {
    led_set(LED_GREEN, 0);
    led_set(LED_RED, 1);
  }
}