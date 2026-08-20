#include "pico/stdlib.h"

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

void led_set(int led_number,bool on)
{
    gpio_put(led_number, !on);
}

int main()
{
    stdio_init_all();
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_init(SWITCH_PIN);
    gpio_set_dir(SWITCH_PIN, GPIO_IN);
    gpio_pull_up(SWITCH_PIN);

    while (true)
    {
        if(gpio_get(SWITCH_PIN))
        {
            led_set(LED_RED, false);
            led_set(LED_GREEN, true);
        }
        else
        {
            led_set(LED_GREEN, false);
            led_set(LED_RED, true);
        }
        sleep_ms(LED_DELAY);
    }
}