/*
  Simple DirectMedia Layer
  Copyright (C) 2019 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/
/*
  RAWINPUT Joystick API for better handling XInput-capable devices on Windows.

  XInput is limited to 4 devices.
  Windows.Gaming.Input does not get inputs from XBox One controllers when not in the foreground.
  DirectInput does not get inputs from XBox One controllers when not in the foreground, nor rumble or accurate triggers.
  RawInput does not get rumble or accurate triggers.

  So, combine them as best we can!
*/
#include "../../SDL_internal.h"

#if SDL_JOYSTICK_RAWINPUT

#include "SDL_assert.h"
#include "SDL_endian.h"
#include "SDL_hints.h"
#include "SDL_log.h"
#include "SDL_mutex.h"
#include "../SDL_sysjoystick.h"
#include "../../core/windows/SDL_windows.h"
#include "../hidapi/SDL_hidapijoystick_c.h"

#ifndef SDL_JOYSTICK_HIDAPI_XBOX360
#error RAWINPUT requires the XBOX360 HIDAPI driver
#endif

#ifndef RIDEV_EXINPUTSINK
#define RIDEV_EXINPUTSINK       0x00001000
#define RIDEV_DEVNOTIFY         0x00002000
#endif

#ifndef WM_INPUT_DEVICE_CHANGE
#define WM_INPUT_DEVICE_CHANGE          0x00FE
#endif
#ifndef WM_INPUT
#define WM_INPUT                        0x00FF
#endif
#ifndef GIDC_ARRIVAL
#define GIDC_ARRIVAL             1
#define GIDC_REMOVAL             2
#endif


/* #define DEBUG_RAWINPUT */

#define USB_PACKET_LENGTH   64

#define SDL_callocStruct(type) (type *)SDL_calloc(1, sizeof(type))
#define SDL_callocStructs(type, count) (type *)SDL_calloc((count), sizeof(type))

#define USAGE_PAGE_GENERIC_DESKTOP 0x0001
#define USAGE_JOYSTICK 0x0004
#define USAGE_GAMEPAD 0x0005
#define USAGE_MULTIAXISCONTROLLER 0x0008


/* external variables referenced. */
extern HWND SDL_HelperWindow;


static SDL_HIDAPI_DeviceDriver *SDL_RAWINPUT_drivers[] = {
#ifdef SDL_JOYSTICK_HIDAPI_XBOX360
    &SDL_HIDAPI_DriverXbox360,
#endif
};

static SDL_bool SDL_RAWINPUT_inited = SDL_FALSE;
static int SDL_RAWINPUT_numjoysticks = 0;
static SDL_bool SDL_RAWINPUT_need_pump = SDL_TRUE;

static void RAWINPUT_JoystickDetect(void);
static void RAWINPUT_PumpMessages(void);
static SDL_bool RAWINPUT_IsDeviceSupported(Uint16 vendor_id, Uint16 product_id, Uint16 version);

typedef struct _SDL_RAWINPUT_Device
{
    SDL_JoystickID instance_id;
    char *name;
    Uint16 vendor_id;
    Uint16 product_id;
    Uint16 version;
    SDL_JoystickGUID guid;
    Uint16 usage_page;
    Uint16 usage;
    SDL_HIDAPI_DeviceDriver *driver;

    HANDLE hDevice;
    SDL_Joystick *joystick;

    struct _SDL_RAWINPUT_Device *next;
} SDL_RAWINPUT_Device;

struct joystick_hwdata
{
    SDL_HIDAPI_DeviceDriver *driver;
    void *context;
    SDL_RAWINPUT_Device *device;

    SDL_mutex *mutex;
};

SDL_RAWINPUT_Device *SDL_RAWINPUT_devices;

static const Uint16 subscribed_devices[] = {
    USAGE_GAMEPAD,
    /* Don't need Joystick for any devices we're handling here (XInput-capable)
    USAGE_JOYSTICK,
    USAGE_MULTIAXISCONTROLLER,
    */
};

