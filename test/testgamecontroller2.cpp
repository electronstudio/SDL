#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <Windows.h>

#include "SDL.h"

#define MAX_CONTROLLERS 256
#define AXIS_DEADZONE 0.25
#define AXIS_HAT_THRESHOLD 18000
#define AXIS_TRIGGER_THRESHOLD_DELTA 4000
#define AXIS_TRIGGER_THRESHOLD1 (16384 + AXIS_TRIGGER_THRESHOLD_DELTA)
#define AXIS_TRIGGER_THRESHOLD2 (16384 - AXIS_TRIGGER_THRESHOLD_DELTA)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define SAFEFREE(x) if ((x) != NULL) { free(x); (x) = NULL; }
#define callocTyped(type, count) ((type*)calloc(sizeof(type), count))
#ifndef MIN
#	define MIN(a, b) ((a)<(b)?(a):(b))
#	define MAX(a, b) ((a)>(b)?(a):(b))
#endif
#ifndef CLAMP
#	define CLAMP(x, mn, mx) (((x)<(mn))?(mn):(((x)>(mx))?(mx):(x)))
#endif

// #define TRIGGER_AS_BUTTON
#ifdef TRIGGER_AS_BUTTON
#define SDL_CONTROLLER_BUTTON_LEFT_TRIGGER SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_CONTROLLER_BUTTON_RIGHT_TRIGGER SDL_CONTROLLER_BUTTON_DPAD_DOWN
#endif

#define va_printf_start(buf, fmt)				\
    va_list va;                                 \
	char stack[2048];                           \
	char *buf;									\
	size_t buf_len;								\
	va_start(va, fmt);							\
	va_list vac;								\
	va_copy(vac, va);							\
	int len = _vscprintf(fmt, vac) + 1;			\
	va_end(vac);								\
	if (len >= ARRAY_SIZE(stack)) {				\
		buf = (char*)malloc(len);				\
		buf_len = len;							\
	} else {									\
		buf = stack;							\
		buf_len = ARRAY_SIZE(stack);			\
	}											\
	vsprintf_s(buf, buf_len, fmt, va);			\
	va_end(va);

#define va_printf_end(buf) \
	if (buf != stack) {    \
		free(buf);         \
	}


typedef struct JoystickState {
	SDL_Joystick *joystick;
	SDL_GameController *gamecontroller;
	SDL_JoystickID sdl_instance_id;
	const char *device_name;
	bool has_left_trigger_bind;
	int dir_states[4]; // For button -> directional mappings
	bool axis_states[2]; // For axis -> button mappings
#ifdef TRIGGER_AS_BUTTON
	int trigger_button;
#endif

	int num_axes;
	float *axes;
	int num_hats;
	float *hats;
	int num_buttons;
	int *buttons_down;
	int *buttons_press_count;
} JoystickState;
JoystickState joystick_state[MAX_CONTROLLERS];

