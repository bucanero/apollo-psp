#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include <pspiofilemgr.h>
#include <pspopenpsid.h>
#include <mini18n.h>

#include "utils.h"
#include "menu.h"
#include "saves.h"
#include "common.h"
#include "icon0.h"
#include "plugin.h"

#define _i18n(str)                   (str)
#define PSP_UTILITY_COMMON_RESULT_OK (0)
#define GAME_PLUGIN_PATH             "ms0:/seplugins/game.txt"
#define SGKEY_DUMP_PLUGIN_PATH       "ms0:/seplugins/SGKeyDumper.prx"

char *strcasestr(const char *, const char *);
static const char* ext_src[] = {"ms0", "ef0", NULL};
static const char* sort_opt[] = {_i18n("Disabled"), _i18n("by Name"), _i18n("by Title ID"), _i18n("by Type"), NULL};

static void log_callback(int sel);
static void storage_callback(int sel);
static void music_callback(int sel);
static void sort_callback(int sel);
static void ani_callback(int sel);
static void db_url_callback(int sel);
static void ftp_url_callback(int sel);
static void clearcache_callback(int sel);
static void upd_appdata_callback(int sel);

menu_option_t menu_options[] = {
	{ .name = _i18n("Background Music"), 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.music, 
		.callback = music_callback 
	},
	{ .name = _i18n("Menu Animations"), 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.doAni, 
		.callback = ani_callback 
	},
	{ .name = _i18n("Sort Saves"), 
		.options = sort_opt,
		.type = APP_OPTION_LIST,
		.value = &apollo_config.doSort, 
		.callback = sort_callback 
	},
	{ .name = _i18n("External Saves Source"),
		.options = ext_src,
		.type = APP_OPTION_LIST,
		.value = &apollo_config.storage,
		.callback = storage_callback
	},
	{ .name = _i18n("Version Update Check"), 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.update, 
		.callback = update_callback 
	},
	{ .name = _i18n("Set User FTP Server URL"),
		.options = NULL,
		.type = APP_OPTION_CALL,
		.value = NULL,
		.callback = ftp_url_callback
	},
	{ .name = _i18n("Update Application Data"), 
		.options = NULL, 
		.type = APP_OPTION_CALL, 
		.value = NULL, 
		.callback = upd_appdata_callback 
	},
	{ .name = _i18n("Clear Local Cache"), 
		.options = NULL, 
		.type = APP_OPTION_CALL, 
		.value = NULL, 
		.callback = clearcache_callback 
	},
	{ .name = _i18n("Change Online Database URL"),
		.options = NULL,
		.type = APP_OPTION_CALL,
		.value = NULL,
		.callback = db_url_callback 
	},
	{ .name = _i18n("Enable Debug Log"),
		.options = NULL,
		.type = APP_OPTION_BOOL,
		.value = &apollo_config.dbglog,
		.callback = log_callback 
	},
	{ .name = NULL }
};


static void music_callback(int sel)
{
	apollo_config.music = !sel;
}

static void sort_callback(int sel)
{
	apollo_config.doSort = sel;
}

static void ani_callback(int sel)
{
	apollo_config.doAni = !sel;
}

static void clearcache_callback(int sel)
{
	LOG("Cleaning folder '%s'...", APOLLO_LOCAL_CACHE);
	clean_directory(APOLLO_LOCAL_CACHE, "");

	show_message("%s\n%s", _("Local cache folder cleaned:"), APOLLO_LOCAL_CACHE);
}

static void upd_appdata_callback(int sel)
{
	int i;

	if (!http_download(ONLINE_PATCH_URL, "apollo-psp-update.zip", APOLLO_LOCAL_CACHE "appdata.zip", 1))
	{
		show_message(_("Error! Can't download data update pack!"));
		return;
	}

	if ((i = extract_zip(APOLLO_LOCAL_CACHE "appdata.zip", APOLLO_DATA_PATH)) > 0)
		show_message(_("Successfully updated %d data files!"), i);
	else
		show_message(_("Error! Can't extract data update pack!"));

	unlink_secure(APOLLO_LOCAL_CACHE "appdata.zip");
}

