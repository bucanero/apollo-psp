#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <pspiofilemgr.h>
#include <psprtc.h>

#include "saves.h"
#include "common.h"
#include "sfo.h"
#include "settings.h"
#include "utils.h"
#include "ps1card.h"

#define UTF8_CHAR_STAR		"\xE2\x98\x85"

#define CHAR_ICON_VMC		"\x02"
#define CHAR_ICON_NET		"\x09"
#define CHAR_ICON_ZIP		"\x0C"
#define CHAR_ICON_COPY		"\x0B"
#define CHAR_ICON_SIGN		"\x06"
#define CHAR_ICON_USER		"\x07"
#define CHAR_ICON_LOCK		"\x08"
#define CHAR_ICON_WARN		"\x0F"


/*
 * Function:		endsWith()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Checks to see if a ends with b
 * Arguments:
 *	a:				String
 *	b:				Potential end
 * Return:			pointer if true, NULL if false
 */
static char* endsWith(const char * a, const char * b)
{
	int al = strlen(a), bl = strlen(b);
    
	if (al < bl)
		return NULL;

	a += (al - bl);
	while (*a)
		if (toupper(*a++) != toupper(*b++)) return NULL;

	return (char*) (a - bl);
}

/*
 * Function:		readFile()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		reads the contents of a file into a new buffer
 * Arguments:
 *	path:			Path to file
 * Return:			Pointer to the newly allocated buffer
 */
char * readTextFile(const char * path, long* size)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0)
	{
		fclose(f);
		return NULL;
	}

	char * string = malloc(fsize + 1);
	fread(string, fsize, 1, f);
	fclose(f);

	string[fsize] = 0;
	if (size)
		*size = fsize;

	return string;
}

static code_entry_t* _createCmdCode(uint8_t type, const char* name, char code)
{
	code_entry_t* entry = (code_entry_t *)calloc(1, sizeof(code_entry_t));
	entry->type = type;
	entry->name = strdup(name);
	asprintf(&entry->codes, "%c", code);

	return entry;
}

static option_entry_t* _initOptions(int count)
{
	option_entry_t* options = (option_entry_t*)calloc(count, sizeof(option_entry_t));

	for(int i = 0; i < count; i++)
	{
		options[i].sel = -1;
		options[i].opts = list_alloc();
	}

	return options;
}

static option_entry_t* _createOptions(int count, const char* name, char value)
{
	option_value_t* optval;
	option_entry_t* options = _initOptions(count);

	optval = malloc(sizeof(option_value_t));
	asprintf(&optval->name, "%s (%s)", name, MS0_PATH);
	asprintf(&optval->value, "%c%c", value, STORAGE_MS0);
	list_append(options[0].opts, optval);

	optval = malloc(sizeof(option_value_t));
	asprintf(&optval->name, "%s (%s)", name, EF0_PATH);
	asprintf(&optval->value, "%c%c", value, STORAGE_EF0);
	list_append(options[0].opts, optval);

	return options;
}

static save_entry_t* _createSaveEntry(uint16_t flag, const char* name)
{
	save_entry_t* entry = (save_entry_t *)calloc(1, sizeof(save_entry_t));
	entry->flags = flag;
	entry->name = strdup(name);

	return entry;
}

static void _walk_dir_list(const char* startdir, const char* inputdir, const char* mask, list_t* list)
{
	char fullname[256];	
	struct dirent *dirp;
	int len = strlen(startdir);
	DIR *dp = opendir(inputdir);

	if (!dp) {
		LOG("Failed to open input directory: '%s'", inputdir);
		return;
	}

	while ((dirp = readdir(dp)) != NULL)
	{
		if ((strcmp(dirp->d_name, ".")  == 0) || (strcmp(dirp->d_name, "..") == 0) ||
			(strcmp(dirp->d_name, "ICON0.PNG") == 0) || (strcmp(dirp->d_name, "PARAM.SFO") == 0) || (strcmp(dirp->d_name,"PIC1.PNG") == 0) ||
			(strcmp(dirp->d_name, "ICON1.PMF") == 0) || (strcmp(dirp->d_name, "SND0.AT3") == 0))
			continue;

		snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

		if (wildcard_match_icase(dirp->d_name, mask))
		{
			//LOG("Adding file '%s'", fullname+len);
			list_append(list, strdup(fullname+len));
		}
	}
	closedir(dp);
}