SDL_bool RAWINPUT_AllXInputDevicesSupported() {
    UINT i, device_count = 0;
    PRAWINPUTDEVICELIST devices;
    SDL_bool any_unsupported = SDL_FALSE;

    if ((GetRawInputDeviceList(NULL, &device_count, sizeof(RAWINPUTDEVICELIST)) == -1) || (!device_count)) {
        return SDL_FALSE;
    }

    devices = (PRAWINPUTDEVICELIST)SDL_malloc(sizeof(RAWINPUTDEVICELIST) * device_count);
    if (devices == NULL) {
        return SDL_FALSE;
    }

    if (GetRawInputDeviceList(devices, &device_count, sizeof(RAWINPUTDEVICELIST)) == -1) {
        SDL_free(devices);
        return SDL_FALSE;
    }

    for (i = 0; i < device_count; i++) {
        RID_DEVICE_INFO rdi;
        char devName[128];
        UINT rdiSize = sizeof(rdi);
        UINT nameSize = SDL_arraysize(devName);

        rdi.cbSize = sizeof(rdi);
        if ((devices[i].dwType == RIM_TYPEHID) &&
            (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICEINFO, &rdi, &rdiSize) != ((UINT)-1)) &&
            (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME, devName, &nameSize) != ((UINT)-1)) &&
            (SDL_strstr(devName, "IG_") != NULL)
        ) {
            /* XInput-capable */
            if (!RAWINPUT_IsDeviceSupported((Uint16)rdi.hid.dwVendorId, (Uint16)rdi.hid.dwProductId, (Uint16)rdi.hid.dwVersionNumber)) {
                /* But not supported, probably Valve virtual controller */
                any_unsupported = SDL_TRUE;
            }
        }
    }
    SDL_free(devices);
    if (any_unsupported) {
        /* This happens with Valve virtual controllers that shows up in the RawInputDeviceList, but do not
            generate WM_INPUT events, so we must use XInput or DInput to read from it, and with XInput if we
            have some supported and some not, we can't easily tell which device is actually showing up in
            RawInput, so we must just disable RawInput for now.  Additionally, if these unsupported devices
			are locally connected, they still show up in RawInput under a *different* HID path, with
			different vendor/product IDs, so there's no way to reconcile. */
#ifdef DEBUG_RAWINPUT
        SDL_Log("Found some supported and some unsupported XInput devices, disabling RawInput\n");
#endif
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

static int
RAWINPUT_JoystickInit(void)
{
    int ii;
    RAWINPUTDEVICE rid[SDL_arraysize(subscribed_devices)];
    SDL_assert(!SDL_RAWINPUT_inited);
    SDL_assert(SDL_HelperWindow);

    if (!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_RAWINPUT, SDL_TRUE))
        return -1;

    if (!RAWINPUT_AllXInputDevicesSupported()) {
        return -1;
    }

    for (ii = 0; ii < SDL_arraysize(subscribed_devices); ii++) {
        rid[ii].usUsagePage = USAGE_PAGE_GENERIC_DESKTOP;
        rid[ii].usUsage = subscribed_devices[ii];
        rid[ii].dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK; /* Receive messages when in background, including device add/remove */
        rid[ii].hwndTarget = SDL_HelperWindow;
    }

    if (!RegisterRawInputDevices(rid, SDL_arraysize(rid), sizeof(RAWINPUTDEVICE))) {
        SDL_SetError("Couldn't initialize RAWINPUT");
        return -1;
    }

    SDL_RAWINPUT_inited = SDL_TRUE;

    RAWINPUT_JoystickDetect();
    RAWINPUT_PumpMessages();
    return 0;
}

static int
RAWINPUT_JoystickGetCount(void)
{
    return SDL_RAWINPUT_numjoysticks;
}

static SDL_RAWINPUT_Device *
RAWINPUT_DeviceFromHandle(HANDLE hDevice)
{
    SDL_RAWINPUT_Device *curr;

    for (curr = SDL_RAWINPUT_devices; curr; curr = curr->next) {
        if (curr->hDevice == hDevice)
            return curr;
    }
    return NULL;
}