void update_callback(int sel)
{
    apollo_config.update = !sel;

    if (!apollo_config.update)
        return;

	LOG("checking latest Apollo version at %s", APOLLO_UPDATE_URL);

	if (!http_download(APOLLO_UPDATE_URL, NULL, APOLLO_LOCAL_CACHE "ver.check", 0))
	{
		LOG("http request to %s failed", APOLLO_UPDATE_URL);
		return;
	}

	char *buffer;
	long size = 0;

	buffer = readTextFile(APOLLO_LOCAL_CACHE "ver.check", &size);

	if (!buffer)
		return;

	LOG("received %ld bytes", size);

	static const char find[] = "\"name\":\"Apollo Save Tool v";
	const char* start = strstr(buffer, find);
	if (!start)
	{
		LOG("no name found");
		goto end_update;
	}

	LOG("found name");
	start += sizeof(find) - 1;

	char* end = strchr(start, '"');
	if (!end)
	{
		LOG("no end of name found");
		goto end_update;
	}
	*end = 0;
	LOG("latest version is %s", start);

	if (strcasecmp(APOLLO_VERSION, start) == 0)
	{
		LOG("no need to update");
		goto end_update;
	}

	start = strstr(end+1, "\"browser_download_url\":\"");
	if (!start)
		goto end_update;

	start += 24;
	end = strchr(start, '"');
	if (!end)
	{
		LOG("no download URL found");
		goto end_update;
	}

	*end = 0;
	LOG("download URL is %s", start);

	if (show_dialog(DIALOG_TYPE_YESNO, _("New version available! Download update?")))
	{
		if (http_download(start, NULL, "ms0:/APOLLO/apollo-psp.zip", 1))
			show_message("%s\n%s", _("Update downloaded to:"), "ms0:/APOLLO/apollo-psp.zip");
		else
			show_message(_("Download error!"));
	}

end_update:
	free(buffer);
	return;
}

static void storage_callback(int sel)
{
	apollo_config.storage = sel;
}

static void log_callback(int sel)
{
	char path[256];

	apollo_config.dbglog = !sel;

	if (!apollo_config.dbglog)
	{
		dbglogger_stop();
		show_message(_("Debug Logging Disabled"));
		return;
	}

	snprintf(path, sizeof(path), APOLLO_PATH "apollo.log", USER_STORAGE_DEV);
	dbglogger_init_mode(FILE_LOGGER, path, 1);
	show_message("%s\n\n%s", _("Debug Logging Enabled!"), path);
}

static void db_url_callback(int sel)
{
	if (!osk_dialog_get_text(_("Enter the Online Database URL"), apollo_config.save_db, sizeof(apollo_config.save_db)))
		return;
	
	if (apollo_config.save_db[strlen(apollo_config.save_db)-1] != '/')
		strcat(apollo_config.save_db, "/");

	show_message("%s\n%s", _("Online database URL changed to:"), apollo_config.save_db);
}

static void ftp_url_callback(int sel)
{
	int ret;
	char *data;
	char tmp[512];

	if (!network_up())
	{
		show_message("%s\n\n%s", _("Network is not available!"), _("Please connect to a network first."));
		return;
	}

	strncpy(tmp, apollo_config.ftp_url[0] ? apollo_config.ftp_url : "ftp://user:pass@192.168.0.10:21/folder/", sizeof(tmp));
	if (!osk_dialog_get_text(_("Enter the FTP server URL"), tmp, sizeof(tmp)))
		return;

	strncpy(apollo_config.ftp_url, tmp, sizeof(apollo_config.ftp_url));

	if (apollo_config.ftp_url[strlen(apollo_config.ftp_url)-1] != '/')
		strcat(apollo_config.ftp_url, "/");

	// test the connection
	init_loading_screen("Testing connection...");
	ret = http_download(apollo_config.ftp_url, "apollo.txt", APOLLO_LOCAL_CACHE "users.ftp", 0);
	data = ret ? readTextFile(APOLLO_LOCAL_CACHE "users.ftp", NULL) : NULL;
	if (!data)
		data = strdup("; Apollo Save Tool (" APOLLO_PLATFORM ") v" APOLLO_VERSION "\r\n");

	snprintf(tmp, sizeof(tmp), "%016" PRIX64, apollo_config.account_id);
	if (strstr(data, tmp) == NULL)
	{
		LOG("Updating users index...");
		FILE* fp = fopen(APOLLO_LOCAL_CACHE "users.ftp", "w");
		if (fp)
		{
			fprintf(fp, "%s%s\r\n", data, tmp);
			fclose(fp);
		}

		ret = ftp_upload(APOLLO_LOCAL_CACHE "users.ftp", apollo_config.ftp_url, "apollo.txt", 0);
	}
	free(data);
	stop_loading_screen();

	if (ret)
		show_message("%s\n%s", _("FTP server URL changed to:"), apollo_config.ftp_url);
	else
		show_message("%s\n%s\n\n%s", _("Error! Couldn't connect to FTP server"), apollo_config.ftp_url, _("Check debug logs for more information"));
}

