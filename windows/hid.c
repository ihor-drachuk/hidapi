/*******************************************************
 HIDAPI - Multi-Platform library for
 communication with HID devices.

 Alan Ott
 Signal 11 Software

 libusb/hidapi Team

 Copyright 2022, All Rights Reserved.

 At the discretion of the user of this library,
 this software may be licensed under the terms of the
 GNU General Public License v3, a BSD-Style license, or the
 original HIDAPI license as outlined in the LICENSE.txt,
 LICENSE-gpl3.txt, LICENSE-bsd.txt, and LICENSE-orig.txt
 files located at the root of the source distribution.
 These files may also be found in the public source
 code repository located at:
        https://github.com/libusb/hidapi .
********************************************************/

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
// Do not warn about wcsncpy usage.
// https://docs.microsoft.com/cpp/c-runtime-library/security-features-in-the-crt
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "hidapi.h"

#include <windows.h>

#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

#ifdef __MINGW32__
#include <ntdef.h>
#include <winbase.h>
#define WC_ERR_INVALID_CHARS 0x00000080
#endif

#ifdef __CYGWIN__
#include <ntdef.h>
#include <wctype.h>
#define _wcsdup wcsdup
#endif

/* MAXIMUM_USB_STRING_LENGTH from usbspec.h is 255 */
/* BLUETOOTH_DEVICE_NAME_SIZE from bluetoothapis.h is 256 */
#define MAX_STRING_WCHARS 256

/*#define HIDAPI_USE_DDK*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <devpropdef.h>

#undef MIN
#define MIN(x,y) ((x) < (y)? (x): (y))

static struct hid_api_version api_version = {
	.major = HID_API_VERSION_MAJOR,
	.minor = HID_API_VERSION_MINOR,
	.patch = HID_API_VERSION_PATCH
};

#ifdef HIDAPI_USE_DDK
	#include <cfgmgr32.h>
	#include <hidsdi.h>
	#include <hidclass.h>
#else /* !HIDAPI_USE_DDK */
	#define HID_OUT_CTL_CODE(id) CTL_CODE(FILE_DEVICE_KEYBOARD, (id), METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
	#define IOCTL_HID_GET_FEATURE HID_OUT_CTL_CODE(100)
	#define IOCTL_HID_GET_INPUT_REPORT HID_OUT_CTL_CODE(104)

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
		USHORT fields_not_used_by_hidapi[10];
	} HIDP_CAPS, *PHIDP_CAPS;
	typedef void* PHIDP_PREPARSED_DATA;
	#define HIDP_STATUS_SUCCESS 0x110000

	typedef void (__stdcall *HidD_GetHidGuid_)(LPGUID hid_guid);
	typedef BOOLEAN (__stdcall *HidD_GetAttributes_)(HANDLE device, PHIDD_ATTRIBUTES attrib);
	typedef BOOLEAN (__stdcall *HidD_GetSerialNumberString_)(HANDLE device, PVOID buffer, ULONG buffer_len);
	typedef BOOLEAN (__stdcall *HidD_GetManufacturerString_)(HANDLE handle, PVOID buffer, ULONG buffer_len);
	typedef BOOLEAN (__stdcall *HidD_GetProductString_)(HANDLE handle, PVOID buffer, ULONG buffer_len);
	typedef BOOLEAN (__stdcall *HidD_SetFeature_)(HANDLE handle, PVOID data, ULONG length);
	typedef BOOLEAN (__stdcall *HidD_GetFeature_)(HANDLE handle, PVOID data, ULONG length);
	typedef BOOLEAN (__stdcall *HidD_GetInputReport_)(HANDLE handle, PVOID data, ULONG length);
	typedef BOOLEAN (__stdcall *HidD_GetIndexedString_)(HANDLE handle, ULONG string_index, PVOID buffer, ULONG buffer_len);
	typedef BOOLEAN (__stdcall *HidD_GetPreparsedData_)(HANDLE handle, PHIDP_PREPARSED_DATA *preparsed_data);
	typedef BOOLEAN (__stdcall *HidD_FreePreparsedData_)(PHIDP_PREPARSED_DATA preparsed_data);
	typedef NTSTATUS (__stdcall *HidP_GetCaps_)(PHIDP_PREPARSED_DATA preparsed_data, HIDP_CAPS *caps);
	typedef BOOLEAN (__stdcall *HidD_SetNumInputBuffers_)(HANDLE handle, ULONG number_buffers);

	static HidD_GetHidGuid_ HidD_GetHidGuid;
	static HidD_GetAttributes_ HidD_GetAttributes;
	static HidD_GetSerialNumberString_ HidD_GetSerialNumberString;
	static HidD_GetManufacturerString_ HidD_GetManufacturerString;
	static HidD_GetProductString_ HidD_GetProductString;
	static HidD_SetFeature_ HidD_SetFeature;
	static HidD_GetFeature_ HidD_GetFeature;
	static HidD_GetInputReport_ HidD_GetInputReport;
	static HidD_GetIndexedString_ HidD_GetIndexedString;
	static HidD_GetPreparsedData_ HidD_GetPreparsedData;
	static HidD_FreePreparsedData_ HidD_FreePreparsedData;
	static HidP_GetCaps_ HidP_GetCaps;
	static HidD_SetNumInputBuffers_ HidD_SetNumInputBuffers;

	static HMODULE lib_handle = NULL;
	static BOOLEAN initialized = FALSE;

	typedef DWORD RETURN_TYPE;
	typedef RETURN_TYPE CONFIGRET;
	typedef DWORD DEVNODE, DEVINST;
	typedef DEVNODE* PDEVNODE, * PDEVINST;
	typedef WCHAR* DEVNODEID_W, * DEVINSTID_W;