static SDL_HIDAPI_DeviceDriver *
RAWINPUT_GetDeviceDriver(SDL_RAWINPUT_Device *device)
{
    int i;

    if (SDL_ShouldIgnoreJoystick(device->name, device->guid)) {
        return NULL;
    }

    if (device->usage_page && device->usage_page != USAGE_PAGE_GENERIC_DESKTOP) {
        return NULL;
    }
    if (device->usage && device->usage != USAGE_JOYSTICK && device->usage != USAGE_GAMEPAD && device->usage != USAGE_MULTIAXISCONTROLLER) {
        return NULL;
    }

    for (i = 0; i < SDL_arraysize(SDL_RAWINPUT_drivers); ++i) {
        SDL_HIDAPI_DeviceDriver *driver = SDL_RAWINPUT_drivers[i];
        if (/*driver->enabled && */driver->IsSupportedDevice(device->vendor_id, device->product_id, device->version, -1)) {
            return driver;
        }
    }
    return NULL;
}

static void
RAWINPUT_AddDevice(HANDLE hDevice)
{
#define CHECK(exp) { if(!(exp)) goto err; }
    SDL_RAWINPUT_Device *device = NULL;
    RID_DEVICE_INFO rdi;
    UINT rdi_size = sizeof(rdi);
    char dev_name[128];
    const char *name;
    UINT name_size = SDL_arraysize(dev_name);
    SDL_RAWINPUT_Device *curr, *last;

    SDL_assert(!RAWINPUT_DeviceFromHandle(hDevice));

    /* Figure out what kind of device it is */
    CHECK(GetRawInputDeviceInfoA(hDevice, RIDI_DEVICEINFO, &rdi, &rdi_size) != (UINT)-1);
    CHECK(rdi.dwType == RIM_TYPEHID);

    /* Get the device "name" (HID Path) */
    CHECK(GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, dev_name, &name_size) != (UINT)-1);
    /* Only take XInput-capable devices */
    CHECK(SDL_strstr(dev_name, "IG_") != NULL);

    CHECK(device = SDL_callocStruct(SDL_RAWINPUT_Device));
    device->hDevice = hDevice;
    device->instance_id = -1;
    device->vendor_id = (Uint16)rdi.hid.dwVendorId;
    device->product_id = (Uint16)rdi.hid.dwProductId;
    device->version = (Uint16)rdi.hid.dwVersionNumber;
    device->usage = rdi.hid.usUsage;
    device->usage_page = rdi.hid.usUsagePage;

    {
        const Uint16 vendor = device->vendor_id;
        const Uint16 product = device->product_id;
        const Uint16 version = device->version;
        Uint16 *guid16 = (Uint16 *)device->guid.data;

        *guid16++ = SDL_SwapLE16(SDL_HARDWARE_BUS_USB);
        *guid16++ = 0;
        *guid16++ = SDL_SwapLE16(vendor);
        *guid16++ = 0;
        *guid16++ = SDL_SwapLE16(product);
        *guid16++ = 0;
        *guid16++ = SDL_SwapLE16(version);
        *guid16++ = 0;

        /* Note that this is a RAWINPUT device for special handling elsewhere */
        device->guid.data[14] = 'r';
        device->guid.data[15] = 0;
    }

    if (!device->name) {
        size_t name_size = (6 + 1 + 6 + 1);
        CHECK(device->name = SDL_callocStructs(char, name_size));
        SDL_snprintf(device->name, name_size, "0x%.4x/0x%.4x", device->vendor_id, device->product_id);
    }

    CHECK(device->driver = RAWINPUT_GetDeviceDriver(device));

    name = device->driver->GetDeviceName(device->vendor_id, device->product_id);
    if (name) {
        SDL_free(device->name);
        device->name = SDL_strdup(name);
    }

#ifdef SDL_JOYSTICK_ANNOTATE_NAMES
    {
        size_t name_size = SDL_strlen(device->name) + SDL_arraysize("RAWINPUT:") + 1;
        char *name = (char *)SDL_malloc(name_size);
        if (!name) {
            SDL_free(device);
            return;
        }
        SDL_snprintf(name, name_size, "RAWINPUT:%s", device->name);
        SDL_free(device->name);
        device->name = name;
    }
