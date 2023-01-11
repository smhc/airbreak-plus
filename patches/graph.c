/*
 * This replaces the normal pressure gauge code in the ROM.
 */
#include "stubs.h"

// Replaces `gui_fill_rect_set_colors`
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

	// Draw a strip chart
	const int width = 240;
	const int top = 155; // 150--230
	const int bottom = 235;
	const int height = bottom - top;

	const float PRESSURE_MAX = 20.0f;
	const float vscale = (height / PRESSURE_MAX); 
	
	const unsigned pos_x = (ivars[0] / 7) % width; // ~14px per second (unit of timer is 10ms)

	int pressure = vscale * fvars[1];    // Current pressure. fvars[2] also has pressure, but it's overscaled?
	int command  = vscale * fvars[0x2a]; // Prescribed target

	if (pressure < 0) pressure = 0;
	if (pressure > height) pressure = height;
	if (command < 0) command = 0;
	if (command > height) command = height;

	GUI_SetColor(0x000000); // Black BGR
	LCD_FillRect(pos_x, top, pos_x + 8, bottom);

	// draw 0, 5, 10, 15, 20 very faintly
	GUI_SetColor(0x202020);
	LCD_DrawPixel(pos_x, bottom);
	LCD_DrawPixel(pos_x, bottom - 5 * vscale);
	LCD_DrawPixel(pos_x, bottom - 10 * vscale);
	LCD_DrawPixel(pos_x, bottom - 15 * vscale);
	LCD_DrawPixel(pos_x, bottom - 20 * vscale);

	// draw the current commanded pressure faintly
	GUI_SetColor(0x0000F0);
	LCD_DrawPixel(pos_x, bottom - command);

	// draw the current actual pressure in bright green
	GUI_SetColor(0x00FF00);
	LCD_DrawPixel(pos_x, bottom - pressure);

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


// Causes the device to crash and reboot
/*
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
*/