#define CR_SUCCESS (0x00000000)
#define CR_BUFFER_SMALL (0x0000001A)

#define CM_LOCATE_DEVNODE_NORMAL 0x00000000

#define CM_GET_DEVICE_INTERFACE_LIST_PRESENT (0x00000000)

#define DEVPROP_TYPEMOD_LIST 0x00002000

#define DEVPROP_TYPE_STRING 0x00000012
#define DEVPROP_TYPE_STRING_LIST (DEVPROP_TYPE_STRING|DEVPROP_TYPEMOD_LIST)

	typedef CONFIGRET(__stdcall* CM_Locate_DevNodeW_)(PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags);
	typedef CONFIGRET(__stdcall* CM_Get_Parent_)(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
	typedef CONFIGRET(__stdcall* CM_Get_DevNode_PropertyW_)(DEVINST dnDevInst, CONST DEVPROPKEY* PropertyKey, DEVPROPTYPE* PropertyType, PBYTE PropertyBuffer, PULONG PropertyBufferSize, ULONG ulFlags);
	typedef CONFIGRET(__stdcall* CM_Get_Device_Interface_PropertyW_)(LPCWSTR pszDeviceInterface, CONST DEVPROPKEY* PropertyKey, DEVPROPTYPE* PropertyType, PBYTE PropertyBuffer, PULONG PropertyBufferSize, ULONG ulFlags);
	typedef CONFIGRET(__stdcall* CM_Get_Device_Interface_List_SizeW_)(PULONG pulLen, LPGUID InterfaceClassGuid, DEVINSTID_W pDeviceID, ULONG ulFlags);
	typedef CONFIGRET(__stdcall* CM_Get_Device_Interface_ListW_)(LPGUID InterfaceClassGuid, DEVINSTID_W pDeviceID, PZZWSTR Buffer, ULONG BufferLen, ULONG ulFlags);

	static CM_Locate_DevNodeW_ CM_Locate_DevNodeW = NULL;
	static CM_Get_Parent_ CM_Get_Parent = NULL;
	static CM_Get_DevNode_PropertyW_ CM_Get_DevNode_PropertyW = NULL;
	static CM_Get_Device_Interface_PropertyW_ CM_Get_Device_Interface_PropertyW = NULL;
	static CM_Get_Device_Interface_List_SizeW_ CM_Get_Device_Interface_List_SizeW = NULL;
	static CM_Get_Device_Interface_ListW_ CM_Get_Device_Interface_ListW = NULL;

	static HMODULE cfgmgr32_lib_handle = NULL;
#endif /* HIDAPI_USE_DDK */

struct hid_device_ {
		HANDLE device_handle;
		BOOL blocking;
		USHORT output_report_length;
		unsigned char *write_buf;
		size_t input_report_length;
		USHORT feature_report_length;
		unsigned char *feature_buf;
		void *last_error_str;
		DWORD last_error_num;
		BOOL read_pending;
		char *read_buf;
		OVERLAPPED ol;
		OVERLAPPED write_ol;
		struct hid_device_info* device_info;
};

static hid_device *new_hid_device()
{
	hid_device *dev = (hid_device*) calloc(1, sizeof(hid_device));
	dev->device_handle = INVALID_HANDLE_VALUE;
	dev->blocking = TRUE;
	dev->output_report_length = 0;
	dev->write_buf = NULL;
	dev->input_report_length = 0;
	dev->feature_report_length = 0;
	dev->feature_buf = NULL;
	dev->last_error_str = NULL;
	dev->last_error_num = 0;
	dev->read_pending = FALSE;
	dev->read_buf = NULL;
	memset(&dev->ol, 0, sizeof(dev->ol));
	dev->ol.hEvent = CreateEvent(NULL, FALSE, FALSE /*initial state f=nonsignaled*/, NULL);
	memset(&dev->write_ol, 0, sizeof(dev->write_ol));
	dev->write_ol.hEvent = CreateEvent(NULL, FALSE, FALSE /*inital state f=nonsignaled*/, NULL);
	dev->device_info = NULL;

	return dev;
}

static void free_hid_device(hid_device *dev)
{
	CloseHandle(dev->ol.hEvent);
	CloseHandle(dev->write_ol.hEvent);
	CloseHandle(dev->device_handle);
	LocalFree(dev->last_error_str);
	free(dev->write_buf);
	free(dev->feature_buf);
	free(dev->read_buf);
	hid_free_enumeration(dev->device_info);
	free(dev);
}

static void register_error(hid_device *dev, const char *op)
{
	WCHAR *ptr, *msg;
	(void)op; // unreferenced  param
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&msg, 0/*sz*/,
		NULL);

	/* Get rid of the CR and LF that FormatMessage() sticks at the
	   end of the message. Thanks Microsoft! */
	ptr = msg;
	while (*ptr) {
		if (*ptr == L'\r') {
			*ptr = L'\0';
			break;
		}
		ptr++;
	}

	/* Store the message off in the Device entry so that
	   the hid_error() function can pick it up. */
	LocalFree(dev->last_error_str);
	dev->last_error_str = msg;
}

