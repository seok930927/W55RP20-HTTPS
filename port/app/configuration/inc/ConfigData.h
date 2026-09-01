/*
    ConfigData.h
*/

#ifndef __CONFIGDATA_H__
#define __CONFIGDATA_H__

#include <stdint.h>

#define DEVICE_NAME_SIZE        20
#define DEVICE_ALIAS_SIZE       40
#define DEVICE_GROUP_SIZE       40
#define DEVICE_CONNECT_DATA_SIZE 32

#define DNS_DOMAIN_SIZE         128

#define MQTT_TOPIC_SIZE		    128
#define MQTT_USER_NAME_SIZE		128
#define MQTT_PASSWORD_SIZE		128
#define MQTT_CLIENT_ID_SIZE		128
// Safe Save
#define MAX_SAVE_RETRY              5
#define SAVE_INTERVAL_MS            1000

#if (DEVICE_BOARD_NAME == PLATYPUS_S2E)
/* flsystem OUI (IEEE MA-S): 8C:1F:64:4E:Bx — 기입 시작 주소 8C:1F:64:4E:B2:00 */
#define MAC_OUI0  0x8C
#define MAC_OUI1  0x1F
#define MAC_OUI2  0x64
#else
#define MAC_OUI0  0x00
#define MAC_OUI1  0x08
#define MAC_OUI2  0xDC
#endif

enum bank_num {
    APP_BANK0 = 0,
    APP_BANK1
};

struct __device_common {
    uint8_t fw_ver[3];          // Major Version . Minor Version . Maintenance Version
    uint8_t device_type[3];
    uint8_t device_name[DEVICE_NAME_SIZE];
    uint8_t device_mode;
} __attribute__((packed));

// todo: group settings
struct __config_common {
    uint16_t app_protocol;
    uint16_t packet_size;
    char pw_search[10];
} __attribute__((packed));

struct __network_common {
    uint8_t mac[6];
    uint8_t local_ip[4];
    uint8_t gateway[4];
    uint8_t subnet[4];
} __attribute__((packed));

struct __network_connection {
    uint8_t working_mode;           // TCP_CLIENT_MODE (0), TCP_SERVER_MODE (1), TCP_MIXED_MODE (2), UDP_MODE (3), SSL_TCP_CLIENT_MODE (4), MQTT_CLIENT_MODE (5), MQTTS_CLIENT_MODE (6)
    uint8_t working_state;          // Network operation state: BOOT(0), OPEN (1), CONNECT (2), UPGRADE (3), ATMODE (4)
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t remote_ip[4];
    uint8_t fixed_local_port;
    uint8_t dns_use;
    char dns_domain_name[DNS_DOMAIN_SIZE];
} __attribute__((packed));

struct __network_option {
    uint8_t dhcp_use;
    uint8_t dns_server_ip[4];
    uint8_t tcp_rcr_val;
} __attribute__((packed));

struct __tcp_option {
    uint16_t inactivity;
    uint16_t reconnection;
    uint8_t keepalive_en;
    uint16_t keepalive_wait_time;
    uint16_t keepalive_retry_time;
    char pw_connect[10];
    uint8_t pw_connect_en;
} __attribute__((packed));

struct __serial_common {
    uint8_t uart_interface_cnt;
    uint8_t serial_debug_en;
} __attribute__((packed));

struct __serial_option {        // todo: [0] RS-232/TTL, [1] RS-422 [2] RS-485
    uint8_t uart_interface;     // UART interface; [0] TTL [1] RS-232 [2] RS-422 [3] RS-485, This value is determined at the initial routine of device.
    uint8_t protocol;           // Serial communication protocol; [0] None, [1] Modbus RTU, [2] Modbus ASCII
    uint8_t baud_rate;          // 0 ~ (enum)
    uint8_t data_bits;          // 7, 8, 9
    uint8_t parity;             // None, Odd, Even
    uint8_t stop_bits;          // 1, 1.5, 2
    uint8_t flow_control;       // None, RTS/CTS, XON/XOFF, RTS Only for RS422/485
    uint8_t dtr_en;             // DTR/DSR Enable, Pins for these signals are shared with [Connection status pins]
    uint8_t dsr_en;             // DTR/DSR Enable, Pins for these signals are shared with [Connection status pins]
} __attribute__((packed));