static option_entry_t* _getFileOptions(const char* save_path, const char* mask, uint8_t is_cmd)
{
	char *filename;
	list_t* file_list;
	list_node_t* node;
	option_value_t* optval;
	option_entry_t* opt;

	LOG("Loading filenames {%s} from '%s'...", mask, save_path);

	file_list = list_alloc();
	_walk_dir_list(save_path, save_path, mask, file_list);

	if (!list_count(file_list))
	{
		is_cmd = 0;
		asprintf(&filename, CHAR_ICON_WARN " --- %s%s --- " CHAR_ICON_WARN, save_path, mask);
		list_append(file_list, filename);
	}

	opt = _initOptions(1);

	for (node = list_head(file_list); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Adding '%s' (%s)", filename, mask);

		optval = malloc(sizeof(option_value_t));
		optval->name = filename;

		if (is_cmd)
			asprintf(&optval->value, "%c", is_cmd);
		else
			asprintf(&optval->value, "%s", mask);

		list_append(opt[0].opts, optval);
	}

	list_free(file_list);

	return opt;
}

static void _addBackupCommands(save_entry_t* item)
{
	code_entry_t* cmd;
	option_value_t* optval;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Apply Changes & Resign", CMD_RESIGN_SAVE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy save game", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Save to Backup Storage", CMD_COPY_SAVE_USB);
	if (!(item->flags & SAVE_FLAG_HDD))
	{
		optval = malloc(sizeof(option_value_t));
		asprintf(&optval->name, "Copy Save to Memory Stick (%s:/PSP)", USER_STORAGE_DEV);
		asprintf(&optval->value, "%c", CMD_COPY_SAVE_HDD);
		list_append(cmd->options[0].opts, optval);
	}
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_ZIP " Export save game to Zip", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Export Zip to Backup Storage", CMD_EXPORT_ZIP_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export decrypted save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_DECRYPT_FILE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import decrypted save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_IMPORT_DATA_FILE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Hex Edit save game files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_HEX_EDIT_FILE);
	list_append(item->codes, cmd);
}

static option_entry_t* _getSaveTitleIDs(const char* title_id)
{
	option_value_t* optval;
	option_entry_t* opt;
	char tmp[16];
	const char *ptr;
	const char *tid = NULL;//get_game_title_ids(title_id);

	if (!tid)
		tid = title_id;

	LOG("Adding TitleIDs=%s", tid);
	opt = _initOptions(1);

	ptr = tid;
	while (*ptr++)
	{
		if ((*ptr == '/') || (*ptr == 0))
		{
			memset(tmp, 0, sizeof(tmp));
			strncpy(tmp, tid, ptr - tid);
			optval = malloc(sizeof(option_value_t));
			asprintf(&optval->name, "%s", tmp);
			asprintf(&optval->value, "%c", SFO_CHANGE_TITLE_ID);
			list_append(opt[0].opts, optval);
			tid = ptr+1;
		}
	}

	return opt;
}

static void add_vmp_commands(save_entry_t* save)
{
	char path[256];
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Virtual Memory Card " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	if (endsWith(save->path, ".VMP"))
	{
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Resign Memory Card", CMD_RESIGN_VMP);
		list_append(save->codes, cmd);
	}

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export Memory Card to .MCR", CMD_EXP_VMP2MCR);
	list_append(save->codes, cmd);

	snprintf(path, sizeof(path), CHAR_ICON_COPY " Import .MCR files to %s", strrchr(save->path, '/')+1);
	cmd = _createCmdCode(PATCH_COMMAND, path, CMD_CODE_NULL);
	cmd->options_count = 1;
	snprintf(path, sizeof(path), PS1_SAVES_PATH_HDD "%s/", USER_STORAGE_DEV, save->title_id);
	cmd->options = _getFileOptions(path, "*.MCR", CMD_IMP_MCR2VMP);
	list_append(save->codes, cmd);

	return;
}

static void add_psp_commands(save_entry_t* item)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_WARN " Delete Save Game", CMD_DELETE_SAVE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Game Key Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Export binary Game Key", CMD_EXP_PSPKEY);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Dump Game Key (text file)", CMD_DUMP_PSPKEY);
	list_append(item->codes, cmd);

	return;
}

static option_entry_t* get_file_entries(const char* path, const char* mask)
{
	return _getFileOptions(path, mask, CMD_CODE_NULL);
}

