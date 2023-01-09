/*
 * This replaces the normal pressure gauge code in the ROM.
 */
#include "stubs.h"
#include "state.h"

int start(void) {
	// therapy manager variable dictionaries
	float * const fvars	= (void*) 0x2000e948;
	int * const ivars	= (void*) 0x2000e750;

	// don't do anything if we are not in an active therapy mode
	if (ivars[0x6f] == 0) return 0;

	// break out of the current clipping so we can drawon the entire screen
	unsigned * const color_ptr = (unsigned*)(gui_context + 60);
	short * const clip = (short*)(gui_context + 8);
	short * const xOff = (short*)(gui_context + 76);
	short * const yOff = (short*)(gui_context + 78);
	const short old_x0 = clip[0];
	const short old_y0 = clip[1];
	const short old_x1 = clip[2];
	const short old_y1 = clip[3];
	const short old_xOff = *xOff;
	const short old_yOff = *yOff;
	const unsigned old_color = *color_ptr;
	clip[0] = 0;
	clip[1] = 0;
	clip[2] = 0x1000;
	clip[3] = 0x1000;
	*xOff = 0;
	*yOff = 0;

// Causes the device to crash and reboot
#if 0
	GUI_SetColor(0x8);
	GUI_FillRect(0, 130, 200, 160);

	GUI_SetColor(0xFF0000);
	GUI_SetFont(font_16);
	//static const char __attribute__((__section__(".text"))) msg[] = "Hello, world!";
	//static const char __attribute__((__section__(".text"))) fmt[] = "%d.%02d";
	GUI_DispStringAt("Hello, world", 10, 130);
	char buf[16];
	int flow = fvars[1] * 100;
	snprintf(buf, sizeof(buf), "%d.%02d", flow / 100, flow % 100);
	GUI_SetColor(0x00FF00);
	GUI_DispStringAt(buf, 40, 150);
#endif


	// Draw a strip chart
	const int width = 240;
	const int center = 190;
	const int height = 80;
	// const int vscale = 2; 

	const unsigned pos_x = state->sample;
	// const unsigned pos_x = (tim_read_tim5()/1000) % width; 

	// actual pressure 0 - 32, scaled to -30 to 30
	int value = 2 * fvars[2] - height/2;
	int command = 2 * fvars[0x2a] - height/2;

	if (value < -height/2) value = -height/2;
	if (value > +height/2) value = +height/2;

	GUI_SetColor(0x0);
	LCD_FillRect(state->sample, center - height/2, state->sample + 8, center + height/2); // bgr?

	// draw 0, 5, 10, 15, 20 very faintly
	GUI_SetColor(0x202020);
	LCD_DrawPixel(pos_x, center + height/2);
	LCD_DrawPixel(pos_x, center - (2 * 10 - height/2));
	LCD_DrawPixel(pos_x, center - (2 * 20 - height/2));
	LCD_DrawPixel(pos_x, center - (2 * 30 - height/2));

	// Draw the last breath's tidal volume
	// GUI_SetColor(0xffff00);
	// LCD_DrawPixel(state->sample, center + height/2 - (int)(fvars[0xBE] / 25.0f) );

	// Draw the cumulative volume stored in unused variable
	// GUI_SetColor(0xffff00);
	// LCD_DrawPixel(pos_x, center + height/2 - (int)(fvars[0x19] / 25.0f) );

	// draw the current commanded pressure faintly
	GUI_SetColor(0x0000F0);
	LCD_DrawPixel(pos_x, center - command);

	// draw the current actual pressure in bright green
	GUI_SetColor(0x00FF00);
	LCD_DrawPixel(pos_x, center - value);

	// state->last_value = value;
	state->sample = (pos_x + 1) % width;

	// restore the old clipping rectangle
	clip[0] = old_x0;
	clip[1] = old_y0;
	clip[2] = old_x1;
	clip[3] = old_y1;
	*xOff = old_xOff;
	*yOff = old_yOff;
	*color_ptr = old_color;

	return 1;
}