#ifndef HIDAPI_USE_DDK
static int lookup_functions()
{
	lib_handle = LoadLibraryA("hid.dll");
	if (lib_handle) {
#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#define RESOLVE(x) x = (x##_)GetProcAddress(lib_handle, #x); if (!x) return -1;
		RESOLVE(HidD_GetHidGuid);
		RESOLVE(HidD_GetAttributes);
		RESOLVE(HidD_GetSerialNumberString);
		RESOLVE(HidD_GetManufacturerString);
		RESOLVE(HidD_GetProductString);
		RESOLVE(HidD_SetFeature);
		RESOLVE(HidD_GetFeature);
		RESOLVE(HidD_GetInputReport);
		RESOLVE(HidD_GetIndexedString);
		RESOLVE(HidD_GetPreparsedData);
		RESOLVE(HidD_FreePreparsedData);
		RESOLVE(HidP_GetCaps);
		RESOLVE(HidD_SetNumInputBuffers);
#undef RESOLVE
#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif
	}
	else
		return -1;

	cfgmgr32_lib_handle = LoadLibraryA("cfgmgr32.dll");
	if (cfgmgr32_lib_handle) {
#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#define RESOLVE(x) x = (x##_)GetProcAddress(cfgmgr32_lib_handle, #x);
		RESOLVE(CM_Locate_DevNodeW);
		RESOLVE(CM_Get_Parent);
		RESOLVE(CM_Get_DevNode_PropertyW);
		RESOLVE(CM_Get_Device_Interface_PropertyW);
		RESOLVE(CM_Get_Device_Interface_List_SizeW);
		RESOLVE(CM_Get_Device_Interface_ListW);
#undef RESOLVE
#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif
	}
	else {
		CM_Locate_DevNodeW = NULL;
		CM_Get_Parent = NULL;
		CM_Get_DevNode_PropertyW = NULL;
		CM_Get_Device_Interface_PropertyW = NULL;
		CM_Get_Device_Interface_List_SizeW = NULL;
		CM_Get_Device_Interface_ListW = NULL;
	}

	return 0;
}
#endif

static HANDLE open_device(const wchar_t *path, BOOL open_rw)
{
	HANDLE handle;
	DWORD desired_access = (open_rw)? (GENERIC_WRITE | GENERIC_READ): 0;
	DWORD share_mode = FILE_SHARE_READ|FILE_SHARE_WRITE;

	handle = CreateFileW(path,
		desired_access,
		share_mode,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,/*FILE_ATTRIBUTE_NORMAL,*/
		0);

	return handle;
}

HID_API_EXPORT const struct hid_api_version* HID_API_CALL hid_version()
{
	return &api_version;
}

HID_API_EXPORT const char* HID_API_CALL hid_version_str()
{
	return HID_API_VERSION_STR;
}

int HID_API_EXPORT hid_init(void)
{
#ifndef HIDAPI_USE_DDK
	if (!initialized) {
		if (lookup_functions() < 0) {
			hid_exit();
			return -1;
		}
		initialized = TRUE;
	}
#endif
	return 0;
}

int HID_API_EXPORT hid_exit(void)
{
#ifndef HIDAPI_USE_DDK
	if (lib_handle)
		FreeLibrary(lib_handle);
	lib_handle = NULL;
	if (cfgmgr32_lib_handle)
		FreeLibrary(cfgmgr32_lib_handle);
	cfgmgr32_lib_handle = NULL;
	initialized = FALSE;
#endif
	return 0;
}

static void hid_internal_get_ble_info(struct hid_device_info* dev, DEVINST dev_node)
{
	ULONG len;
	CONFIGRET cr;
	DEVPROPTYPE property_type;

	static DEVPROPKEY DEVPKEY_NAME = { { 0xb725f130, 0x47ef, 0x101a, 0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac }, 10 }; // DEVPROP_TYPE_STRING
	static DEVPROPKEY PKEY_DeviceInterface_Bluetooth_DeviceAddress = { { 0x2bd67d8b, 0x8beb, 0x48d5, 0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a }, 1 }; // DEVPROP_TYPE_STRING
	static DEVPROPKEY PKEY_DeviceInterface_Bluetooth_Manufacturer = { { 0x2bd67d8b, 0x8beb, 0x48d5, 0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a }, 4 }; // DEVPROP_TYPE_STRING

	/* Manufacturer String */
	len = 0;
	cr = CM_Get_DevNode_PropertyW(dev_node, &PKEY_DeviceInterface_Bluetooth_Manufacturer, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING) {
		free(dev->manufacturer_string);
		dev->manufacturer_string = (wchar_t*)calloc(len, sizeof(BYTE));
		CM_Get_DevNode_PropertyW(dev_node, &PKEY_DeviceInterface_Bluetooth_Manufacturer, &property_type, (PBYTE)dev->manufacturer_string, &len, 0);
	}

	/* Serial Number String (MAC Address) */
	len = 0;
	cr = CM_Get_DevNode_PropertyW(dev_node, &PKEY_DeviceInterface_Bluetooth_DeviceAddress, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING) {
		free(dev->serial_number);
		dev->serial_number = (wchar_t*)calloc(len, sizeof(BYTE));
		CM_Get_DevNode_PropertyW(dev_node, &PKEY_DeviceInterface_Bluetooth_DeviceAddress, &property_type, (PBYTE)dev->serial_number, &len, 0);
	}

	/* Get devnode grandparent to reach out Bluetooth LE device node */
	cr = CM_Get_Parent(&dev_node, dev_node, 0);
	if (cr != CR_SUCCESS)
		return;

	/* Product String */
	len = 0;
	cr = CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_NAME, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING) {
		free(dev->product_string);
		dev->product_string = (wchar_t*)calloc(len, sizeof(BYTE));
		CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_NAME, &property_type, (PBYTE)dev->product_string, &len, 0);
	}
}