void reinitJoysticks(void)
{
	bool found[MAX_CONTROLLERS] = { 0 };
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		SDL_Joystick *joy = SDL_JoystickOpen(i);
		SDL_JoystickID id = SDL_JoystickInstanceID(joy);
		if (id < 0) {
			continue;
		}
		bool was_found = false;
		int free_device_id = -1;
		// Starting at 1 to reserve ID 0 for errors, etc
		for (int jj = 1; jj < MAX_CONTROLLERS; jj++) {
			if (joystick_state[jj].joystick) {
				if (joystick_state[jj].sdl_instance_id == id) {
					assert(!was_found);
					was_found = true;
					found[jj] = true;
				}
			} else if (free_device_id == -1) {
				free_device_id = jj;
			}

		}
		if (was_found) {
			// Already in there
			SDL_JoystickClose(joy);
			continue;
		}
		assert(free_device_id != -1);
		int device_id = free_device_id;
		JoystickState &js = joystick_state[device_id];
		js.sdl_instance_id = id;
		js.joystick = joy;
		js.device_name = SDL_JoystickName(joy);
		if (SDL_IsGameController(i)) {
			js.gamecontroller = SDL_GameControllerOpen(i);
			SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForAxis(js.gamecontroller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
			if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) {
				js.has_left_trigger_bind = true;
			} else {
				js.has_left_trigger_bind = false;
			}
			js.device_name = SDL_GameControllerName(js.gamecontroller);

			js.num_axes = 6;
			js.num_hats = 1;
			js.num_buttons = SDL_CONTROLLER_BUTTON_MAX - 4;

#ifdef TRIGGER_AS_BUTTON
			js.num_axes -= 2;
			js.trigger_button = js.num_buttons;
			js.num_buttons += 2;
#endif

		} else {
			js.num_axes = SDL_JoystickNumAxes(joy);
			js.num_hats = SDL_JoystickNumHats(joy);
			js.num_buttons = SDL_JoystickNumButtons(joy);
#ifdef TRIGGER_AS_BUTTON
			// If odd, treat last axis as triggers
			if (js.num_axes & 1) {
				js.num_axes--;
				js.trigger_button = js.num_buttons;
				js.num_buttons += 2;
			}
#endif
		}

		js.axes = callocTyped(float, js.num_axes);
		js.hats = callocTyped(float, js.num_hats * 2);
		js.buttons_down = callocTyped(int, js.num_buttons);
		js.buttons_press_count = callocTyped(int, js.num_buttons);
		memset(&js.dir_states, 0, sizeof(js.dir_states));
		memset(&js.axis_states, 0, sizeof(js.axis_states));
		found[device_id] = true;
	}
	for (int ii = MAX_CONTROLLERS - 1; ii >= 0; ii--) {
		if (!found[ii]) {
			JoystickState &js = joystick_state[ii];
			if (js.joystick) {
				SDL_JoystickClose(js.joystick);
				js.joystick = NULL;
				if (js.gamecontroller) {
					SDL_GameControllerClose(js.gamecontroller);
					js.gamecontroller = NULL;
				}
				SAFEFREE(js.axes);
				SAFEFREE(js.hats);
				SAFEFREE(js.buttons_down);
				SAFEFREE(js.buttons_press_count);
			}
		}
	}
}

const char *button_names[] = {
	"A ",
	"B ",
	"X ",
	"Y ",
	"Bk",
	"G ",
	"St",
	"LS",
	"RS",
	"LB",
	"RB",
	"^ ",
	"v ",
	"< ",
	"> ",
};
const char *buttonName(int idx) {
	if (idx < ARRAY_SIZE(button_names)) {
		return button_names[idx];
	}
	return "??";
}

const char *triggerChars(float v) {
	if (v < 0)
		return "??";
	if (v == 0)
		return "0.";
	if (v < 0.2)
		return "_.";
	if (v < 0.4)
		return "-.";
	if (v < 0.6)
		return "^.";
	if (v < 0.8)
		return "._";
	if (v < 0.99)
		return ".-";
	return ".^";
}

void bufPrintf(CHAR_INFO *out_buf, int out_buf_size, WORD attributes, const char *fmt, ...) {
	va_printf_start(buf, fmt);
	int str_len = strlen(buf);
	for (int ii = 0; ii < str_len && out_buf_size; ii++) {
		out_buf->Char.AsciiChar = buf[ii];
		out_buf->Attributes = attributes;
		out_buf++;
		--out_buf_size;
	}
	va_printf_end(buf);
}