#endif


#ifdef DEBUG_RAWINPUT
    SDL_Log("Adding RAWINPUT device '%s' VID 0x%.4x, PID 0x%.4x, version %d, handle 0x%.8x\n", device->name, device->vendor_id, device->product_id, device->version, device->hDevice);
#endif

    /* Add it to the list */
    for (curr = SDL_RAWINPUT_devices, last = NULL; curr; last = curr, curr = curr->next) {
        continue;
    }
    if (last) {
        last->next = device;
    } else {
        SDL_RAWINPUT_devices = device;
    }

    device->instance_id = SDL_GetNextJoystickInstanceID();

    ++SDL_RAWINPUT_numjoysticks;

    SDL_PrivateJoystickAdded(device->instance_id);
    return;

err:
    if (device) {
        if (device->name)
            SDL_free(device->name);
        SDL_free(device);
    }
}


static void
RAWINPUT_DelDevice(SDL_RAWINPUT_Device *device, SDL_bool send_event)
{
    SDL_RAWINPUT_Device *curr, *last;
    for (curr = SDL_RAWINPUT_devices, last = NULL; curr; last = curr, curr = curr->next) {
        if (curr == device) {
            if (last) {
                last->next = curr->next;
            } else {
                SDL_RAWINPUT_devices = curr->next;
            }

            if (device->joystick) {
                /* Detach from joystick */
                struct joystick_hwdata *hwdata = device->joystick->hwdata;
                SDL_assert(hwdata->device == device);
                hwdata->device = NULL;
                device->joystick = NULL;
            }

            if (send_event) {
                /* Need to decrement the joystick count before we post the event */
                --SDL_RAWINPUT_numjoysticks;

                SDL_PrivateJoystickRemoved(device->instance_id);
            }

#ifdef DEBUG_RAWINPUT
            SDL_Log("Removing RAWINPUT device '%s' VID 0x%.4x, PID 0x%.4x, version %d, handle 0x%.8x\n", device->name, device->vendor_id, device->product_id, device->version, device->hDevice);
#endif
            SDL_free(device->name);
            SDL_free(device);
            return;
        }
    }
}

