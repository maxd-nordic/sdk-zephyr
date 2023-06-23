#include <stdint.h>

enum ms_os_20_type_t
{
  MS_OS_20_SET_HEADER_DESCRIPTOR       = 0x00,
  MS_OS_20_SUBSET_HEADER_CONFIGURATION = 0x01,
  MS_OS_20_SUBSET_HEADER_FUNCTION      = 0x02,
  MS_OS_20_FEATURE_COMPATIBLE_ID       = 0x03,
  MS_OS_20_FEATURE_REG_PROPERTY        = 0x04,
  MS_OS_20_FEATURE_MIN_RESUME_TIME     = 0x05,
  MS_OS_20_FEATURE_MODEL_ID            = 0x06,
  MS_OS_20_FEATURE_CCGP_DEVICE         = 0x07,
  MS_OS_20_FEATURE_VENDOR_REVISION     = 0x08
};

enum ms_os_20_property_data_type_t
{
  MS_OS_20_PROPERTY_DATA_RESERVED = 0,
  MS_OS_20_PROPERTY_DATA_REG_SZ = 1,
  MS_OS_20_PROPERTY_DATA_REG_EXPAND_SZ = 2,
  MS_OS_20_PROPERTY_DATA_REG_BINARY = 3,
  MS_OS_20_PROPERTY_DATA_REG_DWORD_LITTLE_ENDIAN = 4,
  MS_OS_20_PROPERTY_DATA_REG_DWORD_BIG_ENDIAN = 5,
  MS_OS_20_PROPERTY_DATA_REG_LINK = 6,
  MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ = 7
};

/* Microsoft OS 2.0 descriptor set header */
struct ms_os_20_descriptor_set_header_descriptor {
	uint16_t wLength; /* 10 */
	uint16_t wDescriptorType; /* MS_OS_20_SET_HEADER_DESCRIPTOR */
	uint32_t dwWindowsVersion; /* Windows 8.1: 0x06030000 */
	uint16_t wTotalLength; /* The size of entire MS OS 2.0 descriptor set. */
} __packed;

/* Microsoft OS 2.0 function subset header
 * Note: This must be used if your device has multiple interfaces and cannot be used otherwise.
 */
struct ms_os_20_function_subset_header_descriptor {
	uint16_t wLength; /* 8 */
	uint16_t wDescriptorType; /* MS_OS_20_SUBSET_HEADER_FUNCTION */
	uint8_t bFirstInterface; /* The interface number for the first interface of the function to which this subset applies. */
	uint8_t bReserved; /* 0 */
	uint16_t wSubsetLength; /* The size of entire function subset including this header.*/
} __packed;

/* Microsoft OS 2.0 compatible ID descriptor */
struct ms_os_20_compatible_id_descriptor {
	uint16_t wLength; /* 20 */
	uint16_t wDescriptorType; /* MS_OS_20_FEATURE_COMPATIBLE_ID */
	uint8_t CompatibleID[8]; /* Compatible ID String */
	uint8_t SubCompatibleID[8]; /* Sub-compatible ID String */
} __packed;

/* Microsoft OS 2.0 Registry property descriptor: DeviceInterfaceGUIDs */
struct ms_os_20_guids_property_descriptor {
	uint16_t wLength; /* The length of this descriptor in bytes. */
	uint16_t wDescriptorType; /* MS_OS_20_FEATURE_REG_PROPERTY */
	uint16_t wPropertyDataType; /* MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ */
	uint16_t wPropertyNameLength; /* 42 */
	uint8_t PropertyName[42]; /* "DeviceInterfaceGUIDs\0" in UTF-16 */
	uint16_t wPropertyDataLength; /* 80 */
	uint8_t bPropertyData[80]; /* “{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\0” */
} __packed;

/* DeviceInterfaceGUIDs */
#define DEVICE_INTERFACE_GUIDS_PROPERTY_NAME \
	'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, \
	'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00, \
	'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, \
	'D', 0x00, 's', 0x00, 0x00, 0x00