#define DEFAULT_COLOR (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
void renderFrame() {
	static int max_joysticks = 0;
	for (int ii = max_joysticks; ii < MAX_CONTROLLERS; ii++) {
		if (joystick_state[ii].joystick) {
			max_joysticks = ii;
		}
	}

	static HANDLE console;
	static int console_w = 80, console_h = 25;
	if (!console) {
		static CONSOLE_SCREEN_BUFFER_INFO console_info = { sizeof(console_info) };
		console = GetStdHandle(STD_OUTPUT_HANDLE);
		GetConsoleScreenBufferInfo(console, &console_info);
		console_w = console_info.dwSize.X;
		console_h = console_info.dwSize.Y;
	}
	static int buf_size;
	static CHAR_INFO *char_buf;
#define JOYSTICK_WIDTH 80
#define LINES_PER_JOYSTICK 3
	int board_size = max_joysticks * JOYSTICK_WIDTH * LINES_PER_JOYSTICK;
	if (board_size > buf_size) {
		buf_size = board_size;
		SAFEFREE(char_buf);
		char_buf = callocTyped(CHAR_INFO, buf_size);
	}
	for (int ii = 0; ii < buf_size; ii++)
	{
		char_buf[ii].Char.UnicodeChar = ' ';
		char_buf[ii].Attributes = DEFAULT_COLOR;
	}


	int last = 0;
	int line = 0;
	for (int ii = 1; ii <= max_joysticks; ii++) {
		JoystickState &js = joystick_state[ii];
		if (js.joystick) {
			while (ii != last + 1) {
				line += LINES_PER_JOYSTICK;
				++last;
			}
			CHAR_INFO *line0 = char_buf + line * JOYSTICK_WIDTH;
			CHAR_INFO *line1 = char_buf + (line + 1) * JOYSTICK_WIDTH;
			CHAR_INFO *line2 = char_buf + (line + 2) * JOYSTICK_WIDTH;
			CHAR_INFO *lines[] = {line0, line1, line2};
			int x = 0;
#define PREAMBLE_WIDTH 29
			bufPrintf(line0, PREAMBLE_WIDTH, DEFAULT_COLOR, "Player #%d: SDLID:%d", ii,
				js.sdl_instance_id);
			bufPrintf(line1, PREAMBLE_WIDTH, DEFAULT_COLOR, "    %s", js.gamecontroller ? "GameController" : "Joystick");
			bufPrintf(line2, PREAMBLE_WIDTH, DEFAULT_COLOR, "    %s", js.device_name);
			x += PREAMBLE_WIDTH;
#define AXIS_WIDTH 6
			for (int jj = 0; jj < js.num_axes; jj += 2) {
				if (x + AXIS_WIDTH > JOYSTICK_WIDTH) {
					continue;
				}
				float xv = js.axes[jj];
				float yv = jj < js.num_axes - 1 ? js.axes[jj + 1] : 0;
				if (jj == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
					continue;
				} else {
					bufPrintf(line0 + x, AXIS_WIDTH, DEFAULT_COLOR, "..|.. ");
					bufPrintf(line1 + x, AXIS_WIDTH, DEFAULT_COLOR, "--+-- ");
					bufPrintf(line2 + x, AXIS_WIDTH, DEFAULT_COLOR, "..|.. ");
					if (xv == 0 && yv == 0) {
						(line1 + x + 2)->Char.AsciiChar = '0';
					} else {
						int xx = CLAMP((int)((xv + 1) / 2.f * 5), 0, 4);
						int yy = CLAMP((int)((yv + 1) / 2.f * 3), 0, 2);
						lines[yy][x + xx].Char.AsciiChar = 'X';
					}
				}
				x += AXIS_WIDTH;
			}
			for (int jj = 0; jj < js.num_hats; jj += 2) {
				if (x + AXIS_WIDTH > JOYSTICK_WIDTH) {
					continue;
				}
				bufPrintf(line0 + x, AXIS_WIDTH, DEFAULT_COLOR, "..|.. ");
				bufPrintf(line1 + x, AXIS_WIDTH, DEFAULT_COLOR, "--+-- ");
				bufPrintf(line2 + x, AXIS_WIDTH, DEFAULT_COLOR, "..|.. ");
				float xv = js.hats[jj * 2];
				float yv = js.hats[jj * 2 + 1];
				if (xv == 0 && yv == 0) {
					(line1 + x + 2)->Char.AsciiChar = '0';
				} else {
					int xx = CLAMP((int)((xv + 1) / 2.f * 5), 0, 4);
					int yy = CLAMP((int)((yv + 1) / 2.f * 3), 0, 2);
					lines[yy][x + xx].Char.AsciiChar = 'X';
				}
				x += AXIS_WIDTH;
			}
			int buttons_per_line = (js.num_buttons + 2) / 3;
			int xx = 0;
			int yy = 0;
#define BUTTON_WIDTH 3
			for (int jj = 0; jj < js.num_buttons; jj++) {
				if (xx == buttons_per_line) {
					xx = 0;
					yy++;
				}
				int xoffs = x + xx * BUTTON_WIDTH;
				xx++;
				if (xoffs + BUTTON_WIDTH > JOYSTICK_WIDTH) {
					continue;
				}
				bufPrintf(lines[yy] + xoffs, BUTTON_WIDTH, (js.buttons_down[jj] || js.buttons_press_count[jj]) ?
					FOREGROUND_INTENSITY | FOREGROUND_GREEN :
					FOREGROUND_INTENSITY, "%s", buttonName(jj));
			}
			x += BUTTON_WIDTH * buttons_per_line;
			if (js.num_axes >= SDL_CONTROLLER_AXIS_TRIGGERLEFT - 1) {
				if (x + AXIS_WIDTH > JOYSTICK_WIDTH) {
					continue;
				}
				float xv = js.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT];
				float yv = SDL_CONTROLLER_AXIS_TRIGGERRIGHT < js.num_axes ? js.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] : 0;
				bufPrintf(line0 + x, AXIS_WIDTH, DEFAULT_COLOR, "LT RT ");
				bufPrintf(line1 + x, AXIS_WIDTH, DEFAULT_COLOR, ".  .  ");
				bufPrintf(line2 + x, AXIS_WIDTH, DEFAULT_COLOR, ".  .  ");
				const char *chars = triggerChars(xv);
				(line2 + x)->Char.AsciiChar = chars[0];
				(line1 + x)->Char.AsciiChar = chars[1];
				chars = triggerChars(yv);
				(line2 + x + 3)->Char.AsciiChar = chars[0];
				(line1 + x + 3)->Char.AsciiChar = chars[1];
				x += AXIS_WIDTH;
			}

			line += LINES_PER_JOYSTICK;
			last = ii;
		}
	}

	COORD dims = { (SHORT)JOYSTICK_WIDTH, (SHORT)max_joysticks * LINES_PER_JOYSTICK };
	COORD pos = { 0, 0 };
	SMALL_RECT region = { 0, 0, (SHORT)MIN(console_w, dims.X), (SHORT)MIN(console_h, dims.Y) };
	WriteConsoleOutput(console, char_buf, dims, pos, &region);
}