/* USB Device Interface Number.
   It can be parsed out of the Hardware ID if a USB device is has multiple interfaces (composite device).
   See https://docs.microsoft.com/windows-hardware/drivers/hid/hidclass-hardware-ids-for-top-level-collections
   and https://docs.microsoft.com/windows-hardware/drivers/install/standard-usb-identifiers

   hardware_id is always expected to be uppercase.
*/
static int hid_internal_get_interface_number(const wchar_t* hardware_id)
{
	int interface_number;
	wchar_t *startptr, *endptr;
	const wchar_t *interface_token = L"&MI_";

	startptr = wcsstr(hardware_id, interface_token);
	if (!startptr)
		return -1;

	startptr += wcslen(interface_token);
	interface_number = wcstol(startptr, &endptr, 16);
	if (endptr == startptr)
		return -1;

	return interface_number;
}

static void hid_internal_get_info(const wchar_t* interface_path, struct hid_device_info* dev)
{
	wchar_t *device_id = NULL, *compatible_ids = NULL, *hardware_ids = NULL;
	ULONG len;
	CONFIGRET cr;
	DEVPROPTYPE property_type;
	DEVINST dev_node;

	static DEVPROPKEY DEVPKEY_Device_InstanceId = { { 0x78c34fc8, 0x104a, 0x4aca, 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 }, 256 }; // DEVPROP_TYPE_STRING
	static DEVPROPKEY DEVPKEY_Device_HardwareIds = { { 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}, 3 }; // DEVPROP_TYPE_STRING_LIST
	static DEVPROPKEY DEVPKEY_Device_CompatibleIds = { { 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}, 4 }; // DEVPROP_TYPE_STRING_LIST

#ifndef HIDAPI_USE_DDK
	if (!CM_Get_Device_Interface_PropertyW ||
		!CM_Locate_DevNodeW ||
		!CM_Get_Parent ||
		!CM_Get_DevNode_PropertyW)
		goto end;
#endif

	/* Get the device id from interface path */
	len = 0;
	cr = CM_Get_Device_Interface_PropertyW(interface_path, &DEVPKEY_Device_InstanceId, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING) {
		device_id = (wchar_t*)calloc(len, sizeof(BYTE));
		cr = CM_Get_Device_Interface_PropertyW(interface_path, &DEVPKEY_Device_InstanceId, &property_type, (PBYTE)device_id, &len, 0);
	}
	if (cr != CR_SUCCESS)
		goto end;

	/* Open devnode from device id */
	cr = CM_Locate_DevNodeW(&dev_node, (DEVINSTID_W)device_id, CM_LOCATE_DEVNODE_NORMAL);
	if (cr != CR_SUCCESS)
		goto end;

	/* Get the hardware ids from devnode */
	len = 0;
	cr = CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_Device_HardwareIds, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING_LIST) {
		hardware_ids = (wchar_t*)calloc(len, sizeof(BYTE));
		cr = CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_Device_HardwareIds, &property_type, (PBYTE)hardware_ids, &len, 0);
	}
	if (cr != CR_SUCCESS)
		goto end;

	// Search for interface number in hardware ids
	for (wchar_t* hardware_id = hardware_ids; *hardware_id; hardware_id += wcslen(hardware_id) + 1) {
		/* Normalize to upper case */
		for (wchar_t* p = hardware_id; *p; ++p) *p = towupper(*p);

		dev->interface_number = hid_internal_get_interface_number(hardware_id);

		if (dev->interface_number != -1)
			break;
	}

	/* Get devnode parent */
	cr = CM_Get_Parent(&dev_node, dev_node, 0);
	if (cr != CR_SUCCESS)
		goto end;

	/* Get the compatible ids from parent devnode */
	len = 0;
	cr = CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_Device_CompatibleIds, &property_type, NULL, &len, 0);
	if (cr == CR_BUFFER_SMALL && property_type == DEVPROP_TYPE_STRING_LIST) {
		compatible_ids = (wchar_t*)calloc(len, sizeof(BYTE));
		cr = CM_Get_DevNode_PropertyW(dev_node, &DEVPKEY_Device_CompatibleIds, &property_type, (PBYTE)compatible_ids, &len, 0);
	}
	if (cr != CR_SUCCESS)
		goto end;

	/* Now we can parse parent's compatible IDs to find out the device bus type */
	for (wchar_t* compatible_id = compatible_ids; *compatible_id; compatible_id += wcslen(compatible_id) + 1) {
		/* Normalize to upper case */
		for (wchar_t* p = compatible_id; *p; ++p) *p = towupper(*p);

		/* Bluetooth LE devices */
		if (wcsstr(compatible_id, L"BTHLEDEVICE") != NULL) {
			/* HidD_GetProductString/HidD_GetManufacturerString/HidD_GetSerialNumberString is not working for BLE HID devices
			   Request this info via dev node properties instead.
			   https://docs.microsoft.com/answers/questions/401236/hidd-getproductstring-with-ble-hid-device.html */
			hid_internal_get_ble_info(dev, dev_node);
			break;
		}
	}