/*
 * Function:		ReadLocalCodes()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads an entire NCL file into an array of code_entry
 * Arguments:
 *	path:			Path to ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadCodes(save_entry_t * save)
{
	code_entry_t * code;
	char filePath[256];
	char * buffer = NULL;

	save->codes = list_alloc();
	_addBackupCommands(save);
	add_psp_commands(save);

	snprintf(filePath, sizeof(filePath), APOLLO_DATA_PATH "%s.savepatch", save->title_id);
	if ((buffer = readTextFile(filePath, NULL)) == NULL)
		goto skip_end;

	code = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Cheats " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);	
	list_append(save->codes, code);

	code = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Raw Patch File", CMD_VIEW_RAW_PATCH);
	list_append(save->codes, code);

	LOG("Loading BSD codes '%s'...", filePath);
	load_patch_code_list(buffer, save->codes, &get_file_entries, save->path);
	free (buffer);

skip_end:
	LOG("Loaded %ld codes", list_count(save->codes));

	return list_count(save->codes);
}

static void add_vmc_import_saves(list_t* list, const char* path, const char* folder)
{
	code_entry_t * cmd;
	DIR *d;
	struct dirent *dir;
	char psvPath[256];

	snprintf(psvPath, sizeof(psvPath), "%s%s", path, folder);
	d = opendir(psvPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!endsWith(dir->d_name, ".PSV") && !endsWith(dir->d_name, ".MCS") && !endsWith(dir->d_name, ".PSX") &&
			!endsWith(dir->d_name, ".PS1") && !endsWith(dir->d_name, ".MCB") && !endsWith(dir->d_name, ".PDA"))
			continue;

		snprintf(psvPath, sizeof(psvPath), "%s %s", CHAR_ICON_COPY, dir->d_name);
		cmd = _createCmdCode(PATCH_COMMAND, psvPath, CMD_IMP_VMCSAVE);
		asprintf(&cmd->file, "%s%s%s", path, folder, dir->d_name);
		cmd->codes[1] = FILE_TYPE_PS1;
		list_append(list, cmd);

		LOG("[%s] F(%X) name '%s'", cmd->file, cmd->flags, cmd->name+2);
	}

	closedir(d);
}

static void read_vmc_files(const char* path, list_t* list)
{
	save_entry_t *item;
	DIR *d;
	struct dirent *dir;
	char vmcPath[256];

	snprintf(vmcPath, sizeof(vmcPath), "%s%s", path, PS1_VMC_PATH_USB);
	d = opendir(vmcPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!endsWith(dir->d_name, ".VMP") && !endsWith(dir->d_name, ".MCR") && !endsWith(dir->d_name, ".GME") &&
			!endsWith(dir->d_name, ".VM1") && !endsWith(dir->d_name, ".MCD") && !endsWith(dir->d_name, ".VGS") &&
			!endsWith(dir->d_name, ".VMC") && !endsWith(dir->d_name, ".BIN") && !endsWith(dir->d_name, ".SRM"))
			continue;

		item = _createSaveEntry(SAVE_FLAG_PS1 | SAVE_FLAG_VMC, dir->d_name);
		item->type = FILE_TYPE_VMC;
		item->title_id = strdup("VMC");
		item->dir_name = strdup(PS1_VMC_PATH_USB);
		asprintf(&item->path, "%s%s", vmcPath, dir->d_name);
		list_append(list, item);

		LOG("[%s] F(%X) name '%s'", item->path, item->flags, item->name);
	}

	closedir(d);
}

int ReadVmcCodes(save_entry_t * save)
{
	code_entry_t * cmd;
	char filePath[256];

	save->codes = list_alloc();

	if (save->type == FILE_TYPE_MENU)
	{
		add_vmc_import_saves(save->codes, save->path, PS1_SAVES_PATH_USB);
		add_vmc_import_saves(save->codes, save->path, PS3_SAVES_PATH_USB);
		if (!list_count(save->codes))
		{
			list_free(save->codes);
			save->codes = NULL;
			return 0;
		}

		list_bubbleSort(save->codes, &sortCodeList_Compare);

		return list_count(save->codes);
	}

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_WARN " Delete Save Game", CMD_DELETE_SAVE);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save game to MCS format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Save to Mass Storage", CMD_EXP_VMCSAVE);
	cmd->options[0].id = PS1SAVE_MCS;
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save game to PSV format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Save to Mass Storage", CMD_EXP_VMCSAVE);
	cmd->options[0].id = PS1SAVE_PSV;
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export save game to PSX format", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Save to Mass Storage", CMD_EXP_VMCSAVE);
	cmd->options[0].id = PS1SAVE_AR;
	list_append(save->codes, cmd);

	LOG("Loaded %ld codes", list_count(save->codes));

	return list_count(save->codes);
}

/*
 * Function:		ReadOnlineSaves()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads an entire NCL file into an array of code_entry
 * Arguments:
 *	filename:		File name ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadOnlineSaves(save_entry_t * game)
{
	SceIoStat stat;
	code_entry_t* item;
	option_value_t* optval;
	char path[256];
	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%s.txt", game->title_id);

	if (sceIoGetstat(path, &stat) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
	{
		time_t ftime, ltime;

		sceRtcGetTime_t(&stat.sce_st_mtime, &ftime);
		sceRtcGetCurrentClockLocalTime(&stat.sce_st_atime);
		sceRtcGetTime_t(&stat.sce_st_atime, &ltime);

		LOG("File '%s' is %ld seconds old", path, (ltime - ftime));
		// re-download if file is +1 day old
		if ((int)(ltime - ftime - ONLINE_CACHE_TIMEOUT) > 0 && !http_download(game->path, "saves.txt", path, 1))
			return 0;
	}
	else
	{
		if (!http_download(game->path, "saves.txt", path, 1))
			return 0;
	}

	long fsize;
	char *data = readTextFile(path, &fsize);
	if (!data)
		return 0;
	
	char *ptr = data;
	char *end = data + fsize;

	game->codes = list_alloc();

	while (ptr < end && *ptr)
	{
		const char* content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if (content[12] == '=')
		{
			snprintf(path, sizeof(path), CHAR_ICON_ZIP " %s", content + 13);
			item = _createCmdCode(PATCH_COMMAND, path, CMD_CODE_NULL);
			asprintf(&item->file, "%.12s", content);

			item->options_count = 1;
			item->options = _createOptions(1, "Download to Backup Storage", CMD_DOWNLOAD_USB);
			optval = malloc(sizeof(option_value_t));
			asprintf(&optval->name, "Download to Memory Stick (ms0:/PSP)");
			asprintf(&optval->value, "%c%c", CMD_DOWNLOAD_USB, STORAGE_MS0_PSP);
			list_append(item->options[0].opts, optval);
			list_append(game->codes, item);

			LOG("[%s%s] %s", game->path, item->file, item->name + 1);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	free(data);
	LOG("Loaded %d saves", list_count(game->codes));

	return (list_count(game->codes));
}

list_t * ReadBackupList(const char* userPath)
{
	save_entry_t * item;
	code_entry_t * cmd;
	list_t *list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_ZIP, CHAR_ICON_ZIP " Extract Archives (Zip, 7z)");
	asprintf(&item->path, USB_PATH, USER_STORAGE_DEV);
	item->title_id = strdup(item->path);
	item->type = FILE_TYPE_ZIP;
	list_append(list, item);

	item = _createSaveEntry(SAVE_FLAG_PSP, CHAR_ICON_COPY " Manage Save-game Key Dumper plugin");
	item->path = strdup(MS0_PATH);
	item->type = FILE_TYPE_PRX;
	list_append(list, item);

	item = _createSaveEntry(0, CHAR_ICON_NET " Network Tools");
	asprintf(&item->path, USB_PATH, USER_STORAGE_DEV);
	item->type = FILE_TYPE_NET;
	list_append(list, item);

	item = _createSaveEntry(SAVE_FLAG_PSP, CHAR_ICON_COPY " Decompress .CSO to .ISO");
	asprintf(&item->path, USB_PATH "ISO/", USER_STORAGE_DEV);
	item->title_id = strdup(item->path);
	item->type = FILE_TYPE_CSO;
	list_append(list, item);

	item = _createSaveEntry(SAVE_FLAG_PSP, CHAR_ICON_COPY " Compress .ISO to .CSO");
	asprintf(&item->path, USB_PATH "ISO/", USER_STORAGE_DEV);
	item->title_id = strdup(item->path);
	item->type = FILE_TYPE_ISO;
	list_append(list, item);

	return list;
}

static size_t load_iso_files(save_entry_t * bup, int type)
{
	DIR *d;
	struct dirent *dir;
	code_entry_t * cmd;
	char tmp[256];

	bup->codes = list_alloc();
	LOG("Loading files from '%s'...", bup->path);

	d = opendir(bup->path);
	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			if ((type == FILE_TYPE_ISO && !endsWith(dir->d_name, ".ISO")) || (type == FILE_TYPE_CSO && !endsWith(dir->d_name, ".CSO")))
				continue;

			snprintf(tmp, sizeof(tmp), CHAR_ICON_COPY " Convert %s", dir->d_name);
			cmd = _createCmdCode(PATCH_COMMAND, tmp, (type == FILE_TYPE_ISO) ? CMD_CONV_ISO2CSO : CMD_CONV_CSO2ISO);
			asprintf(&cmd->file, "%s%s", bup->path, dir->d_name);

			LOG("[%s] name '%s'", cmd->file, cmd->name +2);
			list_append(bup->codes, cmd);
		}
		closedir(d);
	}

	if (!list_count(bup->codes))
	{
		list_free(bup->codes);
		bup->codes = NULL;
		return 0;
	}

	LOG("%ld items loaded", list_count(bup->codes));

	return list_count(bup->codes);	
}

int ReadBackupCodes(save_entry_t * bup)
{
	code_entry_t * cmd;
	char tmp[256];

	switch(bup->type)
	{
	case FILE_TYPE_ZIP:
		break;

	case FILE_TYPE_ISO:
	case FILE_TYPE_CSO:
		return load_iso_files(bup, bup->type);

	case FILE_TYPE_NET:
		bup->codes = list_alloc();
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " URL link Downloader (http, https, ftp, ftps)", CMD_URL_DOWNLOAD);
		list_append(bup->codes, cmd);
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Local Web Server (full system access)", CMD_NET_WEBSERVER);
		list_append(bup->codes, cmd);
		return list_count(bup->codes);

	case FILE_TYPE_PRX:
		bup->codes = list_alloc();

		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " Install Save-game Key Dumper plugin", CMD_SETUP_PLUGIN);
		cmd->codes[1] = 1;
		list_append(bup->codes, cmd);
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Disable Save-game Key Dumper plugin", CMD_SETUP_PLUGIN);
		cmd->codes[1] = 0;
		list_append(bup->codes, cmd);

		return list_count(bup->codes);

	default:
		return 0;
	}

	bup->codes = list_alloc();

	LOG("Loading files from '%s'...", bup->path);

	DIR *d;
	struct dirent *dir;
	d = opendir(bup->path);

	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			if (!endsWith(dir->d_name, ".RAR") && !endsWith(dir->d_name, ".ZIP") && !endsWith(dir->d_name, ".7Z"))
				continue;

			snprintf(tmp, sizeof(tmp), CHAR_ICON_ZIP " Extract %s%s", bup->path, dir->d_name);
			cmd = _createCmdCode(PATCH_COMMAND, tmp, CMD_EXTRACT_ARCHIVE);
			asprintf(&cmd->file, "%s%s", bup->path, dir->d_name);

			LOG("[%s] name '%s'", cmd->file, cmd->name +2);
			list_append(bup->codes, cmd);
		}
		closedir(d);
	}

	if (!list_count(bup->codes))
	{
		list_free(bup->codes);
		bup->codes = NULL;
		return 0;
	}

	LOG("%ld items loaded", list_count(bup->codes));

	return list_count(bup->codes);
}

/*
 * Function:		UnloadGameList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Free entire array of game_entry
 * Arguments:
 *	list:			Array of game_entry to free
 *	count:			number of game entries
 * Return:			void
 */