static void
RAWINPUT_PumpMessages(void)
{
    if (SDL_RAWINPUT_need_pump) {
        MSG msg;
        while (PeekMessage(&msg, SDL_HelperWindow, WM_INPUT, WM_INPUT, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        SDL_RAWINPUT_need_pump = SDL_FALSE;
    }
}

static void
RAWINPUT_UpdateDeviceList(void)
{
    MSG msg;
    /* In theory, want only WM_INPUT_DEVICE_CHANGE messages here, but PeekMessage returns nothing unless you also ask
       for WM_INPUT */
    while (PeekMessage(&msg, SDL_HelperWindow, WM_INPUT_DEVICE_CHANGE, WM_INPUT, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

static SDL_bool
RAWINPUT_IsDeviceSupported(Uint16 vendor_id, Uint16 product_id, Uint16 version)
{
    int i;

    for (i = 0; i < SDL_arraysize(SDL_RAWINPUT_drivers); ++i) {
        SDL_HIDAPI_DeviceDriver *driver = SDL_RAWINPUT_drivers[i];
        /* Ignoring driver->enabled here, and elsewhere in this file, as the if the driver is enabled by disabling HID,
            we still want RawInput to use it.  If we end up with more than one RawInput driver, we may need to rework
            how the hints interact (separate enabled state, perhaps).
        */
        if (/*driver->enabled && */driver->IsSupportedDevice(vendor_id, product_id, version, -1)) {
            return SDL_TRUE;
        }
    }
    return SDL_FALSE;
}

SDL_bool
RAWINPUT_IsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version)
{
    SDL_RAWINPUT_Device *device;

    /* Don't update the device list for devices we know aren't supported */
    if (!RAWINPUT_IsDeviceSupported(vendor_id, product_id, version)) {
        return SDL_FALSE;
    }

    /* Make sure the device list is completely up to date when we check for device presence */
    RAWINPUT_UpdateDeviceList();

    device = SDL_RAWINPUT_devices;
    while (device) {
        if (device->vendor_id == vendor_id && device->product_id == product_id) {
            return SDL_TRUE;
        }
        device = device->next;
    }
    return SDL_FALSE;
}

static void
RAWINPUT_JoystickDetect(void)
{
    int i;
    /* Just ensure the window's add/remove messages have been pumped */
    RAWINPUT_UpdateDeviceList();

    for (i = 0; i < SDL_arraysize(SDL_RAWINPUT_drivers); ++i) {
        SDL_HIDAPI_DeviceDriver *driver = SDL_RAWINPUT_drivers[i];
        if (/*driver->enabled && */ driver->PostUpdate) {
            driver->PostUpdate();
        }
    }
    SDL_RAWINPUT_need_pump = SDL_TRUE;
}

static SDL_RAWINPUT_Device *
RAWINPUT_GetJoystickByIndex(int device_index)
{
    SDL_RAWINPUT_Device *device = SDL_RAWINPUT_devices;
    while (device) {
        if (device_index == 0) {
            break;
        }
        --device_index;
        device = device->next;
    }
    return device;
}

static const char *
RAWINPUT_JoystickGetDeviceName(int device_index)
{
    return RAWINPUT_GetJoystickByIndex(device_index)->name;
}

static int
RAWINPUT_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static SDL_JoystickGUID
RAWINPUT_JoystickGetDeviceGUID(int device_index)
{
    return RAWINPUT_GetJoystickByIndex(device_index)->guid;
}

static SDL_JoystickID
RAWINPUT_JoystickGetDeviceInstanceID(int device_index)
{
    return RAWINPUT_GetJoystickByIndex(device_index)->instance_id;
}

static int
RAWINPUT_JoystickOpen(SDL_Joystick * joystick, int device_index)
{
    SDL_RAWINPUT_Device *device = RAWINPUT_GetJoystickByIndex(device_index);
    struct joystick_hwdata *hwdata = SDL_callocStruct(struct joystick_hwdata);
    SDL_assert(!device->joystick);

    if (!hwdata) {
        return SDL_OutOfMemory();
    }

    hwdata->driver = device->driver;

    if (!device->driver->Init(joystick, NULL, device->vendor_id, device->product_id, &hwdata->context)) {
        SDL_free(hwdata);
        return -1;
    }

    hwdata->device = device;
    device->joystick = joystick;

    hwdata->mutex = SDL_CreateMutex();
    joystick->hwdata = hwdata;

    return 0;
}

static int
RAWINPUT_JoystickRumble(SDL_Joystick * joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms)
{
    struct joystick_hwdata *hwdata = joystick->hwdata;
    SDL_HIDAPI_DeviceDriver *driver = hwdata->driver;
    int result;

    SDL_LockMutex(hwdata->mutex);
    result = driver->Rumble(joystick, NULL, hwdata->context, low_frequency_rumble, high_frequency_rumble, duration_ms);
    SDL_UnlockMutex(hwdata->mutex);
    return result;
}

static void
RAWINPUT_JoystickUpdate(SDL_Joystick * joystick)
{
    struct joystick_hwdata *hwdata;
    SDL_HIDAPI_DeviceDriver *driver;
    /* Ensure data messages have been pumped */
    RAWINPUT_PumpMessages();
    hwdata = joystick->hwdata;
    driver = hwdata->driver;

    SDL_LockMutex(hwdata->mutex);
    driver->Update(joystick, NULL, hwdata->context);
    SDL_UnlockMutex(hwdata->mutex);
}

static void
RAWINPUT_JoystickClose(SDL_Joystick * joystick)
{
    struct joystick_hwdata *hwdata = joystick->hwdata;
    SDL_HIDAPI_DeviceDriver *driver = hwdata->driver;
    SDL_RAWINPUT_Device *device;
    driver->Quit(joystick, NULL, hwdata->context);

    device = hwdata->device;
    if (device) {
        SDL_assert(device->joystick == joystick);
        device->joystick = NULL;
    }

    SDL_DestroyMutex(hwdata->mutex);
    SDL_free(hwdata);
    joystick->hwdata = NULL;
}

LRESULT RAWINPUT_WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!SDL_RAWINPUT_inited)
        return -1;

    switch (msg)
    {
        case WM_INPUT_DEVICE_CHANGE:
        {
            HANDLE hDevice = (HANDLE)lParam;
            switch (wParam) {
            case GIDC_ARRIVAL:
                RAWINPUT_AddDevice(hDevice);
                break;
            case GIDC_REMOVAL: {
                SDL_RAWINPUT_Device *device;
                device = RAWINPUT_DeviceFromHandle(hDevice);
                if (device) {
                    RAWINPUT_DelDevice(device, SDL_TRUE);
                }
            } break;
            default:
                return 0;
            }
        }
        return 0;
        case WM_INPUT:
        {
            Uint8 data[sizeof(RAWINPUTHEADER) + sizeof(RAWHID) + USB_PACKET_LENGTH];
            UINT buffer_size = SDL_arraysize(data);

            if ((int)GetRawInputData((HRAWINPUT)lParam, RID_INPUT, data, &buffer_size, sizeof(RAWINPUTHEADER)) > 0) {
                PRAWINPUT raw_input = (PRAWINPUT)data;
                SDL_RAWINPUT_Device *device = RAWINPUT_DeviceFromHandle(raw_input->header.hDevice);
                if (device) {
                    SDL_HIDAPI_DeviceDriver *driver = device->driver;
                    SDL_Joystick *joystick = device->joystick;
                    if (joystick) {
                        struct joystick_hwdata *hwdata = joystick->hwdata;
                        driver->HandleStatePacketFromRAWINPUT(joystick, hwdata->context, &raw_input->data.hid.bRawData[1], raw_input->data.hid.dwSizeHid - 1);
                    }
                }
            }
        }
        return 0;
    }
    return -1;
}

static void
RAWINPUT_JoystickQuit(void)
{
    int ii;
    RAWINPUTDEVICE rid[SDL_arraysize(subscribed_devices)];
    
    if (!SDL_RAWINPUT_inited)
        return;

    for (ii = 0; ii < SDL_arraysize(subscribed_devices); ii++) {
        rid[ii].usUsagePage = USAGE_PAGE_GENERIC_DESKTOP;
        rid[ii].usUsage = subscribed_devices[ii];
        rid[ii].dwFlags = RIDEV_REMOVE;
        rid[ii].hwndTarget = NULL;
    }

    if (!RegisterRawInputDevices(rid, SDL_arraysize(rid), sizeof(RAWINPUTDEVICE))) {
        SDL_Log("Couldn't un-register RAWINPUT");
    }
    
    while (SDL_RAWINPUT_devices) {
        RAWINPUT_DelDevice(SDL_RAWINPUT_devices, SDL_FALSE);
    }

    SDL_RAWINPUT_numjoysticks = 0;

    SDL_RAWINPUT_inited = SDL_FALSE;
}


SDL_JoystickDriver SDL_RAWINPUT_JoystickDriver =
{
    RAWINPUT_JoystickInit,
    RAWINPUT_JoystickGetCount,
    RAWINPUT_JoystickDetect,
    RAWINPUT_JoystickGetDeviceName,
    RAWINPUT_JoystickGetDevicePlayerIndex,
    RAWINPUT_JoystickGetDeviceGUID,
    RAWINPUT_JoystickGetDeviceInstanceID,
    RAWINPUT_JoystickOpen,
    RAWINPUT_JoystickRumble,
    RAWINPUT_JoystickUpdate,
    RAWINPUT_JoystickClose,
    RAWINPUT_JoystickQuit,
};

#endif /* SDL_JOYSTICK_RAWINPUT */

/* vi: set ts=4 sw=4 expandtab: */
