#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <polarssl/md5.h>
#include <pspnet_apctl.h>
#include <psputility.h>
#include <pspwlan.h>
#include <psprtc.h>
#include <pspiofilemgr.h>

#include "saves.h"
#include "menu.h"
#include "common.h"
#include "utils.h"
#include "sfo.h"
#include "ps1card.h"

static char host_buf[256];

static int _set_dest_path(char* path, int dest, const char* folder)
{
	switch (dest)
	{
	case STORAGE_EF0:
		sprintf(path, "%s%s", EF0_PATH, folder);
		break;

	case STORAGE_MS0:
		sprintf(path, "%s%s", MS0_PATH, folder);
		break;

	default:
		path[0] = 0;
		return 0;
	}

	return 1;
}

static void downloadSave(const save_entry_t* entry, const char* file, int dst)
{
	char path[256];

	_set_dest_path(path, dst, (entry->flags & SAVE_FLAG_PS1) ? PS3_SAVES_PATH_USB : PSP_SAVES_PATH_USB);
	if (dst == STORAGE_MS0_PSP)
		snprintf(path, sizeof(path), PSP_SAVES_PATH_HDD, menu_options[3].options[STORAGE_MS0]);

	if (mkdirs(path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", path);
		return;
	}

	if (!http_download(entry->path, file, APOLLO_LOCAL_CACHE "tmpsave.zip", 1))
	{
		show_message("Error downloading save game from:\n%s%s", entry->path, file);
		return;
	}

	if (extract_zip(APOLLO_LOCAL_CACHE "tmpsave.zip", path))
		show_message("Save game successfully downloaded to:\n%s", path);
	else
		show_message("Error extracting save game!");

	unlink_secure(APOLLO_LOCAL_CACHE "tmpsave.zip");
}

static uint32_t get_filename_id(const char* dir, const char* title_id)
{
	char path[128];
	uint32_t tid = 0;

	do
	{
		tid++;
		snprintf(path, sizeof(path), "%s%s-%08d.zip", dir, title_id, tid);
	}
	while (file_exists(path) == SUCCESS);

	return tid;
}

static void zipSave(const save_entry_t* entry, int dst)
{
	char exp_path[256];
	char export_file[256];
	char* tmp;
	uint32_t fid;
	int ret;

	_set_dest_path(exp_path, dst, EXPORT_PATH);
	if (mkdirs(exp_path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", exp_path);
		return;
	}

	init_loading_screen("Exporting save game...");

	fid = get_filename_id(exp_path, entry->title_id);
	snprintf(export_file, sizeof(export_file), "%s%s-%08d.zip", exp_path, entry->title_id, fid);

	tmp = strdup(entry->path);
	*strrchr(tmp, '/') = 0;
	*strrchr(tmp, '/') = 0;

	ret = zip_directory(tmp, entry->path, export_file);
	free(tmp);

	if (ret)
	{
		snprintf(export_file, sizeof(export_file), "%s%016" PRIx64 "%016" PRIx64 ".txt", exp_path, ES64(apollo_config.psid[0]), ES64(apollo_config.psid[1]));
		FILE* f = fopen(export_file, "a");
		if (f)
		{
			fprintf(f, "%s-%08d.zip=%s\n", entry->title_id, fid, entry->name);
			fclose(f);
		}
	}

	stop_loading_screen();
	if (!ret)
	{
		show_message("Error! Can't export save game to:\n%s", exp_path);
		return;
	}

	show_message("Zip file successfully saved to:\n%s%s-%08d.zip", exp_path, entry->title_id, fid);
}

static void copySave(const save_entry_t* save, int dev)
{
	char* copy_path;
	char exp_path[256];

	_set_dest_path(exp_path, dev, PSP_SAVES_PATH_USB);
	if (strncmp(save->path, exp_path, strlen(exp_path)) == 0)
	{
		show_message("Copy operation cancelled!\nSame source and destination.");
		return;
	}

	if (mkdirs(exp_path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", exp_path);
		return;
	}

	init_loading_screen("Copying files...");

	asprintf(&copy_path, "%s%s/", exp_path, save->dir_name);

	LOG("Copying <%s> to %s...", save->path, copy_path);
	copy_directory(save->path, save->path, copy_path);

	free(copy_path);

	stop_loading_screen();
	show_message("Files successfully copied to:\n%s", exp_path);
}

static int get_psp_save_key(const save_entry_t* entry, uint8_t* key)
{
	FILE* fp;
	char path[256];

	snprintf(path, sizeof(path), "ms0:/PSP/SAVEPLAIN/%s/%s.bin", entry->dir_name, entry->title_id);
	if (read_psp_game_key(path, key))
		return 1;

	snprintf(path, sizeof(path), "ms0:/PSP/SAVEPLAIN/%s/%s.bin", entry->title_id, entry->title_id);
	if (read_psp_game_key(path, key))
		return 1;

	// SGKeyDumper 1.5+ support
	snprintf(path, sizeof(path), "ms0:/PSP/GAME/SED/gamekey/%s.bin", entry->title_id);
	if (read_psp_game_key(path, key))
		return 1;

	// SGKeyDumper 1.6+ support
	snprintf(path, sizeof(path), "ef0:/PSP/GAME/SED/gamekey/%s.bin", entry->title_id);
	if (read_psp_game_key(path, key))
		return 1;

	snprintf(path, sizeof(path), APOLLO_DATA_PATH "gamekeys.txt");
	if ((fp = fopen(path, "r")) == NULL)
		return 0;

	while(fgets(path, sizeof(path), fp))
	{
		char *ptr = strchr(path, '=');

		if (!ptr || path[0] == ';')
			continue;

		*ptr++ = 0;
		ptr[32] = 0;
		if (strncasecmp(entry->dir_name, path, strlen(path)) == 0)
		{
			LOG("[DB] %s Key found: %s", path, ptr);
			ptr = x_to_u8_buffer(ptr);
			memcpy(key, ptr, 16);
			free(ptr);

			return 1;
		}
	}
	fclose(fp);

	return 0;
}

static int _copy_save_psp(const save_entry_t* save)
{
	char copy_path[256];

	snprintf(copy_path, sizeof(copy_path), PSP_SAVES_PATH_HDD "%s/", USER_STORAGE_DEV, save->dir_name);

	LOG("Copying <%s> to %s...", save->path, copy_path);
	return (copy_directory(save->path, save->path, copy_path) == SUCCESS);
}

static void copySaveHDD(const save_entry_t* save)
{
	//source save is already on HDD
	if (save->flags & SAVE_FLAG_HDD)
	{
		show_message("Copy operation cancelled!\nSame source and destination.");
		return;
	}

	init_loading_screen("Copying save game...");
	int ret = _copy_save_psp(save);
	stop_loading_screen();

	if (ret)
		show_message("Save-game successfully copied to:\n" PSP_SAVES_PATH_HDD "%s",
			USER_STORAGE_DEV, save->dir_name);
	else
		show_message("Error! Can't copy Save-game folder:\n" PSP_SAVES_PATH_HDD "%s",
			USER_STORAGE_DEV, save->dir_name);
}

static void copyAllSavesHDD(const save_entry_t* save, int all)
{
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Copying all saves...");

	LOG("Copying all saves from '%s' to HDD...", save->path);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_PSP || !(all || item->flags & SAVE_FLAG_SELECTED))
			continue;

		if ((item->flags & SAVE_FLAG_PS1) || item->type == FILE_TYPE_PSP)
			(_copy_save_psp(item) ? done++ : err_count++);
	}

	end_progress_bar();

	show_message("%d/%d Saves copied to Memory Stick\n" PSP_SAVES_PATH_HDD,
		done, done+err_count, USER_STORAGE_DEV);
}

static void extractArchive(const char* file_path)
{
	int ret = 0;
	char exp_path[256];

	strncpy(exp_path, file_path, sizeof(exp_path));
	*strrchr(exp_path, '.') = 0;

	switch (strrchr(file_path, '.')[1])
	{
	case 'z':
	case 'Z':
		/* ZIP */
		strcat(exp_path, "/");
		ret = extract_zip(file_path, exp_path);
		break;

	case 'r':
	case 'R':
		/* RAR */
//		ret = extract_rar(file_path, exp_path);
		break;

	case '7':
		/* 7-Zip */
		ret = extract_7zip(file_path, exp_path);
		break;

	default:
		break;
	}

	if (ret)
		show_message("All files extracted to:\n%s", exp_path);
	else
		show_message("Error: %s couldn't be extracted", file_path);
}

static int pspDumpKey(const save_entry_t* save, int verbose)
{
	char fpath[256];
	uint8_t buffer[0x10];

	if (!get_psp_save_key(save, buffer))
	{
		if (verbose) show_message("Error! Game Key file is not available:\n%s/%s.bin", save->dir_name, save->title_id);
		return 0;
	}

	snprintf(fpath, sizeof(fpath), APOLLO_PATH "gamekeys.txt", USER_STORAGE_DEV);
	FILE *fp = fopen(fpath, "a");
	if (!fp)
	{
		if (verbose) show_message("Error! Can't open file:\n%s", fpath);
		return 0;
	}

	fprintf(fp, "%s=", save->dir_name);
	for (size_t i = 0; i < sizeof(buffer); i++)
		fprintf(fp, "%02X", buffer[i]);

	fprintf(fp, "\n");
	fclose(fp);

	if (verbose) show_message("%s game key successfully saved to:\n%s", save->title_id, fpath);
	return 1;
}

static void pspExportKey(const save_entry_t* save)
{
	char fpath[256];
	uint8_t buffer[0x10];

	if (!get_psp_save_key(save, buffer))
	{
		show_message("Error! Game Key file is not available:\n%s/%s.bin", save->dir_name, save->title_id);
		return;
	}

	snprintf(fpath, sizeof(fpath), APOLLO_USER_PATH "%s/%s.bin", USER_STORAGE_DEV, save->dir_name, save->title_id);
	mkdirs(fpath);

	if (write_buffer(fpath, buffer, sizeof(buffer)) == SUCCESS)
		show_message("%s game key successfully saved to:\n%s", save->title_id, fpath);
	else
		show_message("Error! Can't save file:\n%s", fpath);
}

static void dumpAllFingerprints(const save_entry_t* save)
{
	int count = 0, err = 0;
	uint64_t progress = 0;
	list_node_t *node;
	save_entry_t *item;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Dumping all game keys...");

	LOG("Dumping all fingerprints from '%s'...", save->path);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_PSP)
			continue;

		pspDumpKey(item, 0) ? count++ : err++;
	}

	end_progress_bar();
	show_message("%d/%d game keys dumped to:\n" APOLLO_PATH "gamekeys.txt", count, count+err, USER_STORAGE_DEV);
}