void UnloadGameList(list_t * list)
{
	list_node_t *node, *nc, *no;
	save_entry_t *item;
	code_entry_t *code;
	option_value_t* optval;

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		if (item->name)
		{
			free(item->name);
			item->name = NULL;
		}

		if (item->path)
		{
			free(item->path);
			item->path = NULL;
		}

		if (item->dir_name)
		{
			free(item->dir_name);
			item->dir_name = NULL;
		}

		if (item->title_id)
		{
			free(item->title_id);
			item->title_id = NULL;
		}
		
		if (item->codes)
		{
			for (nc = list_head(item->codes); (code = list_get(nc)); nc = list_next(nc))
			{
				if (code->codes)
				{
					free (code->codes);
					code->codes = NULL;
				}
				if (code->name)
				{
					free (code->name);
					code->name = NULL;
				}
				if (code->options && code->options_count > 0)
				{
					for (int z = 0; z < code->options_count; z++)
					{
						for (no = list_head(code->options[z].opts); (optval = list_get(no)); no = list_next(no))
						{
							if (optval->name)
								free(optval->name);
							if (optval->value)
								free(optval->value);

							free(optval);
						}
						list_free(code->options[z].opts);

						if (code->options[z].line)
							free(code->options[z].line);
					}
					
					free (code->options);
				}

				free(code);
			}
			
			list_free(item->codes);
			item->codes = NULL;
		}

		free(item);
	}

	list_free(list);
	
	LOG("UnloadGameList() :: Successfully unloaded game list");
}