static int is_psp_go(void)
{
    SceDevInf inf;
    SceDevctlCmd cmd;

    cmd.dev_inf = &inf;
    memset(&inf, 0, sizeof(SceDevInf));

    return !(sceIoDevctl("ef0:", SCE_PR_GETDEV, &cmd, sizeof(SceDevctlCmd), NULL, 0) < 0);
}

/*
static void initSavedata(SceUtilitySavedataParam * savedata, int mode, void* usr_data)
{
	memset(savedata, 0, sizeof(SceUtilitySavedataParam));
	savedata->base.size = sizeof(SceUtilitySavedataParam);
	savedata->base.language = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	savedata->base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
	savedata->base.graphicsThread = 0x11;
	savedata->base.accessThread = 0x13;
	savedata->base.fontThread = 0x12;
	savedata->base.soundThread = 0x10;
	savedata->mode = mode;
	savedata->overwrite = 1;
	savedata->focus = PSP_UTILITY_SAVEDATA_FOCUS_LATEST; // Set initial focus to the newest file (for loading)

	strncpy(savedata->key, "bucanero.com.ar", sizeof(savedata->key));
	strncpy(savedata->gameName, "NP0APOLLO", sizeof(savedata->gameName));	// First part of the save name, game identifier name
	strncpy(savedata->saveName, "-Settings", sizeof(savedata->saveName));	// Second part of the save name, save identifier name
	strncpy(savedata->fileName, "SETTINGS.BIN", sizeof(savedata->fileName));	// name of the data file

	// Allocate buffers used to store various parts of the save data
	savedata->dataBuf = usr_data;
	savedata->dataBufSize = sizeof(app_config_t);
	savedata->dataSize = sizeof(app_config_t);

	// Set save data
	if (mode == PSP_UTILITY_SAVEDATA_AUTOSAVE)
	{
		strcpy(savedata->sfoParam.title, "Apollo Save Tool");
		strcpy(savedata->sfoParam.savedataTitle,"Settings");
		strcpy(savedata->sfoParam.detail,"www.bucanero.com.ar");
		savedata->sfoParam.parentalLevel = 1;

		// Set icon0
		savedata->icon0FileData.buf = icon0;
		savedata->icon0FileData.bufSize = size_icon0;
		savedata->icon0FileData.size = size_icon0;
		savedata->focus = PSP_UTILITY_SAVEDATA_FOCUS_FIRSTEMPTY; // If saving, set inital focus to the first empty slot
	}
}

static int runSaveDialog(int mode, void* data)
{
	SceUtilitySavedataParam dialog;

	initSavedata(&dialog, mode, data);
	if (sceUtilitySavedataInitStart(&dialog) < 0)
		return 0;

	do {
		mode = sceUtilitySavedataGetStatus();
		switch(mode)
		{
			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilitySavedataUpdate(1);
				break;

			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilitySavedataShutdownStart();
				break;
		}
	} while (mode != PSP_UTILITY_DIALOG_FINISHED);

	return (dialog.base.result == PSP_UTILITY_COMMON_RESULT_OK);
}
*/