static int webReqHandler(dWebRequest_t* req, dWebResponse_t* res, void* list)
{
	list_node_t *node;
	save_entry_t *item;

	// http://ps3-ip:8080/
	if (strcmp(req->resource, "/") == 0)
	{
		uint64_t hash[2];
		md5_context ctx;

		md5_starts(&ctx);
		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
			md5_update(&ctx, (uint8_t*) item->name, strlen(item->name));

		md5_finish(&ctx, (uint8_t*) hash);
		asprintf(&res->data, APOLLO_LOCAL_CACHE "web%016llx%016llx.html", hash[0], hash[1]);

		if (file_exists(res->data) == SUCCESS)
			return 1;

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		fprintf(f, "<html><head><meta charset=\"UTF-8\"><style>h1, h2 { font-family: arial; } img { display: none; } table { border-collapse: collapse; margin: 25px 0; font-size: 0.9em; font-family: sans-serif; min-width: 400px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.15); } table thead tr { background-color: #009879; color: #ffffff; text-align: left; } table th, td { padding: 12px 15px; } table tbody tr { border-bottom: 1px solid #dddddd; } table tbody tr:nth-of-type(even) { background-color: #f3f3f3; } table tbody tr:last-of-type { border-bottom: 2px solid #009879; }</style>");
		fprintf(f, "<script language=\"javascript\">function show(sid,src){var im=document.getElementById('img'+sid);im.src=src;im.style.display='block';document.getElementById('btn'+sid).style.display='none';}</script>");
		fprintf(f, "<title>Apollo Save Tool</title></head><body><h1>.:: Apollo Save Tool</h1><h2>Index of %s</h2><table><thead><tr><th>Name</th><th>Icon</th><th>Title ID</th><th>Folder</th><th>Location</th></tr></thead><tbody>", selected_entry->path);

		int i = 0;
		for (node = list_head(list); (item = list_get(node)); node = list_next(node), i++)
		{
			if (item->type != FILE_TYPE_PSP || !(item->flags & (SAVE_FLAG_PS1|SAVE_FLAG_PSP)))
				continue;

			fprintf(f, "<tr><td><a href=\"/zip/%08d/%s_%s.zip\">%s</a></td>", i, item->title_id, item->dir_name, item->name);
			fprintf(f, "<td><button type=\"button\" id=\"btn%d\" onclick=\"show(%d,'/icon/%08x/ICON0.PNG", i, i, i);
			fprintf(f, "')\">Show Icon</button><img id=\"img%d\" alt=\"%s\" height=\"80\"></td>", i, item->name);
			fprintf(f, "<td>%s</td>", item->title_id);
			fprintf(f, "<td>%s</td>", item->dir_name);
			fprintf(f, "<td>%.4s</td></tr>", item->path);
		}

		fprintf(f, "</tbody></table></body></html>");
		fclose(f);
		return 1;
	}

	// http://psp-ip:8080/PSP/games.txt
	if (wildcard_match(req->resource, "/PSP/games.txt"))
	{
		asprintf(&res->data, "%s%s", APOLLO_LOCAL_CACHE, "PSP_games.txt");

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_PSP))
				continue;

			fprintf(f, "%s=%s\n", item->title_id, item->name);
		}

		fclose(f);
		return 1;
	}

	// http://psp-ip:8080/PSP/BLUS12345/saves.txt
	if (wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/saves.txt"))
	{
		asprintf(&res->data, "%sweb%.9s_saves.txt", APOLLO_LOCAL_CACHE, req->resource + 5);

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		int i = 0;
		for (node = list_head(list); (item = list_get(node)); node = list_next(node), i++)
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_PSP) || strncmp(item->title_id, req->resource + 5, 9))
				continue;

			fprintf(f, "%08d.zip=(%s) %s\n", i, item->dir_name, item->name);
		}

		fclose(f);
		return 1;
	}

	// http://ps3-ip:8080/zip/00000000/CUSA12345_DIR-NAME.zip
	// http://psp-ip:8080/PSP/BLUS12345/00000000.zip
	if (wildcard_match(req->resource, "/zip/\?\?\?\?\?\?\?\?/\?\?\?\?\?\?\?\?\?_*.zip") ||
		wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/*.zip"))
	{
		char *base, *path;
		int id = 0;

		sscanf(req->resource + (strncmp(req->resource, "/PSP", 4) == 0 ? 15 : 5), "%08d", &id);
		item = list_get_item(list, id);
		asprintf(&res->data, "%s%s_%s.zip", APOLLO_LOCAL_CACHE, item->title_id, item->dir_name);

		base = strdup(item->path);
		path = strdup(item->path);
		*strrchr(base, '/') = 0;
		*strrchr(base, '/') = 0;

		id = zip_directory(base, path, res->data);

		free(base);
		free(path);
		return id;
	}

	// http://psp-ip:8080/PSP/BLUS12345/ICON0.PNG
	if (wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/ICON0.PNG"))
	{
		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_PSP) || strncmp(item->title_id, req->resource + 5, 9))
				continue;

			asprintf(&res->data, "%sICON0.PNG", item->path);
			return (file_exists(res->data) == SUCCESS);
		}

		return 0;
	}

	// http://vita-ip:8080/icon/00000000/ICON0.PNG
	if (wildcard_match(req->resource, "/icon/\?\?\?\?\?\?\?\?/ICON0.PNG"))
	{
		int id = 0;

		sscanf(req->resource + 6, "%08x", &id);
		item = list_get_item(list, id);
		asprintf(&res->data, "%sICON0.PNG", item->path);

		return (file_exists(res->data) == SUCCESS);
	}

	return 0;
}

static void enableWebServer(dWebReqHandler_t handler, void* data, int port)
{
	union SceNetApctlInfo ip_info;

	if (!network_up())
		return;

	memset(&ip_info, 0, sizeof(ip_info));
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &ip_info);
	LOG("Starting local web server %s:%d ...", ip_info.ip, port);

	if (dbg_webserver_start(port, handler, data))
	{
		show_message("Web Server on http://%s:%d\nPress OK to stop the Server.", ip_info.ip, port);
		dbg_webserver_stop();
	}
	else show_message("Error starting Web Server!");
}

static void copyAllSavesUSB(const save_entry_t* save, int dev, int all)
{
	char dst_path[256];
	char copy_path[256];
	char save_path[256];
	int done = 0, err_count = 0;
	uint64_t progress = 0;
	list_node_t *node;
	save_entry_t *item;
	list_t *list = ((void**)save->dir_name)[0];

	_set_dest_path(dst_path, dev, PSP_SAVES_PATH_USB);
	if (!list || mkdirs(dst_path) != SUCCESS)
	{
		show_message("Error! Folder is not available:\n%s", dst_path);
		return;
	}

	init_progress_bar("Copying all saves...");

	LOG("Copying all saves to '%s'...", dst_path);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_PSP || !(all || item->flags & SAVE_FLAG_SELECTED))
			continue;

		snprintf(copy_path, sizeof(copy_path), "%s%s/", dst_path, item->dir_name);
		LOG("Copying <%s> to %s...", item->path, copy_path);

		if ((item->flags & SAVE_FLAG_PS1) || item->type == FILE_TYPE_PSP)
			(copy_directory(item->path, item->path, copy_path) == SUCCESS) ? done++ : err_count++;
	}

	end_progress_bar();
	show_message("%d/%d Saves copied to:\n%s", done, done+err_count, dst_path);
}

static void exportAllSavesVMC(const save_entry_t* save, int dev, int all)
{
	char outPath[256];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Exporting all VMC saves...");
	_set_dest_path(outPath, dev, PS3_SAVES_PATH_USB);
	mkdirs(outPath);

	LOG("Exporting all saves from '%s' to %s...", save->path, outPath);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

		if (item->type == FILE_TYPE_PS1)
			(saveSingleSave(outPath, item->blocks, PS1SAVE_PSV) ? done++ : err_count++);
	}

	end_progress_bar();

	show_message("%d/%d Saves exported to\n%s", done, done+err_count, outPath);
}

static void exportVmcSave(const save_entry_t* save, int type, int dst_id)
{
	int ret = 0;
	char outPath[256];
	ScePspDateTime t;

	_set_dest_path(outPath, dst_id, (type == PS1SAVE_PSV) ? PS3_SAVES_PATH_USB : PS1_SAVES_PATH_USB);
	mkdirs(outPath);
	if (type != PS1SAVE_PSV)
	{
		// build file path
		sceRtcGetCurrentClockLocalTime(&t);
		sprintf(strrchr(outPath, '/'), "/%s_%d-%02d-%02d_%02d%02d%02d.%s", save->title_id,
			t.year, t.month, t.day, t.hour, t.minute, t.second,
			(type == PS1SAVE_MCS) ? "mcs" : "psx");
	}

	if (saveSingleSave(outPath, save->blocks, type))
		show_message("Save successfully exported to:\n%s", outPath);
	else
		show_message("Error exporting save:\n%s", save->path);
}

static int deleteSave(const save_entry_t* save)
{
	int ret = 0;

	if (!show_dialog(DIALOG_TYPE_YESNO, "Do you want to delete %s?", save->dir_name))
		return 0;

	if (save->type == FILE_TYPE_PSP)
	{
		char tmp[256];
		strncpy(tmp, save->path, sizeof(tmp));
		strrchr(tmp, '/')[0] = 0;

		clean_directory(save->path);
		ret = (sceIoRmdir(tmp) == SUCCESS);
	}
	else if (save->flags & SAVE_FLAG_PS1)
		ret = formatSave(save->blocks);

	if (ret)
		show_message("Save successfully deleted:\n%s", save->dir_name);
	else
		show_message("Error! Couldn't delete save:\n%s", save->dir_name);

	return ret;
}

/*
static int apply_sfo_patches(save_entry_t* entry, sfo_patch_t* patch)
{
    code_entry_t* code;
    char in_file_path[256];
    char tmp_dir[SFO_DIRECTORY_SIZE];
    u8 tmp_psid[SFO_PSID_SIZE];
    list_node_t* node;

    if (entry->flags & SAVE_FLAG_PSP)
        return 1;

    for (node = list_head(entry->codes); (code = list_get(node)); node = list_next(node))
    {
        if (!code->activated || code->type != PATCH_SFO)
            continue;

        LOG("Active: [%s]", code->name);

        switch (code->codes[0])
        {
        case SFO_CHANGE_ACCOUNT_ID:
//            if (entry->flags & SAVE_FLAG_OWNER)
//                entry->flags ^= SAVE_FLAG_OWNER;

            sscanf(code->options->value[code->options->sel], "%lx", &patch->account_id);
            break;

        case SFO_REMOVE_PSID:
            bzero(tmp_psid, SFO_PSID_SIZE);
            patch->psid = tmp_psid;
            break;

        case SFO_CHANGE_TITLE_ID:
            patch->directory = strstr(entry->path, entry->title_id);
            snprintf(in_file_path, sizeof(in_file_path), "%s", entry->path);
            strncpy(tmp_dir, patch->directory, SFO_DIRECTORY_SIZE);

            strncpy(entry->title_id, code->options[0].name[code->options[0].sel], 9);
            strncpy(patch->directory, entry->title_id, 9);
            strncpy(tmp_dir, entry->title_id, 9);
            *strrchr(tmp_dir, '/') = 0;
            patch->directory = tmp_dir;

            LOG("Moving (%s) -> (%s)", in_file_path, entry->path);
            rename(in_file_path, entry->path);
            break;

        default:
            break;
        }

        code->activated = 0;
    }

	snprintf(in_file_path, sizeof(in_file_path), "%s" "sce_sys/param.sfo", selected_entry->path);
	LOG("Applying SFO patches '%s'...", in_file_path);

	return (patch_sfo(in_file_path, patch) == SUCCESS);
}
*/
static int psp_is_decrypted(list_t* list, const char* fname)
{
	list_node_t *node;

	for (node = list_head(list); node; node = list_next(node))
		if (strcmp(list_get(node), fname) == 0)
			return 1;

	return 0;
}

static void* psp_host_callback(int id, int* size)
{
	memset(host_buf, 0, sizeof(host_buf));

	switch (id)
	{
	case APOLLO_HOST_TEMP_PATH:
		return APOLLO_LOCAL_CACHE;

	case APOLLO_HOST_USERNAME:
		return "APOLLO-PSP";

	case APOLLO_HOST_PSID:
		memcpy(host_buf, apollo_config.psid, 16);
		if (size) *size = 16;
		return host_buf;

	case APOLLO_HOST_SYS_NAME:
		if (sceUtilityGetSystemParamString(PSP_SYSTEMPARAM_ID_STRING_NICKNAME, host_buf, sizeof(host_buf)) < 0)
			LOG("Error getting system Nickname");

		if (size) *size = strlen(host_buf);
		return host_buf;

	case APOLLO_HOST_LAN_ADDR:
	case APOLLO_HOST_WLAN_ADDR:
		if (sceWlanGetEtherAddr(host_buf) < 0)
			LOG("Error getting Wlan Ethernet Address");

		if (size) *size = 6;
		return host_buf;
	}

	if (size) *size = 1;
	return host_buf;
}

static int apply_cheat_patches(const save_entry_t* entry)
{
	int ret = 1;
	char tmpfile[256];
	char* filename;
	code_entry_t* code;
	list_node_t* node;
	list_t* decrypted_files = list_alloc();
	uint8_t key[16];

	init_loading_screen("Applying changes...");

	for (node = list_head(entry->codes); (code = list_get(node)); node = list_next(node))
	{
		if (!code->activated || (code->type != PATCH_GAMEGENIE && code->type != PATCH_BSD))
			continue;

		LOG("Active code: [%s]", code->name);

		if (strrchr(code->file, '\\'))
			filename = strrchr(code->file, '\\')+1;
		else
			filename = code->file;

		if (strchr(filename, '*'))
		{
			option_value_t* optval = list_get_item(code->options[0].opts, code->options[0].sel);
			filename = optval->name;
		}

		if (strstr(code->file, "~extracted\\"))
			snprintf(tmpfile, sizeof(tmpfile), "%s[%s]%s", APOLLO_LOCAL_CACHE, entry->title_id, filename);
		else
		{
			snprintf(tmpfile, sizeof(tmpfile), "%s%s", entry->path, filename);
			LOG("Decrypting file '%s'", tmpfile);

			if (entry->flags & SAVE_FLAG_PSP && !psp_is_decrypted(decrypted_files, filename))
			{
				if (get_psp_save_key(entry, key) && psp_DecryptSavedata(entry->path, tmpfile, key))
				{
					LOG("Decrypted PSP file '%s'", filename);
					list_append(decrypted_files, strdup(filename));
				}
				else
				{
					LOG("Error: failed to decrypt (%s)", filename);
					ret = 0;
					continue;
				}
			}
		}

		if (!apply_cheat_patch_code(tmpfile, entry->title_id, code, &psp_host_callback))
		{
			LOG("Error: failed to apply (%s)", code->name);
			ret = 0;
		}

		code->activated = 0;
	}

	for (node = list_head(decrypted_files); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Encrypting '%s'...", filename);
		if (!get_psp_save_key(entry, key) || !psp_EncryptSavedata(entry->path, filename, key))
		{
			LOG("Error: failed to encrypt (%s)", filename);
			ret = 0;
		}

		free(filename);
	}

	list_free(decrypted_files);
	free_patch_var_list();
	stop_loading_screen();

	return ret;
}

static void resignSave(save_entry_t* entry)
{
    sfo_patch_t patch = {
        .flags = 0,
        .psid = (uint8_t*) apollo_config.psid,
        .directory = NULL,
    };

    LOG("Applying cheats to '%s'...", entry->name);
    if (!apply_cheat_patches(entry))
        show_message("Error! Cheat codes couldn't be applied");

    LOG("Resigning save '%s'...", entry->name);
    if (!psp_ResignSavedata(entry->path))
        show_message("Error! Save couldn't be resigned");

    show_message("Save %s successfully modified!", entry->title_id);
}

static void resignAllSaves(const save_entry_t* save, int all)
{
	char sfoPath[256];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Resigning all saves...");

	LOG("Resigning all saves from '%s'...", save->path);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_PSP || !(all || item->flags & SAVE_FLAG_SELECTED))
			continue;

		LOG("Patching SFO '%s'...", item->dir_name);
		(psp_ResignSavedata(item->path) ? done++ : err_count++);
	}

	end_progress_bar();

	show_message("%d/%d Saves resigned\n%s", done, done+err_count, save->path);
}