int sortCodeList_Compare(const void* a, const void* b)
{
	return strcasecmp(((code_entry_t*) a)->name, ((code_entry_t*) b)->name);
}

/*
 * Function:		qsortSaveList_Compare()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Compares two game_entry for QuickSort
 * Arguments:
 *	a:				First code
 *	b:				Second code
 * Return:			1 if greater, -1 if less, or 0 if equal
 */
int sortSaveList_Compare(const void* a, const void* b)
{
	uint8_t* ta = ((save_entry_t*) a)->name;
	uint8_t* tb = ((save_entry_t*) b)->name;

	for (int t = 0; *ta && *tb; ta++, tb++)
	{
		t = tolower(*ta) - tolower(*tb);
		if (t < 0)
			return -1;
		else if (t > 0)
			return 1;
	}

	return 0;
}

static int parseTypeFlags(int flags)
{
	if (flags & SAVE_FLAG_VMC)
		return FILE_TYPE_VMC;
	else if (flags & SAVE_FLAG_PS1)
		return FILE_TYPE_PS1;
	else if (flags & SAVE_FLAG_PSP)
		return FILE_TYPE_PSP;

	return 0;
}

int sortSaveList_Compare_Type(const void* a, const void* b)
{
	int ta = parseTypeFlags(((save_entry_t*) a)->flags);
	int tb = parseTypeFlags(((save_entry_t*) b)->flags);

	if (ta == tb)
		return sortSaveList_Compare(a, b);
	else if (ta < tb)
		return -1;
	else
		return 1;
}