int save_app_settings(app_config_t* config)
{
	char filePath[256];
	Byte dest[SIZE_PARAMSFO];
	uLong destLen = SIZE_PARAMSFO;

	LOG("Apollo Save Tool %s v%s - Patch Engine v%s", APOLLO_PLATFORM, APOLLO_VERSION, APOLLO_LIB_VERSION);
	snprintf(filePath, sizeof(filePath), "%s%s%s%s", MS0_PATH, USER_PATH_HDD, "NP0APOLLO-Settings/", "ICON0.PNG");
	if (mkdirs(filePath) != SUCCESS)
	{
		LOG("sceSaveDataMount2 ERROR");
		return 0;
	}

	LOG("Saving Settings...");
	write_buffer(filePath, icon0, sizeof(icon0));

	snprintf(filePath, sizeof(filePath), "%s%s%s%s", MS0_PATH, USER_PATH_HDD, "NP0APOLLO-Settings/", "PARAM.SFO");
	uncompress(dest, &destLen, paramsfo, sizeof(paramsfo));
	write_buffer(filePath, dest, destLen);

	snprintf(filePath, sizeof(filePath), "%s%s%s%s", MS0_PATH, USER_PATH_HDD, "NP0APOLLO-Settings/", "SETTINGS.BIN");
	if (write_buffer(filePath, (uint8_t*) config, sizeof(app_config_t)) < 0)
	{
		LOG("Error saving settings!");
		return 0;
	}

	return 1;
}

int load_app_settings(app_config_t* config)
{
	char filePath[256];
	app_config_t file_data;

	sceOpenPSIDGetOpenPSID((PspOpenPSID*) config->psid);

	snprintf(filePath, sizeof(filePath), "%s%s%s%s", MS0_PATH, USER_PATH_HDD, "NP0APOLLO-Settings/", "SETTINGS.BIN");

	LOG("Loading Settings...");
	if (read_file(filePath, (uint8_t*) &file_data, sizeof(app_config_t)) == SUCCESS)
	{
		memcpy(file_data.psid, config->psid, sizeof(PspOpenPSID));
		memcpy(config, &file_data, sizeof(app_config_t));

		LOG("Settings loaded: PSID (%016" PRIX64 " %016" PRIX64 ")", config->psid[0], config->psid[1]);
		return 1;
	}

	LOG("Settings not found, using defaults");
	config->storage = is_psp_go();
	config->account_id = get_account_id();
	save_app_settings(config);
	return 0;
}

int install_sgkey_plugin(int install)
{
	char* data;
	size_t size;

	mkdirs(SGKEY_DUMP_PLUGIN_PATH);
	if (write_buffer(SGKEY_DUMP_PLUGIN_PATH, sgk_plugin, sizeof(sgk_plugin)) < 0)
		return 0;

	if (read_buffer(GAME_PLUGIN_PATH, (uint8_t**) &data, &size) < 0)
	{
		LOG("Error reading game.txt");
		if (!install)
			return 0;

		if (write_buffer(GAME_PLUGIN_PATH, SGKEY_DUMP_PLUGIN_PATH " 1\n", strlen(SGKEY_DUMP_PLUGIN_PATH)+3) < 0)
		{
			LOG("Error creating game.txt");
			return 0;
		}

		return 1;
	}

	if (install)
	{
		char *ptr = strcasestr(data, SGKEY_DUMP_PLUGIN_PATH " ");
		if (ptr != NULL && (ptr[31] == '1' || ptr[31] == '0'))
		{
			LOG("Plugin enabled");
			ptr[31] = '1';
			write_buffer(GAME_PLUGIN_PATH, data, size);
			free(data);

			return 1;
		}
		free(data);

		FILE* fp = fopen(GAME_PLUGIN_PATH, "a");
		if (!fp)
		{
			LOG("Error opening game.txt");
			return 0;
		}

		fprintf(fp, "%s%s", SGKEY_DUMP_PLUGIN_PATH, " 1\n");
		fclose(fp);
		return 1;
	}
	else
	{
		char *ptr = strcasestr(data, SGKEY_DUMP_PLUGIN_PATH " ");
		if (ptr != NULL && (ptr[31] == '1' || ptr[31] == '0'))
		{
			LOG("Plugin disabled");
			ptr[31] = '0';
			write_buffer(GAME_PLUGIN_PATH, data, size);
		}
		free(data);
		return 1;
	}

	return 0;
}