static void import_mcr2vmp(const save_entry_t* save, const char* src)
{
	char mcrPath[256];
	uint8_t *data = NULL;
	size_t size = 0;

	snprintf(mcrPath, sizeof(mcrPath), PS1_SAVES_PATH_HDD "%s/%s", USER_STORAGE_DEV, save->title_id, src);
	read_buffer(mcrPath, &data, &size);

	if (openMemoryCardStream(data, size, 0))
		show_message("Memory card successfully imported to:\n%s", save->path);
	else
		show_message("Error importing memory card:\n%s", mcrPath);

	free(data);
}

static void export_vmp2mcr(const save_entry_t* save)
{
	char mcrPath[256];

	snprintf(mcrPath, sizeof(mcrPath), PS1_SAVES_PATH_HDD "%s/%s", USER_STORAGE_DEV, save->title_id, strrchr(save->path, '/') + 1);
	strcpy(strrchr(mcrPath, '.'), ".MCR");
	mkdirs(mcrPath);

	if (saveMemoryCard(mcrPath, PS1CARD_RAW, 0))
		show_message("Memory card successfully exported to:\n%s", mcrPath);
	else
		show_message("Error exporting memory card:\n%s", save->path);
}

static int _copy_save_file(const char* src_path, const char* dst_path, const char* filename)
{
	char src[256], dst[256];

	snprintf(src, sizeof(src), "%s%s", src_path, filename);
	snprintf(dst, sizeof(dst), "%s%s", dst_path, filename);

	return (copy_file(src, dst) == SUCCESS);
}