struct __serial_data_packing {
    uint16_t packing_time;              // 0~2048
    uint16_t packing_size;              // 0~2048
    uint8_t packing_delimiter[4];
    uint8_t packing_delimiter_length;   // 0~4
    uint8_t packing_data_appendix;      // 0~2
} __attribute__((packed));

struct __serial_command {
    uint8_t serial_command;         // Serial Command Mode enable
    uint8_t serial_trigger[3];      // Serial Command mode entry trigger code (default: +++)
    uint8_t serial_command_echo;    // Serial Command echoback enable
} __attribute__((packed));

struct __user_io_info {
    uint16_t user_io_enable;        // 0: Disable / 1: Enable
    uint16_t user_io_type;          // 0: Digital / 1: Analog
    uint16_t user_io_direction;     // 0: Input / 1: Output
    uint16_t user_io_status;        // Digital Output only`
} __attribute__((packed));

struct __firmware_update {
    uint8_t fwup_flag;
    uint16_t fwup_port;
    uint32_t fwup_size;
    uint8_t fwup_server_flag;
    uint16_t fwup_server_port;
    uint8_t fwup_copy_flag;
} __attribute__((packed));

#if 1
struct __device_option {
    uint8_t pw_setting_en;
    uint8_t pw_setting[10];
    uint8_t device_alias[DEVICE_ALIAS_SIZE];
    uint8_t device_group[DEVICE_GROUP_SIZE];
    uint8_t device_serial_connect_data[DEVICE_CONNECT_DATA_SIZE]; // Serial Send data when connected
    uint8_t device_serial_disconnect_data[DEVICE_CONNECT_DATA_SIZE];  // Serial Send data when disconnected
    uint8_t device_eth_connect_data[DEVICE_CONNECT_DATA_SIZE]; // Ethernet Send data when connected
    //    uint8_t ntp_domain_name[NTP_SERVER_DOMAIN_CNT][NTP_SERVER_DOMAIN_SIZE];
} __attribute__((packed));
#endif

struct __ssl_option {
    uint8_t root_ca_option;     //0: Verify_none / 1: Verify_option / 2: Verify_require
    uint8_t client_cert_enable;
    uint32_t rootca_len;
    uint32_t clica_len;
    uint32_t pkey_len;
    uint32_t recv_timeout;
} __attribute__((packed));

struct __mqtt_option {
    uint8_t pub_topic[MQTT_TOPIC_SIZE];
    uint8_t sub_topic_0[MQTT_TOPIC_SIZE];
    uint8_t sub_topic_1[MQTT_TOPIC_SIZE];
    uint8_t sub_topic_2[MQTT_TOPIC_SIZE];
    uint8_t user_name[MQTT_USER_NAME_SIZE];
    uint8_t client_id[MQTT_CLIENT_ID_SIZE];
    uint8_t password[MQTT_PASSWORD_SIZE];
    uint32_t keepalive;
    uint8_t qos;
} __attribute__((packed));


/*
    ============================================================
    [LEGACY SECTION] — DO NOT MODIFY LAYOUT
    Byte-for-byte compatible with pre-94177ca DevConfig.
    CONFIG Tool / bootloader rely on this offset map.
    Append-only growth is forbidden here; use reserved_legacy
    (and bump ext_version) to absorb future fields if needed.
    ============================================================

    [EXTENSION SECTION] — new fields land here
    Gated by ext_magic / ext_version. If a flash blob is read
    from an older firmware (legacy section valid, extension area
    blank/garbage), load_DevConfig_from_storage() re-initializes
    the extension area in place without wiping legacy data.
*/

/* SNMP option lives in the extension section */
#define SNMP_ALLOWED_IP_CNT   4
#define SNMP_TRAP_IP_CNT      4
struct __snmp_option {
    uint8_t allowed_ip[SNMP_ALLOWED_IP_CNT][4];
    uint8_t trap_ip[SNMP_TRAP_IP_CNT][4];
} __attribute__((packed));