end:
	free(device_id);
	free(hardware_ids);
	free(compatible_ids);
}

static char *hid_internal_UTF16toUTF8(const wchar_t *src)
{
	char *dst = NULL;
	int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src, -1, NULL, 0, NULL, NULL);
	if (len) {
		dst = (char*)calloc(len, sizeof(char));
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src, -1, dst, len, NULL, NULL);
	}

	return dst;
}

static wchar_t *hid_internal_UTF8toUTF16(const char *src)
{
	wchar_t *dst = NULL;
	int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
	if (len) {
		dst = (wchar_t*)calloc(len, sizeof(wchar_t));
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, len);
	}

	return dst;
}

static struct hid_device_info *hid_internal_get_device_info(const wchar_t *path, HANDLE handle)
{
	struct hid_device_info *dev = NULL; /* return object */
	HIDD_ATTRIBUTES attrib;
	PHIDP_PREPARSED_DATA pp_data = NULL;
	HIDP_CAPS caps;
	wchar_t string[MAX_STRING_WCHARS];

	/* Create the record. */
	dev = (struct hid_device_info*)calloc(1, sizeof(struct hid_device_info));

	/* Fill out the record */
	dev->next = NULL;
	dev->path = hid_internal_UTF16toUTF8(path);

	attrib.Size = sizeof(HIDD_ATTRIBUTES);
	if (HidD_GetAttributes(handle, &attrib)) {
		/* VID/PID */
		dev->vendor_id = attrib.VendorID;
		dev->product_id = attrib.ProductID;

		/* Release Number */
		dev->release_number = attrib.VersionNumber;
	}

	/* Get the Usage Page and Usage for this device. */
	if (HidD_GetPreparsedData(handle, &pp_data)) {
		if (HidP_GetCaps(pp_data, &caps) == HIDP_STATUS_SUCCESS) {
			dev->usage_page = caps.UsagePage;
			dev->usage = caps.Usage;
		}

		HidD_FreePreparsedData(pp_data);
	}

	/* Serial Number */
	string[0] = L'\0';
	HidD_GetSerialNumberString(handle, string, sizeof(string));
	string[MAX_STRING_WCHARS - 1] = L'\0';
	dev->serial_number = _wcsdup(string);

	/* Manufacturer String */
	string[0] = L'\0';
	HidD_GetManufacturerString(handle, string, sizeof(string));
	string[MAX_STRING_WCHARS - 1] = L'\0';
	dev->manufacturer_string = _wcsdup(string);

	/* Product String */
	string[0] = L'\0';
	HidD_GetProductString(handle, string, sizeof(string));
	string[MAX_STRING_WCHARS - 1] = L'\0';
	dev->product_string = _wcsdup(string);

	hid_internal_get_info(path, dev);

	return dev;
}

struct hid_device_info HID_API_EXPORT * HID_API_CALL hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
	struct hid_device_info *root = NULL; /* return object */
	struct hid_device_info *cur_dev = NULL;
	GUID interface_class_guid;
	CONFIGRET cr;
	wchar_t* device_interface_list = NULL;
	DWORD len;

	if (hid_init() < 0)
		return NULL;

#ifndef HIDAPI_USE_DDK
	if (!CM_Get_Device_Interface_List_SizeW ||
		!CM_Get_Device_Interface_ListW)
		return NULL;
