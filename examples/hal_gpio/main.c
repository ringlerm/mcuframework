#include <cr_section_macros.h>
#include "target.h"
#include "hal_gpio.h"

// TODO: insert other include files here

// TODO: insert other definitions and declarations here
#define GPIO_LED_RED         ((hal_gpio_pin_t){1u, 2u})
#define GPIO_LED_BLUE         ((hal_gpio_pin_t){1u, 1u})
#define GPIO_LED_GREEN         ((hal_gpio_pin_t){1u, 0u})
#define GPIO_BUTTON         ((hal_gpio_pin_t){0u, 4u})

int main(void) {

	hal_gpio_level_t button_state, button_state_prev;
	hal_gpio_pin_configure(GPIO_LED_RED, HAL_GPIO_MODE_OUTPUT_PUSHPULL);
	hal_gpio_pin_configure(GPIO_LED_BLUE, HAL_GPIO_MODE_OUTPUT_PUSHPULL);
	hal_gpio_pin_configure(GPIO_LED_GREEN, HAL_GPIO_MODE_OUTPUT_PUSHPULL);
	hal_gpio_pin_configure(GPIO_BUTTON, HAL_GPIO_MODE_INPUT_PULLUP);

	hal_gpio_pin_write(GPIO_LED_RED, 1);
	hal_gpio_pin_write(GPIO_LED_BLUE, 1);
	hal_gpio_pin_write(GPIO_LED_GREEN, 0);
	hal_gpio_pin_read(GPIO_BUTTON, &button_state);
	button_state_prev = button_state;

    // TODO: insert code here

    while(1) {
    	static uint8_t i=0;
    	hal_gpio_pin_read(GPIO_BUTTON, &button_state);
    	if(button_state_prev && !button_state)
    	{
    		i++;
    		if(i>2)
    		{
    			i=0;
    		}
    	}
    	button_state_prev = button_state;
    	switch(i)
		{
    		case 0:
    			hal_gpio_pin_write(GPIO_LED_RED, 0);
				hal_gpio_pin_write(GPIO_LED_BLUE, 1);
				hal_gpio_pin_write(GPIO_LED_GREEN, 1);
				break;
    		case 1:
    			hal_gpio_pin_write(GPIO_LED_RED, 1);
    			hal_gpio_pin_write(GPIO_LED_BLUE, 1);
    			hal_gpio_pin_write(GPIO_LED_GREEN, 0);
    			break;
    		case 2:
    			hal_gpio_pin_write(GPIO_LED_RED, 1);
    			hal_gpio_pin_write(GPIO_LED_BLUE, 0);
    			hal_gpio_pin_write(GPIO_LED_GREEN, 1);
    			break;

		}
    }
    return 0 ;
}