#define DEVCONFIG_EXT_MAGIC      0x57495A45UL  /* 'WIZE' (LE: 45 5A 49 57) */
/*  ext_version history:
      2: allowed_ip/trap_ip slots 2->4 (struct __snmp_option grew 16B)
      3: add serial_option_485 (RS-485 = uart0 gets its own baud/format,
         independent of legacy serial_option which drives RS-232 = uart1)
      4: add https_port / snmp_agent_port (web-configurable service ports)
      5: add web_access_ip[2][4] (HTTPS source-IP allow list; 0.0.0.0 => any)
    Devices with an older ext_version re-init the extension area on first boot. */
#define DEVCONFIG_EXT_VERSION    5
#define DEVCONFIG_RESERVED_LEGACY_SIZE  64
/*  reserved_ext shrinks as named ext fields are added, keeping sizeof(DevConfig)
    constant so existing flash blobs stay layout-compatible.
    89 = 128 - https_session_timeout_min(2) - snmp_option slot growth(16)
             - sizeof(struct __serial_option)(9) - https_port(2) - snmp_agent_port(2)
             - web_access_ip(8). */
#define DEVCONFIG_RESERVED_EXT_SIZE    89

/* Service port defaults / bounds (0 stored => use default at runtime). */
#define HTTPS_PORT_DEFAULT        443
#define SNMP_AGENT_PORT_DEFAULT   161

/* HTTPS web access source-IP allow list (0.0.0.0 in all slots => allow any). */
#define WEB_ACCESS_IP_CNT         2

typedef struct __DevConfig {
    /* ---- LEGACY SECTION (frozen layout) ---- */
    struct __device_common device_common;
    struct __config_common config_common;
    struct __network_common network_common;
    struct __network_connection network_connection;
    struct __network_option network_option;
    struct __tcp_option tcp_option;
    struct __serial_common serial_common;
    struct __serial_command serial_command;
    struct __serial_option serial_option;
    struct __serial_data_packing serial_data_packing;
    struct __user_io_info user_io_info;
    struct __firmware_update firmware_update;
    struct __ssl_option ssl_option;
    struct __mqtt_option mqtt_option;
    struct __device_option device_option;
    uint32_t devConfigVer;

    /* ---- RESERVED PADDING (legacy-side growth absorber) ---- */
    uint8_t reserved_legacy[DEVCONFIG_RESERVED_LEGACY_SIZE];

    /* ---- EXTENSION SECTION (new fields, ext_magic gated) ---- */
    uint32_t ext_magic;
    uint16_t ext_version;
    uint16_t ext_reserved0;
    struct __snmp_option snmp_option;
    uint16_t https_session_timeout_min;   /* HTTPS login session lifetime (min); 0 => default */
    struct __serial_option serial_option_485;  /* RS-485 (uart0) port settings, independent of
                                                  legacy serial_option which drives RS-232 (uart1) */
    uint16_t https_port;        /* HTTPS server TCP port; 0 => HTTPS_PORT_DEFAULT (443) */
    uint16_t snmp_agent_port;   /* SNMP agent UDP port; 0 => SNMP_AGENT_PORT_DEFAULT (161) */
    uint8_t  web_access_ip[WEB_ACCESS_IP_CNT][4];  /* HTTPS source-IP allow list; all 0 => any */
    uint8_t reserved_ext[DEVCONFIG_RESERVED_EXT_SIZE];
} __attribute__((packed)) DevConfig;

/* HTTPS session timeout bounds (minutes). 0/out-of-range => DEFAULT. */
#define HTTPS_SESSION_TIMEOUT_MIN_DEFAULT  30
#define HTTPS_SESSION_TIMEOUT_MIN_MIN      1
#define HTTPS_SESSION_TIMEOUT_MIN_MAX      1440

DevConfig* get_DevConfig_pointer(void);
void set_DevConfig_to_factory_value(void);
void set_DevConfig_ext_to_factory_value(void);
void load_DevConfig_from_storage(void);
void save_DevConfig_to_storage(void);
void get_DevConfig_value(void *dest, const void *src, uint16_t size);
void set_DevConfig_value(void *dest, const void *value, const uint16_t size);

void display_Net_Info(void);
void Mac_Conf(void);
void Net_Conf(void);
void set_dhcp_mode(void);

uint8_t get_hex(uint8_t b0, uint8_t b1);
char atonum(char ch);
void check_mac_address(void);

#endif /* __CONFIGDATA_H__ */