#endif

	/* Retrieve HID Interface Class GUID
	   https://docs.microsoft.com/windows-hardware/drivers/install/guid-devinterface-hid */
	HidD_GetHidGuid(&interface_class_guid);

	/* Get the list of all device interfaces belonging to the HID class. */
	/* Retry in case of list was changed between calls to
	  CM_Get_Device_Interface_List_SizeW and CM_Get_Device_Interface_ListW */
	do {
		cr = CM_Get_Device_Interface_List_SizeW(&len, &interface_class_guid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
		if (cr != CR_SUCCESS) {
			break;
		}

		if (device_interface_list != NULL) {
			free(device_interface_list);
		}

		device_interface_list = (wchar_t*)calloc(len, sizeof(wchar_t));
		if (device_interface_list == NULL) {
			return NULL;
		}
		cr = CM_Get_Device_Interface_ListW(&interface_class_guid, NULL, device_interface_list, len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	} while (cr == CR_BUFFER_SMALL);

	if (cr != CR_SUCCESS) {
		goto end_of_function;
	}

	/* Iterate over each device interface in the HID class, looking for the right one. */
	for (wchar_t* device_interface = device_interface_list; *device_interface; device_interface += wcslen(device_interface) + 1) {
		HANDLE device_handle = INVALID_HANDLE_VALUE;
		HIDD_ATTRIBUTES attrib;

		/* Open read-only handle to the device */
		device_handle = open_device(device_interface, FALSE);

		/* Check validity of device_handle. */
		if (device_handle == INVALID_HANDLE_VALUE) {
			/* Unable to open the device. */
			continue;
		}

		/* Get the Vendor ID and Product ID for this device. */
		attrib.Size = sizeof(HIDD_ATTRIBUTES);
		HidD_GetAttributes(device_handle, &attrib);

		/* Check the VID/PID to see if we should add this
		   device to the enumeration list. */
		if ((vendor_id == 0x0 || attrib.VendorID == vendor_id) &&
		    (product_id == 0x0 || attrib.ProductID == product_id)) {

			/* VID/PID match. Create the record. */
			struct hid_device_info *tmp = hid_internal_get_device_info(device_interface, device_handle);

			if (tmp == NULL) {
				goto cont_close;
			}

			if (cur_dev) {
				cur_dev->next = tmp;
			}
			else {
				root = tmp;
			}
			cur_dev = tmp;
		}

cont_close:
		CloseHandle(device_handle);
	}

end_of_function:
	free(device_interface_list);

	return root;
}

void  HID_API_EXPORT HID_API_CALL hid_free_enumeration(struct hid_device_info *devs)
{
	/* TODO: Merge this with the Linux version. This function is platform-independent. */
	struct hid_device_info *d = devs;
	while (d) {
		struct hid_device_info *next = d->next;
		free(d->path);
		free(d->serial_number);
		free(d->manufacturer_string);
		free(d->product_string);
		free(d);
		d = next;
	}
}


HID_API_EXPORT hid_device * HID_API_CALL hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
	/* TODO: Merge this functions with the Linux version. This function should be platform independent. */
	struct hid_device_info *devs, *cur_dev;
	const char *path_to_open = NULL;
	hid_device *handle = NULL;

	devs = hid_enumerate(vendor_id, product_id);
	cur_dev = devs;
	while (cur_dev) {
		if (cur_dev->vendor_id == vendor_id &&
		    cur_dev->product_id == product_id) {
			if (serial_number) {
				if (cur_dev->serial_number && wcscmp(serial_number, cur_dev->serial_number) == 0) {
					path_to_open = cur_dev->path;
					break;
				}
			}
			else {
				path_to_open = cur_dev->path;
				break;
			}
		}
		cur_dev = cur_dev->next;
	}

	if (path_to_open) {
		/* Open the device */
		handle = hid_open_path(path_to_open);
	}

	hid_free_enumeration(devs);

	return handle;
}

HID_API_EXPORT hid_device * HID_API_CALL hid_open_path(const char *path)
{
	hid_device *dev = NULL;
	wchar_t* interface_path = NULL;
	HANDLE device_handle = INVALID_HANDLE_VALUE;
	PHIDP_PREPARSED_DATA pp_data = NULL;
	HIDP_CAPS caps;

	if (hid_init() < 0)
		goto end_of_function;

	interface_path = hid_internal_UTF8toUTF16(path);
	if (!interface_path)
		goto end_of_function;

	/* Open a handle to the device */
	device_handle = open_device(interface_path, TRUE);

	/* Check validity of write_handle. */
	if (device_handle == INVALID_HANDLE_VALUE) {
		/* System devices, such as keyboards and mice, cannot be opened in
		   read-write mode, because the system takes exclusive control over
		   them.  This is to prevent keyloggers.  However, feature reports
		   can still be sent and received.  Retry opening the device, but
		   without read/write access. */
		device_handle = open_device(interface_path, FALSE);

		/* Check the validity of the limited device_handle. */
		if (device_handle == INVALID_HANDLE_VALUE)
			goto end_of_function;
	}

	/* Set the Input Report buffer size to 64 reports. */
	if (!HidD_SetNumInputBuffers(device_handle, 64))
		goto end_of_function;

	/* Get the Input Report length for the device. */
	if (!HidD_GetPreparsedData(device_handle, &pp_data))
		goto end_of_function;

	if (HidP_GetCaps(pp_data, &caps) != HIDP_STATUS_SUCCESS)
		goto end_of_function;

	dev = new_hid_device();

	dev->device_handle = device_handle;
	device_handle = INVALID_HANDLE_VALUE;

	dev->output_report_length = caps.OutputReportByteLength;
	dev->input_report_length = caps.InputReportByteLength;
	dev->feature_report_length = caps.FeatureReportByteLength;
	dev->read_buf = (char*) malloc(dev->input_report_length);
	dev->device_info = hid_internal_get_device_info(interface_path, dev->device_handle);

end_of_function:
	free(interface_path);
	CloseHandle(device_handle);

	if (pp_data) {
		HidD_FreePreparsedData(pp_data);
	}

	return dev;
}