int sortSaveList_Compare_TitleID(const void* a, const void* b)
{
	char* ta = ((save_entry_t*) a)->title_id;
	char* tb = ((save_entry_t*) b)->title_id;

	if (!ta)
		return (-1);

	if (!tb)
		return (1);

	int ret = strcasecmp(ta, tb);

	return (ret ? ret : sortSaveList_Compare(a, b));
}

static void read_psp_savegames(const char* userPath, list_t *list, int flags)
{
	DIR *d;
	struct dirent *dir;
	save_entry_t *item;
	char sfoPath[256];

	d = opendir(userPath);
	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(sfoPath, sizeof(sfoPath), "%s%s/PARAM.SFO", userPath, dir->d_name);

		LOG("Reading %s...", sfoPath);
		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			continue;
		}

		snprintf(sfoPath, sizeof(sfoPath), "%s%s/SCEVMC0.VMP", userPath, dir->d_name);
		if ((strcmp((char*) sfo_get_param_value(sfo, "SAVEDATA_FILE_LIST"), "CONFIG.BIN") == 0) &&
			(file_exists(sfoPath) == SUCCESS))
		{
			item = _createSaveEntry(SAVE_FLAG_PS1 | flags, sfo_get_param_value(sfo, "TITLE"));
			item->type = FILE_TYPE_PSP;
			item->dir_name = strdup((char*) sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY"));
			asprintf(&item->title_id, "%.9s", item->dir_name);
			asprintf(&item->path, "%s%s/", userPath, dir->d_name);
			list_append(list, item);

			snprintf(sfoPath, sizeof(sfoPath), "%s (MemCard)", sfo_get_param_value(sfo, "TITLE"));
			item = _createSaveEntry(SAVE_FLAG_PS1 | SAVE_FLAG_VMC | flags, sfoPath);
			item->type = FILE_TYPE_VMC;
			item->dir_name = strdup((char*) sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY"));
			item->title_id = strdup("VMC 0");
			asprintf(&item->path, "%s%s/SCEVMC0.VMP", userPath, dir->d_name);
			list_append(list, item);

			item = _createSaveEntry(SAVE_FLAG_PS1 | SAVE_FLAG_VMC | flags, sfoPath);
			item->type = FILE_TYPE_VMC;
			item->dir_name = strdup((char*) sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY"));
			item->title_id = strdup("VMC 1");
			asprintf(&item->path, "%s%s/SCEVMC1.VMP", userPath, dir->d_name);
		}
		else
		{
			item = _createSaveEntry(SAVE_FLAG_PSP | flags, (char*) sfo_get_param_value(sfo, "TITLE"));
			item->type = FILE_TYPE_PSP;
			item->dir_name = strdup((char*) sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY"));
			asprintf(&item->title_id, "%.9s", item->dir_name);
			asprintf(&item->path, "%s%s/", userPath, dir->d_name);
		}

		sfo_free(sfo);
		LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	closedir(d);
}

/*
 * Function:		ReadUserList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads the entire userlist folder into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
list_t * ReadUsbList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;
	char name[256];

	if (dir_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PSP, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	asprintf(&item->path, "%s%s", userPath, PSP_SAVES_PATH_USB);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	snprintf(name, sizeof(name), CHAR_ICON_COPY " Copy selected Saves to Memory Stick (%s:/PSP)", USER_STORAGE_DEV);
	cmd = _createCmdCode(PATCH_COMMAND, name, CMD_COPY_SAVES_HDD);
	list_append(item->codes, cmd);

	snprintf(name, sizeof(name), CHAR_ICON_COPY " Copy All Saves to Memory Stick (%s:/PSP)", USER_STORAGE_DEV);
	cmd = _createCmdCode(PATCH_COMMAND, name, CMD_COPY_ALL_SAVES_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Resign selected Saves", CMD_RESIGN_SAVES);
	list_append(item->codes, cmd);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Resign All Saves", CMD_RESIGN_ALL_SAVES);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Start local Web Server", CMD_SAVE_WEBSERVER);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Export all Save-game Keys", CMD_DUMP_FINGERPRINTS);
	list_append(item->codes, cmd);
	list_append(list, item);

	read_psp_savegames(item->path, list, 0);
	read_vmc_files(userPath, list);

	return list;
}

list_t * ReadUserList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;

	if (dir_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PSP, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy selected Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Saves to Backup Storage", CMD_COPY_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy all Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Saves to Backup Storage", CMD_COPY_ALL_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Resign selected Saves", CMD_RESIGN_SAVES);
	list_append(item->codes, cmd);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Resign All Saves", CMD_RESIGN_ALL_SAVES);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Start local Web Server", CMD_SAVE_WEBSERVER);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Export all Save-game Keys", CMD_DUMP_FINGERPRINTS);
	list_append(item->codes, cmd);
	list_append(list, item);

	read_psp_savegames(userPath, list, SAVE_FLAG_HDD);

	return list;
}

/*
 * Function:		ReadOnlineList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads the entire gamelist file into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
static void _ReadOnlineListEx(const char* urlPath, uint16_t flag, list_t *list)
{
	SceIoStat stat;
	save_entry_t *item;
	char path[256];

	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%04X_games.txt", flag);

	if (sceIoGetstat(path, &stat) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
	{
		time_t ftime, ltime;

		sceRtcGetTime_t(&stat.sce_st_mtime, &ftime);
		sceRtcGetCurrentClockLocalTime(&stat.sce_st_atime);
		sceRtcGetTime_t(&stat.sce_st_atime, &ltime);

		LOG("File '%s' is %ld seconds old", path, (ltime - ftime));
		// re-download if file is +1 day old
		if ((int)(ltime - ftime - ONLINE_CACHE_TIMEOUT) > 0 && !http_download(urlPath, "games.txt", path, 0))
			return;
	}
	else
	{
		if (!http_download(urlPath, "games.txt", path, 0))
			return;
	}
	
	long fsize;
	char *data = readTextFile(path, &fsize);
	if (!data)
		return;
	
	char *ptr = data;
	char *end = data + fsize;

	while (ptr < end && *ptr)
	{
		char *tmp, *content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if ((tmp = strchr(content, '=')) != NULL)
		{
			*tmp++ = 0;
			item = _createSaveEntry(flag | SAVE_FLAG_ONLINE, tmp);
			item->title_id = strdup(content);
			asprintf(&item->path, "%s%s/", urlPath, item->title_id);

			LOG("+ [%s] %s", item->title_id, item->name);
			list_append(list, item);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);
}

list_t * ReadOnlineList(const char* urlPath)
{
	char url[256];
	list_t *list = list_alloc();

	// PS1 save-games (Zip PSV)
	snprintf(url, sizeof(url), "%s" "PS1/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PS1, list);

	// PSP save-games (Zip folder)
	snprintf(url, sizeof(url), "%sPSP/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PSP, list);

	if (!list_count(list))
	{
		list_free(list);
		return NULL;
	}

	return list;
}

list_t * ReadVmcList(const char* userPath)
{
	char filePath[256];
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;
	ps1mcData_t* mcdata;

	if (!openMemoryCard(userPath, 0))
	{
		LOG("Error: no PS1 Memory Card detected! (%s)", userPath);
		return NULL;
	}

	mcdata = getMemoryCardData();
	if (!mcdata)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PS1, CHAR_ICON_VMC " Memory Card Management");
	item->type = FILE_TYPE_MENU;
	item->path = strdup(userPath);
	item->codes = list_alloc();
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	strncpy(filePath, userPath, sizeof(filePath));
	strrchr(filePath, '/')[0] = 0;
	item->title_id = strdup(strrchr(filePath, '/')+1);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export selected Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Saves to Backup Storage", CMD_EXP_SAVES_VMC);
	list_append(item->codes, cmd);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export all Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(1, "Copy Saves to Backup Storage", CMD_EXP_ALL_SAVES_VMC);
	list_append(item->codes, cmd);
	add_vmp_commands(item);
	list_append(list, item);

	item = _createSaveEntry(SAVE_FLAG_PS1, CHAR_ICON_COPY " Import Saves to Virtual Card");
	asprintf(&item->path, "%s:/", USER_STORAGE_DEV);
	asprintf(&item->title_id, " %s", item->path);
	item->dir_name = strdup(userPath);
	item->type = FILE_TYPE_MENU;
	list_append(list, item);

	for (int i = 0; i < PS1CARD_MAX_SLOTS; i++)
	{
		if (mcdata[i].saveType != PS1BLOCK_INITIAL)
			continue;

		LOG("Reading '%s'...", mcdata[i].saveName);

		char* tmp = sjis2utf8(mcdata[i].saveTitle);
		item = _createSaveEntry(SAVE_FLAG_PS1 | SAVE_FLAG_VMC, tmp);
		item->blocks = i;
		item->type = FILE_TYPE_PS1;
		item->dir_name = strdup(mcdata[i].saveName);
		item->title_id = strdup(mcdata[i].saveProdCode);
		asprintf(&item->path, "%s\n%s/", userPath, mcdata[i].saveName);
		free(tmp);

		LOG("[%s] F(%X) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	return list;
}

int get_save_details(const save_entry_t* save, char **details)
{
	char sfoPath[256];

	if (save->type == FILE_TYPE_PS1)
	{
		asprintf(details, "%s\n----- PS1 Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name);
		return 1;
	}

	if (save->type == FILE_TYPE_VMC)
	{
		asprintf(details, "%s\n----- Virtual Memory Card -----\n"
			"Game: %s\n"
			"Type: %s\n"
			"Folder: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name);
		return 1;
	}

	if (save->flags & SAVE_FLAG_ONLINE)
	{
		asprintf(details, "%s\n----- Online Database -----\n"
			"Game: %s\n"
			"Title ID: %s\n",
			save->path,
			save->name,
			save->title_id);
		return 1;
	}

	if (save->flags & SAVE_FLAG_PSP || save->flags & SAVE_FLAG_PS1)
	{
		snprintf(sfoPath, sizeof(sfoPath), "%sPARAM.SFO", save->path);
		LOG("Save Details :: Reading %s...", sfoPath);

		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			return 0;
		}

		asprintf(details, "%s\n----- PSP Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n"
			"Title: %s\n"
			"Details: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name,
			(char*)sfo_get_param_value(sfo, "SAVEDATA_TITLE"),
			(char*)sfo_get_param_value(sfo, "SAVEDATA_DETAIL"));

		sfo_free(sfo);
		return 1;
	}

	asprintf(details, "%s\n\nTitle: %s\n", save->path, save->name);

	return 1;
}