static void decryptSaveFile(const save_entry_t* entry, const char* filename)
{
	char path[256];
	uint8_t key[16];

	if (entry->flags & SAVE_FLAG_PSP && !get_psp_save_key(entry, key))
	{
		show_message("Error! No game decryption key available for %s", entry->title_id);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/", USER_STORAGE_DEV, entry->dir_name);
	mkdirs(path);

	LOG("Decrypt '%s%s' to '%s'...", entry->path, filename, path);

	if (_copy_save_file(entry->path, path, filename))
	{
		strlcat(path, filename, sizeof(path));
		if (entry->flags & SAVE_FLAG_PSP && !psp_DecryptSavedata(entry->path, path, key))
			show_message("Error! File %s couldn't be exported", filename);

		show_message("File successfully exported to:\n%s", path);
	}
	else
		show_message("Error! File %s couldn't be exported", filename);
}

static void encryptSaveFile(const save_entry_t* entry, const char* filename)
{
	char path[256];
	uint8_t key[16];

	if (entry->flags & SAVE_FLAG_PSP && !get_psp_save_key(entry, key))
	{
		show_message("Error! No game decryption key available for %s", entry->title_id);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/%s", USER_STORAGE_DEV, entry->dir_name, filename);
	if (file_exists(path) != SUCCESS)
	{
		show_message("Error! Can't find decrypted save-game file:\n%s", path);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/", USER_STORAGE_DEV, entry->dir_name);
	LOG("Encrypt '%s%s' to '%s'...", path, filename, entry->path);

	if (_copy_save_file(path, entry->path, filename))
	{
		if (entry->flags & SAVE_FLAG_PSP && !psp_EncryptSavedata(entry->path, filename, key))
			show_message("Error! File %s couldn't be imported", filename);

		show_message("File successfully imported to:\n%s%s", entry->path, filename);
	}
	else
		show_message("Error! File %s couldn't be imported", filename);
}

static void downloadLink(const char* path)
{
	char url[256] = "http://";
	char out_path[256];

	if (!osk_dialog_get_text("Download URL", url, sizeof(url)))
		return;

	char *fname = strrchr(url, '/');
	snprintf(out_path, sizeof(out_path), "%s%s", path, fname ? ++fname : "download.bin");

	if (http_download(url, NULL, out_path, 1))
		show_message("File successfully downloaded to:\n%s", out_path);
	else
		show_message("Error! File couldn't be downloaded");
}

void execCodeCommand(code_entry_t* code, const char* codecmd)
{
	option_value_t* optval;

	switch (codecmd[0])
	{
		case CMD_DECRYPT_FILE:
			optval = list_get_item(code->options[0].opts, code->options[0].sel);
			decryptSaveFile(selected_entry, optval->name);
			code->activated = 0;
			break;

		case CMD_DOWNLOAD_USB:
			downloadSave(selected_entry, code->file, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_EXPORT_ZIP_USB:
			zipSave(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_USB:
			copySave(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_HDD:
			copySaveHDD(selected_entry);
			code->activated = 0;
			break;

		case CMD_EXP_PSPKEY:
			pspExportKey(selected_entry);
			code->activated = 0;
			break;

		case CMD_DUMP_PSPKEY:
			pspDumpKey(selected_entry, 1);
			code->activated = 0;
			break;

		case CMD_SETUP_PLUGIN:
			show_message(install_sgkey_plugin(codecmd[1]) ? "Plugin successfully %s" : "Error! Plugin couldn't be %s", codecmd[1] ? "installed" : "disabled");
			code->activated = 0;
			break;

		case CMD_IMP_MCR2VMP:
			optval = list_get_item(code->options[0].opts, code->options[0].sel);
			import_mcr2vmp(selected_entry, optval->name);
			selected_entry->flags |= SAVE_FLAG_UPDATED;
			code->activated = 0;
			break;

		case CMD_EXP_VMP2MCR:
			export_vmp2mcr(selected_entry);
			code->activated = 0;
			break;

		case CMD_RESIGN_VMP:
			if (vmp_resign(selected_entry->path))
				show_message("Memory card successfully resigned:\n%s", selected_entry->path);
			else
				show_message("Error resigning memory card:\n%s", selected_entry->path);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_USB:
		case CMD_COPY_ALL_SAVES_USB:
			copyAllSavesUSB(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_USB);
			code->activated = 0;
			break;

		case CMD_DUMP_FINGERPRINTS:
			dumpAllFingerprints(selected_entry);
			code->activated = 0;
			break;

		case CMD_RESIGN_SAVE:
			resignSave(selected_entry);
			code->activated = 0;
			break;

		case CMD_RESIGN_SAVES:
		case CMD_RESIGN_ALL_SAVES:
			resignAllSaves(selected_entry, codecmd[0] == CMD_RESIGN_ALL_SAVES);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_HDD:
		case CMD_COPY_ALL_SAVES_HDD:
			copyAllSavesHDD(selected_entry, codecmd[0] == CMD_COPY_ALL_SAVES_HDD);
			code->activated = 0;
			break;

		case CMD_EXP_SAVES_VMC:
		case CMD_EXP_ALL_SAVES_VMC:
			exportAllSavesVMC(selected_entry, codecmd[1], codecmd[0] == CMD_EXP_ALL_SAVES_VMC);
			code->activated = 0;
			break;

		case CMD_EXP_VMCSAVE:
			exportVmcSave(selected_entry, code->options[0].id, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_IMP_VMCSAVE:
			if (openSingleSave(code->file, (int*) host_buf))
				show_message("Save successfully imported:\n%s", code->file);
			else
				show_message("Error! Couldn't import save:\n%s", code->file);

			selected_entry->flags |= SAVE_FLAG_UPDATED;
			code->activated = 0;
			break;

		case CMD_DELETE_SAVE:
			if (deleteSave(selected_entry))
				selected_entry->flags |= SAVE_FLAG_UPDATED;
			else
				code->activated = 0;
			break;

		case CMD_SAVE_WEBSERVER:
			enableWebServer(webReqHandler, ((void**)selected_entry->dir_name)[0], 8080);
			code->activated = 0;
			break;

		case CMD_IMPORT_DATA_FILE:
			optval = list_get_item(code->options[0].opts, code->options[0].sel);
			encryptSaveFile(selected_entry, optval->name);
			code->activated = 0;
			break;

		case CMD_EXTRACT_ARCHIVE:
			extractArchive(code->file);
			code->activated = 0;
			break;

		case CMD_URL_DOWNLOAD:
			downloadLink(selected_entry->path);
			code->activated = 0;
			break;

		case CMD_NET_WEBSERVER:
			enableWebServer(dbg_simpleWebServerHandler, NULL, 8080);
			code->activated = 0;
			break;

		case CMD_CONV_CSO2ISO:
			if (convert_cso2iso(code->file))
				show_message("ISO successfully saved to %s", selected_entry->path);
			else
				show_message("Error! ISO couldn't be created");
			code->activated = 0;
			break;

		case CMD_CONV_ISO2CSO:
			if (convert_iso2cso(code->file))
				show_message("CSO successfully saved to %s", selected_entry->path);
			else
				show_message("Error! CSO couldn't be created");
			code->activated = 0;
			break;


		default:
			break;
	}

	return;
}
