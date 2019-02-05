
/*#define HIDAPI_USE_DDK*/

#include <windows.h>

#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <setupapi.h>
#include <winioctl.h>
#ifdef HIDAPI_USE_DDK
    #include <hidsdi.h>
#endif

#ifndef HIDAPI_USE_DDK
    /* Since we're not building with the DDK, and the HID header
       files aren't part of the SDK, we have to define all this
       stuff here. In lookup_functions(), the function pointers
       defined below are set. */
    typedef struct _HIDD_ATTRIBUTES{
        ULONG Size;
        USHORT VendorID;
        USHORT ProductID;
        USHORT VersionNumber;
    } HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

    typedef USHORT USAGE;
    typedef struct _HIDP_CAPS {
        USAGE Usage;
        USAGE UsagePage;
        USHORT InputReportByteLength;
        USHORT OutputReportByteLength;
        USHORT FeatureReportByteLength;
        USHORT Reserved[17];
        USHORT NumberLinkCollectionNodes;
        USHORT NumberInputButtonCaps;
        USHORT NumberInputValueCaps;
        USHORT NumberInputDataIndices;
        USHORT NumberOutputButtonCaps;
        USHORT NumberOutputValueCaps;
        USHORT NumberOutputDataIndices;
        USHORT NumberFeatureButtonCaps;
        USHORT NumberFeatureValueCaps;
        USHORT NumberFeatureDataIndices;
    } HIDP_CAPS, *PHIDP_CAPS;
    typedef void* PHIDP_PREPARSED_DATA;
    typedef struct _HIDP_BUTTON_CAPS
    {
        USAGE    UsagePage;
        UCHAR    ReportID;
        BOOLEAN  IsAlias;
        USHORT   BitField;
        USHORT   LinkCollection;
        USAGE    LinkUsage;
        USAGE    LinkUsagePage;
        BOOLEAN  IsRange;
        BOOLEAN  IsStringRange;
        BOOLEAN  IsDesignatorRange;
        BOOLEAN  IsAbsolute;
        ULONG    Reserved[10];
        union {
            struct {
                USAGE    UsageMin, UsageMax;
                USHORT   StringMin, StringMax;
                USHORT   DesignatorMin, DesignatorMax;
                USHORT   DataIndexMin, DataIndexMax;
            } Range;
            struct {
                USAGE    Usage, Reserved1;
                USHORT   StringIndex, Reserved2;
                USHORT   DesignatorIndex, Reserved3;
                USHORT   DataIndex, Reserved4;
            } NotRange;
        };
    } HIDP_BUTTON_CAPS, *PHIDP_BUTTON_CAPS;
    typedef struct _HIDP_VALUE_CAPS
    {
        USAGE    UsagePage;
        UCHAR    ReportID;
        BOOLEAN  IsAlias;
        USHORT   BitField;
        USHORT   LinkCollection;
        USAGE    LinkUsage;
        USAGE    LinkUsagePage;
        BOOLEAN  IsRange;
        BOOLEAN  IsStringRange;
        BOOLEAN  IsDesignatorRange;
        BOOLEAN  IsAbsolute;
        BOOLEAN  HasNull;
        UCHAR    Reserved;
        USHORT   BitSize;
        USHORT   ReportCount;
        USHORT   Reserved2[5];
        ULONG    UnitsExp;
        ULONG    Units;
        LONG     LogicalMin, LogicalMax;
        LONG     PhysicalMin, PhysicalMax;
        union {
            struct {
                USAGE    UsageMin, UsageMax;
                USHORT   StringMin, StringMax;
                USHORT   DesignatorMin, DesignatorMax;
                USHORT   DataIndexMin, DataIndexMax;
            } Range;

            struct {
                USAGE    Usage, Reserved1;
                USHORT   StringIndex, Reserved2;
                USHORT   DesignatorIndex, Reserved3;
                USHORT   DataIndex, Reserved4;
            } NotRange;
        };
    } HIDP_VALUE_CAPS, *PHIDP_VALUE_CAPS;
    typedef enum _HIDP_REPORT_TYPE
    {
        HidP_Input,
        HidP_Output,
        HidP_Feature
    } HIDP_REPORT_TYPE;
#define HIDP_STATUS_SUCCESS 0x110000

    typedef BOOLEAN (__stdcall *HidD_GetAttributes_)(HANDLE device, PHIDD_ATTRIBUTES attrib);
    typedef BOOLEAN (__stdcall *HidD_GetSerialNumberString_)(HANDLE device, PVOID buffer, ULONG buffer_len);
    typedef BOOLEAN (__stdcall *HidD_GetManufacturerString_)(HANDLE handle, PVOID buffer, ULONG buffer_len);
    typedef BOOLEAN (__stdcall *HidD_GetProductString_)(HANDLE handle, PVOID buffer, ULONG buffer_len);
    typedef BOOLEAN (__stdcall *HidD_SetFeature_)(HANDLE handle, PVOID data, ULONG length);
    typedef BOOLEAN (__stdcall *HidD_GetFeature_)(HANDLE handle, PVOID data, ULONG length);
    typedef BOOLEAN (__stdcall *HidD_GetIndexedString_)(HANDLE handle, ULONG string_index, PVOID buffer, ULONG buffer_len);
    typedef BOOLEAN (__stdcall *HidD_GetPreparsedData_)(HANDLE handle, PHIDP_PREPARSED_DATA *preparsed_data);
    typedef BOOLEAN (__stdcall *HidD_FreePreparsedData_)(PHIDP_PREPARSED_DATA preparsed_data);
    typedef NTSTATUS (__stdcall *HidP_GetCaps_)(PHIDP_PREPARSED_DATA preparsed_data, HIDP_CAPS *caps);
    typedef BOOLEAN (__stdcall *HidD_SetNumInputBuffers_)(HANDLE handle, ULONG number_buffers);
    typedef BOOLEAN (__stdcall *HidD_SetOutputReport_)(HANDLE handle, PVOID buffer, ULONG buffer_len);
    typedef NTSTATUS (__stdcall *HidP_GetButtonCaps_)(HIDP_REPORT_TYPE report_type, PHIDP_BUTTON_CAPS button_caps, PUSHORT button_caps_length, PHIDP_PREPARSED_DATA preparsed_data);
    typedef NTSTATUS (__stdcall *HidP_GetValueCaps_)(HIDP_REPORT_TYPE report_type, PHIDP_VALUE_CAPS value_caps, PUSHORT value_caps_length, PHIDP_PREPARSED_DATA preparsed_data);

    extern HidD_GetAttributes_ HidD_GetAttributes;
    extern HidD_GetSerialNumberString_ HidD_GetSerialNumberString;
    extern HidD_GetManufacturerString_ HidD_GetManufacturerString;
    extern HidD_GetProductString_ HidD_GetProductString;
    extern HidD_SetFeature_ HidD_SetFeature;
    extern HidD_GetFeature_ HidD_GetFeature;
    extern HidD_GetIndexedString_ HidD_GetIndexedString;
    extern HidD_GetPreparsedData_ HidD_GetPreparsedData;
    extern HidD_FreePreparsedData_ HidD_FreePreparsedData;
    extern HidP_GetCaps_ HidP_GetCaps;
    extern HidD_SetNumInputBuffers_ HidD_SetNumInputBuffers;
    extern HidD_SetOutputReport_ HidD_SetOutputReport;
    extern HidP_GetButtonCaps_ HidP_GetButtonCaps;
    extern HidP_GetValueCaps_ HidP_GetValueCaps;
#endif /* HIDAPI_USE_DDK */

#ifdef __cplusplus
} /* extern "C" */
#endif
