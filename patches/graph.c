/*
 * This replaces the normal pressure gauge code in the ROM.
 */
#include "stubs.h"
#include "common_code.h"

#define DRAW_PRESSURE 1
#define DRAW_FLOW 0

STATIC float rescale(float value, float start, float end, float graph_height) {
	return map01(value, start, end) * graph_height;
}


STATIC void LCD_FillRect2(int x1, int y1, int x2, int y2) {
	int temp = 0;
	if (y1 > y2) { temp = y2; y2 = y1; y1 = temp; }
	LCD_FillRect(x1, y1, x2, y2);
}

// Replaces `gui_fill_rect_set_colors`
int MAIN start(void) {
	// don't do anything if we are not in an active therapy mode
	if (*therapy_mode == 0) return 0;

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

	#define HEIGHT_PRES 40
	#define HEIGHT_FLOW 40

	const float p_min = 0.0f;
	const float p_max = 20.0f;
	
	const unsigned pos_x = (ivars[0] / 10) % width; // ~10px per second (unit of timer is 10ms)

	int pressure = rescale(p_actual, p_min, p_max, HEIGHT_PRES);
	int command = rescale(p_command, p_min, p_max, HEIGHT_PRES);
	int error = -p_error * ( HEIGHT_PRES / (p_max-p_min) * 3.0f); // Error 

	if (breath_progress <= 0.5f) { // Active inhale
		GUI_SetColor(0x101010);
	} else {
		GUI_SetColor(0x000000);
	}
	LCD_FillRect(pos_x, top -1, pos_x + 3, bottom + 1);

	GUI_SetColor(0x404040);
	LCD_DrawPixel(pos_x, top);
	LCD_DrawPixel(pos_x, top + HEIGHT_FLOW);
	LCD_DrawPixel(pos_x, top + HEIGHT_FLOW + HEIGHT_PRES);

	float g_top = top + HEIGHT_FLOW;
	float g_bottom = top + HEIGHT_FLOW + HEIGHT_PRES;
	// float g_vscale = (p_max-p_min) / HEIGHT_PRES;

	#if HEIGHT_PRES > 0
		// draw 0, 5, 10, 15, 20 very faintly
		// GUI_SetColor(0x202020);
		// LCD_DrawPixel(pos_x, g_bottom - 5 * g_vscale);
		// LCD_DrawPixel(pos_x, g_bottom - 10 * g_vscale);
		// LCD_DrawPixel(pos_x, g_bottom - 15 * g_vscale);
		// LCD_DrawPixel(pos_x, g_bottom - 20 * g_vscale);

		// draw amplified pressure error with respect to the commanded pressure
		GUI_SetColor(0x000080);
		LCD_FillRect2(pos_x, g_bottom - command, pos_x, g_bottom - command + error );
		// draw the current commanded pressure
		GUI_SetColor(0x00FFF0);
		LCD_DrawPixel(pos_x, g_bottom - command);

	#endif

	g_top = top;
	g_bottom = top + HEIGHT_FLOW;
	// g_vscale = 1.0f / (HEIGHT_FLOW*2);

	#if HEIGHT_FLOW > 0
		GUI_SetColor(0x202020);
		LCD_DrawPixel(pos_x, g_top + HEIGHT_FLOW/2);
		// Draw unfucked flow variable
		GUI_SetColor(0x6600FF);
		LCD_DrawPixel(pos_x, g_top + HEIGHT_FLOW/2 - f_unfucked / 2);
		// Draw the leak-compensated flow variable
		GUI_SetColor(0x00FF00);
		LCD_DrawPixel(pos_x, g_top + HEIGHT_FLOW/2 - f_compensated / 2);
	#endif

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
	GUI_SetFont(font_16); // Causes device to crash and reboot
	//static const char __attribute__((__section__(".text"))) msg[] = "Hello, world!";
	//static const char __attribute__((__section__(".text"))) fmt[] = "%d.%02d";
	GUI_DispStringAt("Hello, world", 10, 130);
	char buf[16];
	int flow = fvars[1] * 100;
	snprintf(buf, sizeof(buf), "%d.%02d", flow / 100, flow % 100);
	GUI_SetColor(0x00FF00);
	GUI_DispStringAt(buf, 40, 150);
#endif

// Also tried:
GUI_SetColor(0xFFFF00);
GUI_SetTextMode(2);
GUI_SetTextAlign(0);
GUI_SetFont_default();
GUI_DispStringAt("Hello, world", 20, top + HEIGHT_FLOW + 20);
// No luck, displays nothing. Changing params of GUI_DispStringAt to short or unsigned short does nothing too.

*/