int HID_API_EXPORT HID_API_CALL hid_write(hid_device *dev, const unsigned char *data, size_t length)
{
	DWORD bytes_written = 0;
	int function_result = -1;
	BOOL res;
	BOOL overlapped = FALSE;

	unsigned char *buf;

	if (!data || (length==0)) {
		register_error(dev, "Zero length buffer");
		return function_result;
	}

	/* Make sure the right number of bytes are passed to WriteFile. Windows
	   expects the number of bytes which are in the _longest_ report (plus
	   one for the report number) bytes even if the data is a report
	   which is shorter than that. Windows gives us this value in
	   caps.OutputReportByteLength. If a user passes in fewer bytes than this,
	   use cached temporary buffer which is the proper size. */
	if (length >= dev->output_report_length) {
		/* The user passed the right number of bytes. Use the buffer as-is. */
		buf = (unsigned char *) data;
	} else {
		if (dev->write_buf == NULL)
			dev->write_buf = (unsigned char *) malloc(dev->output_report_length);
		buf = dev->write_buf;
		memcpy(buf, data, length);
		memset(buf + length, 0, dev->output_report_length - length);
		length = dev->output_report_length;
	}

	res = WriteFile(dev->device_handle, buf, (DWORD) length, NULL, &dev->write_ol);

	if (!res) {
		if (GetLastError() != ERROR_IO_PENDING) {
			/* WriteFile() failed. Return error. */
			register_error(dev, "WriteFile");
			goto end_of_function;
		}
		overlapped = TRUE;
	}

	if (overlapped) {
		/* Wait for the transaction to complete. This makes
		   hid_write() synchronous. */
		res = WaitForSingleObject(dev->write_ol.hEvent, 1000);
		if (res != WAIT_OBJECT_0) {
			/* There was a Timeout. */
			register_error(dev, "WriteFile/WaitForSingleObject Timeout");
			goto end_of_function;
		}

		/* Get the result. */
		res = GetOverlappedResult(dev->device_handle, &dev->write_ol, &bytes_written, FALSE/*wait*/);
		if (res) {
			function_result = bytes_written;
		}
		else {
			/* The Write operation failed. */
			register_error(dev, "WriteFile");
			goto end_of_function;
		}
	}

end_of_function:
	return function_result;
}


int HID_API_EXPORT HID_API_CALL hid_read_timeout(hid_device *dev, unsigned char *data, size_t length, int milliseconds)
{
	DWORD bytes_read = 0;
	size_t copy_len = 0;
	BOOL res = FALSE;
	BOOL overlapped = FALSE;

	/* Copy the handle for convenience. */
	HANDLE ev = dev->ol.hEvent;

	if (!dev->read_pending) {
		/* Start an Overlapped I/O read. */
		dev->read_pending = TRUE;
		memset(dev->read_buf, 0, dev->input_report_length);
		ResetEvent(ev);
		res = ReadFile(dev->device_handle, dev->read_buf, (DWORD) dev->input_report_length, &bytes_read, &dev->ol);

		if (!res) {
			if (GetLastError() != ERROR_IO_PENDING) {
				/* ReadFile() has failed.
				   Clean up and return error. */
				CancelIo(dev->device_handle);
				dev->read_pending = FALSE;
				goto end_of_function;
			}
			overlapped = TRUE;
		}
	}
	else {
		overlapped = TRUE;
	}

	if (overlapped) {
		if (milliseconds >= 0) {
			/* See if there is any data yet. */
			res = WaitForSingleObject(ev, milliseconds);
			if (res != WAIT_OBJECT_0) {
				/* There was no data this time. Return zero bytes available,
				   but leave the Overlapped I/O running. */
				return 0;
			}
		}

		/* Either WaitForSingleObject() told us that ReadFile has completed, or
		   we are in non-blocking mode. Get the number of bytes read. The actual
		   data has been copied to the data[] array which was passed to ReadFile(). */
		res = GetOverlappedResult(dev->device_handle, &dev->ol, &bytes_read, TRUE/*wait*/);
	}
	/* Set pending back to false, even if GetOverlappedResult() returned error. */
	dev->read_pending = FALSE;

	if (res && bytes_read > 0) {
		if (dev->read_buf[0] == 0x0) {
			/* If report numbers aren't being used, but Windows sticks a report
			   number (0x0) on the beginning of the report anyway. To make this
			   work like the other platforms, and to make it work more like the
			   HID spec, we'll skip over this byte. */
			bytes_read--;
			copy_len = length > bytes_read ? bytes_read : length;
			memcpy(data, dev->read_buf+1, copy_len);
		}
		else {
			/* Copy the whole buffer, report number and all. */
			copy_len = length > bytes_read ? bytes_read : length;
			memcpy(data, dev->read_buf, copy_len);
		}
	}

end_of_function:
	if (!res) {
		register_error(dev, "GetOverlappedResult");
		return -1;
	}

	return (int) copy_len;
}

int HID_API_EXPORT HID_API_CALL hid_read(hid_device *dev, unsigned char *data, size_t length)
{
	return hid_read_timeout(dev, data, length, (dev->blocking)? -1: 0);
}