int joystickIdToDeviceId(int sdl_instance_id) {
	for (int ii = 0; ii < MAX_CONTROLLERS; ii++) {
		if (joystick_state[ii].joystick && joystick_state[ii].sdl_instance_id == sdl_instance_id) {
			return ii;
		}
	}
	return 0;
}

bool handleEvent(SDL_Event *evt) {
#if 0
	// Filter movement buttons on PS2 controllers coming as button presses on Windows 7 driver
	if (evt->type == SDL_JOYBUTTONDOWN || evt->type == SDL_JOYBUTTONUP)
	{
		SDL_JoyButtonEvent *jbe = &evt->jbutton;
		int which = joystickIdToDeviceId(jbe->which);
		if (!joystick_state[which].gamecontroller) {
#define HAT_BASE 12
			if (jbe->button >= HAT_BASE && jbe->button < HAT_BASE + 4)
			{
				int *dir_states = joystick_state[which].dir_states;
				dir_states[jbe->button - HAT_BASE] = (jbe->state == SDL_PRESSED);
				evt->type = SDL_JOYHATMOTION;
				SDL_JoyHatEvent *jhe = &evt->jhat;
				jhe->which = jbe->which;
				jhe->hat = 0;
				if (dir_states[0] && dir_states[1])
					jhe->value = SDL_HAT_RIGHTUP;
				else if (dir_states[1] && dir_states[2])
					jhe->value = SDL_HAT_RIGHTDOWN;
				else if (dir_states[2] && dir_states[3])
					jhe->value = SDL_HAT_LEFTDOWN;
				else if (dir_states[3] && dir_states[0])
					jhe->value = SDL_HAT_LEFTUP;
				else if (dir_states[0])
					jhe->value = SDL_HAT_UP;
				else if (dir_states[1])
					jhe->value = SDL_HAT_RIGHT;
				else if (dir_states[2])
					jhe->value = SDL_HAT_DOWN;
				else if (dir_states[3])
					jhe->value = SDL_HAT_LEFT;
				else
					jhe->value = SDL_HAT_CENTERED;
			}
		}
	}
#endif

	// Mapping GameController dpad buttons to old "hat" buttons
	if (evt->type == SDL_CONTROLLERBUTTONDOWN || evt->type == SDL_CONTROLLERBUTTONUP) {
		SDL_ControllerButtonEvent *cbe = &evt->cbutton;
		if (cbe->button >= SDL_CONTROLLER_BUTTON_DPAD_UP && cbe->button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
		{
			int which = joystickIdToDeviceId(cbe->which);
			int *dir_states = joystick_state[which].dir_states;
			int dir = cbe->button == SDL_CONTROLLER_BUTTON_DPAD_UP ? 0
				: cbe->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1
				: cbe->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 2
				: cbe->button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? 3
				: 0;
			dir_states[dir] = (cbe->state == SDL_PRESSED);
			evt->type = SDL_JOYHATMOTION;
			SDL_JoyHatEvent *jhe = &evt->jhat;
			jhe->which = cbe->which;
			jhe->hat = 0;
			if (dir_states[0] && dir_states[1])
				jhe->value = SDL_HAT_RIGHTUP;
			else if (dir_states[1] && dir_states[2])
				jhe->value = SDL_HAT_RIGHTDOWN;
			else if (dir_states[2] && dir_states[3])
				jhe->value = SDL_HAT_LEFTDOWN;
			else if (dir_states[3] && dir_states[0])
				jhe->value = SDL_HAT_LEFTUP;
			else if (dir_states[0])
				jhe->value = SDL_HAT_UP;
			else if (dir_states[1])
				jhe->value = SDL_HAT_RIGHT;
			else if (dir_states[2])
				jhe->value = SDL_HAT_DOWN;
			else if (dir_states[3])
				jhe->value = SDL_HAT_LEFT;
			else
				jhe->value = SDL_HAT_CENTERED;
		}
	}

#ifdef TRIGGER_AS_BUTTON
	// Map trigger axises to buttons (GameController)
	if (evt->type == SDL_CONTROLLERAXISMOTION) {
		SDL_ControllerAxisEvent *cae = &evt->caxis;
		if (cae->axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || cae->axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
			bool ignore = true;
			SDL_ControllerButtonEvent *cbe = &evt->cbutton;
			int which = joystickIdToDeviceId(cae->which);
			JoystickState *js = &joystick_state[which];
			if (js->has_left_trigger_bind) {
				int idx = cae->axis - SDL_CONTROLLER_AXIS_TRIGGERLEFT;
				bool down = cae->value > AXIS_HAT_THRESHOLD;
				if (js->axis_states[idx] != down) {
					js->axis_states[idx] = down;
					ignore = false;
					evt->type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
					cbe->button = js->trigger_button + idx;
					cbe->state = down;
				}
			} else {
				// both left and right trigger encoded in one value
				// neither pressed if axis = 32k
				// left trigger is pressed if axis value is > 32k, right if less
				bool left_down = cae->value > AXIS_TRIGGER_THRESHOLD1;
				if (js->axis_states[0] != left_down) {
					js->axis_states[0] = left_down;
					ignore = false;
					evt->type = left_down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
					cbe->button = js->trigger_button;
					cbe->state = left_down;
				}
				bool right_down = cae->value < AXIS_TRIGGER_THRESHOLD2;
				if (js->axis_states[1] != right_down) {
					js->axis_states[1] = right_down;
					ignore = false;
					evt->type = right_down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
					cbe->button = js->trigger_button + 1;
					cbe->state = right_down;
				}
			}
			// otherwise ignore this event
			if (ignore)
				return false;
		}
	}

	// Map trigger axises to buttons (Joystick)
	if (evt->type == SDL_JOYAXISMOTION) {
		SDL_JoyAxisEvent *jae = &evt->jaxis;
		int which = joystickIdToDeviceId(jae->which);
		JoystickState *js = &joystick_state[which];
		// OutputDebugStringf("JoyAxis %d value %d\n", jae->axis, jae->value);
		if (jae->axis == js->num_axes) {
			if (!js->gamecontroller) {
				bool ignore = true;
				SDL_ControllerButtonEvent *cbe = &evt->cbutton;
				// both left and right trigger encoded in one value
				// neither pressed if axis = 0k
				// left trigger is pressed if axis value is > 0, right if less
				bool left_down = jae->value > AXIS_HAT_THRESHOLD;
				if (js->axis_states[0] != left_down) {
					js->axis_states[0] = left_down;
					ignore = false;
					evt->type = left_down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
					cbe->button = js->trigger_button;
					cbe->state = left_down;
				}
				bool right_down = jae->value < -AXIS_HAT_THRESHOLD;
				if (js->axis_states[1] != right_down) {
					js->axis_states[1] = right_down;
					ignore = false;
					evt->type = right_down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
					cbe->button = js->trigger_button + 1;
					cbe->state = right_down;
				}
				// otherwise ignore this event
				if (ignore)
					return false;
			}
		}
	}
#endif

	switch (evt->type) {
	case SDL_JOYAXISMOTION: {
		SDL_JoyAxisEvent *jae = (SDL_JoyAxisEvent*)evt;
		int which = joystickIdToDeviceId(jae->which);
		JoystickState *js = &joystick_state[which];
		if (js->gamecontroller) {
			break;
		}
		float value = jae->value / 32768.f;
		if (value < 0) {
			value += AXIS_DEADZONE;
			value /= (1.f - AXIS_DEADZONE);
			value = CLAMP(value, -1, 0);
		} else {
			value -= AXIS_DEADZONE;
			value /= (1.f - AXIS_DEADZONE);
			value = CLAMP(value, 0, 1);
		}
		assert(jae->axis <= js->num_axes);
		js->axes[jae->axis] = value;
		break;
	}
	case SDL_JOYHATMOTION: {
		SDL_JoyHatEvent *jhe = (SDL_JoyHatEvent*)evt;
		int which = joystickIdToDeviceId(jhe->which);
		JoystickState *js = &joystick_state[which];
		// Maybe, maybe not?  Doesn't fire on Xbox controllers!
		// if (js->gamecontroller) {
		// 	break;
		// }
		float x = 0, y = 0;
		switch (jhe->value) {
		case SDL_HAT_LEFTUP:
			x = -1; y = -1;
			break;
		case SDL_HAT_UP:
			x = 0; y = -1;
			break;
		case SDL_HAT_RIGHTUP:
			x = 1; y = -1;
			break;
		case SDL_HAT_LEFT:
			x = -1; y = 0;
			break;
		case SDL_HAT_CENTERED:
			x = 0; y = 0;
			break;
		case SDL_HAT_RIGHT:
			x = 1; y = 0;
			break;
		case SDL_HAT_LEFTDOWN:
			x = -1; y = 1;
			break;
		case SDL_HAT_DOWN:
			x = 0; y = 1;
			break;
		case SDL_HAT_RIGHTDOWN:
			x = 1; y = 1;
			break;
		}
		if (jhe->hat < js->num_hats) {
			js->hats[jhe->hat * 2] = x;
			js->hats[jhe->hat * 2 + 1] = y;
		}
		break;
	}
	case SDL_CONTROLLERAXISMOTION: {
		SDL_ControllerAxisEvent *cae = &evt->caxis;
		int which = joystickIdToDeviceId(cae->which);
		JoystickState *js = &joystick_state[which];
		float value = cae->value / 32768.f;
		if (value < 0) {
			value += AXIS_DEADZONE;
			value /= (1.f - AXIS_DEADZONE);
			value = CLAMP(value, -1, 0);
		} else {
			value -= AXIS_DEADZONE;
			value /= (1.f - AXIS_DEADZONE);
			value = CLAMP(value, 0, 1);
		}
		assert(cae->axis <= js->num_axes);
		js->axes[cae->axis] = value;
		break;
	}
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP: {
		SDL_JoyButtonEvent *jbe = &evt->jbutton;
		int which = joystickIdToDeviceId(jbe->which);
		JoystickState *js = &joystick_state[which];
		if (js->gamecontroller) {
			break;
		}
		assert(jbe->button < js->num_buttons);
		if (jbe->state) {
			js->buttons_press_count[jbe->button]++;
		}
		js->buttons_down[jbe->button] = jbe->state;
		break;
	}
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP: {
		SDL_ControllerButtonEvent *cbe = &evt->cbutton;
		int which = joystickIdToDeviceId(cbe->which);
		JoystickState *js = &joystick_state[which];
		assert(cbe->button < js->num_buttons);
		if (cbe->state) {
			js->buttons_press_count[cbe->button]++;
		}
		js->buttons_down[cbe->button] = cbe->state;
		break;
	}
	case SDL_QUIT:
		return true;
	}
	return false;
}

void resetTopOfFrame() {
	for (int ii = 0; ii < MAX_CONTROLLERS; ii++) {
		if (joystick_state[ii].joystick) {
			memset(joystick_state[ii].buttons_press_count, 0, joystick_state[ii].num_buttons * sizeof(joystick_state[ii].buttons_press_count[0]));
		}
	}
}

bool loop() {
	resetTopOfFrame();
	SDL_PumpEvents();
	SDL_Event evt;
	while (SDL_PollEvent(&evt)) {
		if (handleEvent(&evt)) {
			return true;
		}
	}

	renderFrame();

	return false;
}

int main(int argc, char *argv[])
{
	// SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");

	if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");

	reinitJoysticks();

	while (!loop()) {
		SDL_Delay(16);
	}
	return 0;
}