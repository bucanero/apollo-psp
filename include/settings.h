#define APOLLO_VERSION          "2.0.0"     //Apollo PSP version (about menu)
#define APOLLO_PLATFORM         "PSP"       //Apollo platform

#define MENU_TITLE_OFF			30			//Offset of menu title text from menu mini icon
#define MENU_ICON_OFF 			35          //X Offset to start printing menu mini icon
#define MENU_ANI_MAX 			0x80        //Max animation number
#define MENU_SPLIT_OFF			100			//Offset from left of sub/split menu to start drawing
#define OPTION_ITEM_OFF         365         //Offset from left of settings item/value

#define USER_STORAGE_DEV        menu_options[3].options[apollo_config.storage]

enum app_option_type
{
    APP_OPTION_NONE,
    APP_OPTION_BOOL,
    APP_OPTION_LIST,
    APP_OPTION_INC,
    APP_OPTION_CALL,
};

typedef struct
{
	char * name;
	char * * options;
	int type;
	uint8_t * value;
	void(*callback)(int);
} menu_option_t;

typedef struct __attribute__((packed))
{
    char app_name[8];
    char app_ver[8];
    uint8_t music;
    uint8_t doSort;
    uint8_t doAni;
    uint8_t storage;
    uint8_t update;
    uint8_t dbglog;
    uint64_t account_id;
    uint64_t psid[2];
    char save_db[256];
    char ftp_url[512];
} app_config_t;

extern menu_option_t menu_options[];
extern app_config_t apollo_config;

void update_callback(int sel);
