#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <zlib.h>
#include <pspiofilemgr.h>
#include <psputility.h>

#include "types.h"
#include "common.h"

#define TMP_BUFF_SIZE 0x20000

//----------------------------------------
//String Utils
//----------------------------------------
int is_char_integer(char c)
{
	if (c >= '0' && c <= '9')
		return SUCCESS;
	return FAILED;
}

int is_char_letter(char c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
		return SUCCESS;
	return FAILED;
}

char * safe_strncpy(char *dst, const char* src, size_t size)
{
    strncpy(dst, src, size);
    dst[size - 1] = '\0';
    return dst;
}

char * rstrip(char *s)
{
    char *p = s + strlen(s);
    while (p > s && isspace(*--p))
        *p = '\0';
    return s;
}

char * lskip(const char *s)
{
    while (*s != '\0' && isspace(*s))
        ++s;
    return (char *)s;
}

//----------------------------------------
//FILE UTILS
//----------------------------------------

int file_exists(const char *path)
{
    return (access(path, F_OK));
}

int dir_exists(const char *path)
{
    DIR* d;
    if ((d = opendir(path)) != NULL)
    {
        closedir(d);
        return SUCCESS;
    }
    return FAILED;
}

int unlink_secure(const char *path)
{
    if(file_exists(path)==SUCCESS)
    {
        chmod(path, 0777);
        return sceIoRemove(path);
    }
    return FAILED;
}

/*
* Creates all the directories in the provided path. (can include a filename)
* (directory must end with '/')
*/
int mkdirs(const char* dir)
{
    SceIoStat stat;
    char path[256];
    snprintf(path, sizeof(path), "%s", dir);

    char* ptr = strrchr(path, '/');
    *ptr = 0;

    if (sceIoGetstat(path, &stat) == SUCCESS && FIO_S_ISDIR(stat.st_mode))
        return SUCCESS;

    ptr = strchr(path, '/');
    if (!ptr)
        return FAILED;
    else
        ptr++;

    while (*ptr)
    {
        while (*ptr && *ptr != '/')
            ptr++;

        char last = *ptr;
        *ptr = 0;

        if (dir_exists(path) == FAILED)
        {
            if (mkdir(path, 0777) < 0)
                return FAILED;
            else
                chmod(path, 0777);
        }
        
        *ptr++ = last;
        if (last == 0)
            break;
    }

    if (sceIoGetstat(path, &stat) < 0 || !FIO_S_ISDIR(stat.st_mode))
        return FAILED;

    return SUCCESS;
}

int copy_file(const char* input, const char* output)
{
    size_t read, written;
    FILE *fd, *fd2;

    if (mkdirs(output) != SUCCESS)
        return FAILED;

    if((fd = fopen(input, "rb")) == NULL)
        return FAILED;

    if((fd2 = fopen(output, "wb")) == NULL)
    {
        fclose(fd);
        return FAILED;
    }

    char* buffer = malloc(TMP_BUFF_SIZE);

    if (!buffer)
        return FAILED;

    do
    {
        read = fread(buffer, 1, TMP_BUFF_SIZE, fd);
        written = fwrite(buffer, 1, read, fd2);
    }
    while ((read == written) && (read == TMP_BUFF_SIZE));

    free(buffer);
    fclose(fd);
    fclose(fd2);
    chmod(output, 0777);

    return (read - written);
}

uint32_t file_crc32(const char* input)
{
    Bytef *buffer;
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t read;

    FILE* in = fopen(input, "rb");
    if (!in)
        return FAILED;

    buffer = malloc(TMP_BUFF_SIZE);

    do
    {
        read = fread(buffer, 1, TMP_BUFF_SIZE, in);
        crc = crc32(crc, buffer, read);
    }
    while (read == TMP_BUFF_SIZE);

    free(buffer);
    fclose(in);

    return crc;
}

int copy_directory(const char* startdir, const char* inputdir, const char* outputdir)
{
	char fullname[256];
	char out_name[256];
	struct dirent *dirp;
	int len = strlen(startdir);
	DIR *dp = opendir(inputdir);

	if (!dp) {
		return FAILED;
	}

	while ((dirp = readdir(dp)) != NULL) {
		if ((strcmp(dirp->d_name, ".")  != 0) && (strcmp(dirp->d_name, "..") != 0)) {
			snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

			if (dir_exists(fullname) == SUCCESS) {
				strcat(fullname, "/");
				copy_directory(startdir, fullname, outputdir);
			} else {
				snprintf(out_name, sizeof(out_name), "%s%s", outputdir, &fullname[len]);
				if (copy_file(fullname, out_name) != SUCCESS)
					return FAILED;
			}
		}
	}
	closedir(dp);

	return SUCCESS;
}

int clean_directory(const char* inputdir, const char* filter)
{
	DIR *d;
	struct dirent *dir;
	char dataPath[256];

	d = opendir(inputdir);
	if (!d)
		return FAILED;

	while ((dir = readdir(d)) != NULL)
	{
		if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0 && strstr(dir->d_name, filter) != NULL)
		{
			snprintf(dataPath, sizeof(dataPath), "%s%s", inputdir, dir->d_name);
			unlink_secure(dataPath);
		}
	}
	closedir(d);

	return SUCCESS;
}

const char * get_user_language(void)
{
    int language;

    // Prompt language
    if(sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language) < 0)
        return "en";

    switch (language)
    {
    case PSP_SYSTEMPARAM_LANGUAGE_JAPANESE:             //  0   Japanese
        return "ja";

    case PSP_SYSTEMPARAM_LANGUAGE_ENGLISH:              //  1   English (United States)
        return "en";

    case PSP_SYSTEMPARAM_LANGUAGE_FRENCH:               //  2   French
        return "fr";

    case PSP_SYSTEMPARAM_LANGUAGE_SPANISH:              //  3   Spanish
        return "es";

    case PSP_SYSTEMPARAM_LANGUAGE_GERMAN:               //  4   German
        return "de";

    case PSP_SYSTEMPARAM_LANGUAGE_ITALIAN:              //  5   Italian
        return "it";

    case PSP_SYSTEMPARAM_LANGUAGE_DUTCH:                //  6   Dutch
        return "nl";

    case PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN:              //  8   Russian
        return "ru";

    case PSP_SYSTEMPARAM_LANGUAGE_KOREAN:               //  9   Korean
        return "ko";

    case PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL:  // 10   Chinese (traditional)
    case PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED:   // 11   Chinese (simplified)
        return "zh";

    case PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE:           //  7   Portuguese (Portugal)
        return "pt";

    default:
        break;
    }

    return "en";
}