int HID_API_EXPORT HID_API_CALL hid_set_nonblocking(hid_device *dev, int nonblock)
{
	dev->blocking = !nonblock;
	return 0; /* Success */
}

int HID_API_EXPORT HID_API_CALL hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length)
{
	BOOL res = FALSE;
	unsigned char *buf;
	size_t length_to_send;

	/* Windows expects at least caps.FeatureReportByteLength bytes passed
	   to HidD_SetFeature(), even if the report is shorter. Any less sent and
	   the function fails with error ERROR_INVALID_PARAMETER set. Any more
	   and HidD_SetFeature() silently truncates the data sent in the report
	   to caps.FeatureReportByteLength. */
	if (length >= dev->feature_report_length) {
		buf = (unsigned char *) data;
		length_to_send = length;
	} else {
		if (dev->feature_buf == NULL)
			dev->feature_buf = (unsigned char *) malloc(dev->feature_report_length);
		buf = dev->feature_buf;
		memcpy(buf, data, length);
		memset(buf + length, 0, dev->feature_report_length - length);
		length_to_send = dev->feature_report_length;
	}

	res = HidD_SetFeature(dev->device_handle, (PVOID)buf, (DWORD) length_to_send);

	if (!res) {
		register_error(dev, "HidD_SetFeature");
		return -1;
	}

	return (int) length;
}

static int hid_get_report(hid_device *dev, DWORD report_type, unsigned char *data, size_t length)
{
	BOOL res;
	DWORD bytes_returned = 0;

	OVERLAPPED ol;
	memset(&ol, 0, sizeof(ol));

	res = DeviceIoControl(dev->device_handle,
		report_type,
		data, (DWORD) length,
		data, (DWORD) length,
		&bytes_returned, &ol);

	if (!res) {
		if (GetLastError() != ERROR_IO_PENDING) {
			/* DeviceIoControl() failed. Return error. */
			register_error(dev, "Get Input/Feature Report DeviceIoControl");
			return -1;
		}
	}

	/* Wait here until the write is done. This makes
	   hid_get_feature_report() synchronous. */
	res = GetOverlappedResult(dev->device_handle, &ol, &bytes_returned, TRUE/*wait*/);
	if (!res) {
		/* The operation failed. */
		register_error(dev, "Get Input/Feature Report GetOverLappedResult");
		return -1;
	}

	/* When numbered reports aren't used,
	   bytes_returned seem to include only what is actually received from the device
	   (not including the first byte with 0, as an indication "no numbered reports"). */
	if (data[0] == 0x0) {
		bytes_returned++;
	}

	return bytes_returned;
}

int HID_API_EXPORT HID_API_CALL hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length)
{
	/* We could use HidD_GetFeature() instead, but it doesn't give us an actual length, unfortunately */
	return hid_get_report(dev, IOCTL_HID_GET_FEATURE, data, length);
}

int HID_API_EXPORT HID_API_CALL hid_get_input_report(hid_device *dev, unsigned char *data, size_t length)
{
	/* We could use HidD_GetInputReport() instead, but it doesn't give us an actual length, unfortunately */
	return hid_get_report(dev, IOCTL_HID_GET_INPUT_REPORT, data, length);
}

void HID_API_EXPORT HID_API_CALL hid_close(hid_device *dev)
{
	if (!dev)
		return;

	CancelIo(dev->device_handle);
	free_hid_device(dev);
}

int HID_API_EXPORT_CALL HID_API_CALL hid_get_manufacturer_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
	if (!dev->device_info || !string || !maxlen)
		return -1;

	wcsncpy(string, dev->device_info->manufacturer_string, maxlen);
	string[maxlen] = L'\0';

	return 0;
}

int HID_API_EXPORT_CALL HID_API_CALL hid_get_product_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
	if (!dev->device_info || !string || !maxlen)
		return -1;

	wcsncpy(string, dev->device_info->product_string, maxlen);
	string[maxlen] = L'\0';

	return 0;
}

int HID_API_EXPORT_CALL HID_API_CALL hid_get_serial_number_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
	if (!dev->device_info || !string || !maxlen)
		return -1;

	wcsncpy(string, dev->device_info->serial_number, maxlen);
	string[maxlen] = L'\0';

	return 0;
}

int HID_API_EXPORT_CALL HID_API_CALL hid_get_indexed_string(hid_device *dev, int string_index, wchar_t *string, size_t maxlen)
{
	BOOL res;

	res = HidD_GetIndexedString(dev->device_handle, string_index, string, sizeof(wchar_t) * (DWORD) MIN(maxlen, MAX_STRING_WCHARS));
	if (!res) {
		register_error(dev, "HidD_GetIndexedString");
		return -1;
	}

	return 0;
}


HID_API_EXPORT const wchar_t * HID_API_CALL  hid_error(hid_device *dev)
{
	if (dev) {
		if (dev->last_error_str == NULL)
			return L"Success";
		return (wchar_t*)dev->last_error_str;
	}

	// Global error messages are not (yet) implemented on Windows.
	return L"hid_error for global errors is not implemented yet";
}

#ifdef __cplusplus
} /* extern "C" */
#endif
