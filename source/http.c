#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <psputility.h>

#include "common.h"
#include "saves.h"

#define HTTP_SUCCESS 	1
#define HTTP_FAILED	 	0
#define HTTP_USER_AGENT "Mozilla/5.0 (PLAYSTATION PORTABLE; 1.00)"

static int net_up = 0;

int psp_DisplayNetDialog(void);

char * basename (const char *filename)
{
	char *p = strrchr (filename, '/');
	return p ? p + 1 : (char *) filename;
}

int network_up(void)
{
	if (net_up)
		return HTTP_SUCCESS;

	if (!psp_DisplayNetDialog())
		return HTTP_FAILED;

	net_up = 1;

	return HTTP_SUCCESS;
}

int http_init(void)
{
	int ret = 0;

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
	
	if ((ret = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024)) < 0) {
		LOG("sceNetInit() failed: 0x%08x\n", ret);
		return HTTP_FAILED;
	}

	if ((ret = sceNetInetInit()) < 0) {
		LOG("sceNetInetInit() failed: 0x%08x\n", ret);
		return HTTP_FAILED;
	}

	if ((ret = sceNetApctlInit(0x8000, 48)) < 0) {
		LOG("sceNetApctlInit() failed: 0x%08x\n", ret);
		return HTTP_FAILED;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	return HTTP_SUCCESS;
}

/* follow the CURLOPT_XFERINFOFUNCTION callback definition */
static int update_progress(void *p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
	LOG("DL: %lld / %lld", dlnow, dltotal);
	update_progress_bar(dlnow, dltotal, (const char*) p);

	return 0;
}

static int upload_progress(void *p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
	LOG("UL: %lld / %lld", ulnow, ultotal);
	update_progress_bar(ulnow, ultotal, (const char*) p);
	return 0;
}

static void set_curl_opts(CURL* curl)
{
	union SceNetApctlInfo proxy_info;

	// Set user agent string
	curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
	// not sure how to use this when enabled
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	// not sure how to use this when enabled
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	// Set SSL VERSION to TLS 1.2
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	// Set timeout for the connection to build
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	// Follow redirects (?)
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// maximum number of redirects allowed
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
	// Fail the request if the HTTP code returned is equal to or larger than 400
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	// request using SSL for the FTP transfer if available
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

	// check for proxy settings
	memset(&proxy_info, 0, sizeof(proxy_info));
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_USE_PROXY, &proxy_info);
	
	if (proxy_info.useProxy)
	{
		memset(&proxy_info, 0, sizeof(proxy_info));
		sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PROXY_URL, &proxy_info);
		curl_easy_setopt(curl, CURLOPT_PROXY, proxy_info.proxyUrl);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

		memset(&proxy_info, 0, sizeof(proxy_info));
		sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PROXY_PORT, &proxy_info);
		curl_easy_setopt(curl, CURLOPT_PROXYPORT, proxy_info.proxyPort);
	}
}

int http_download(const char* url, const char* filename, const char* local_dst, int show_progress)
{
	union SceNetApctlInfo proxy_info;
	char full_url[1024];
	CURL *curl;
	CURLcode res;
	FILE* fd;

	if (network_up() != HTTP_SUCCESS)
		return HTTP_FAILED;

	curl = curl_easy_init();
	if(!curl)
	{
		LOG("ERROR: CURL INIT");
		return HTTP_FAILED;
	}

	fd = fopen(local_dst, "wb");
	if (!fd) {
		LOG("fopen Error: File path '%s'", local_dst);
		return HTTP_FAILED;
	}

	if (!filename) filename = "";
	snprintf(full_url, sizeof(full_url), "%s%s", url, filename);
	LOG("URL: %s >> %s", full_url, local_dst);

	set_curl_opts(curl);
	// Set the URL to get
	curl_easy_setopt(curl, CURLOPT_URL, full_url);
	// The function that will be used to write the data 
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	// The data filedescriptor which will be written to
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);

	if (show_progress)
	{
		init_progress_bar("Downloading...");
		/* pass the struct pointer into the xferinfo function */
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &update_progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, full_url);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	// Perform the request
	res = curl_easy_perform(curl);

	if (res == CURLE_SSL_CONNECT_ERROR)
	{
		curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_NONE);
		res = curl_easy_perform(curl);
	}

	// close file descriptor
	fclose(fd);
	// cleanup
	curl_easy_cleanup(curl);

	if (show_progress)
		end_progress_bar();

	if(res != CURLE_OK)
	{
		LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
		unlink_secure(local_dst);
		return HTTP_FAILED;
	}

	return HTTP_SUCCESS;
}

void http_end(void)
{
	curl_global_cleanup();

	sceNetApctlTerm();
	sceNetInetTerm();
	sceNetTerm();

	sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
	sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
}

int ftp_upload(const char* local_file, const char* url, const char* filename, int show_progress)
{
	FILE *fd;
	CURL *curl;
	CURLcode res;
	char remote_url[1024];
	long fsize;
	curl_off_t remoteFileSize = -1;

	if (network_up() != HTTP_SUCCESS)
		return HTTP_FAILED;

	/* get a curl handle */
	curl = curl_easy_init();
	if(!curl)
	{
		LOG("ERROR: CURL INIT");
		return HTTP_FAILED;
	}

	/* get a FILE * of the same file */
	fd = fopen(local_file, "rb");
	if(!fd)
	{
		LOG("Couldn't open '%s'", local_file);
		return HTTP_FAILED;
	}

	/* get the file size of the local file */
	fseek(fd, 0, SEEK_END);
	fsize = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	snprintf(remote_url, sizeof(remote_url), "%s%s", url, filename);
	LOG("Local file size: %ld bytes.", fsize);
	LOG("Uploading (%s) -> (%s)", local_file, remote_url);

	set_curl_opts(curl);
	curl_easy_setopt(curl, CURLOPT_URL, remote_url);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
	// create missing dirs if needed
	curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);

	res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		LOG("Remote check failed: %s", curl_easy_strerror(res));
		fclose(fd);
		curl_easy_cleanup(curl);
		return HTTP_FAILED;
	}
	else curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &remoteFileSize);

	if (remoteFileSize > fsize)
	{
		LOG("Error! '%s' remote size: %lld", filename, remoteFileSize);
		fclose(fd);
		curl_easy_cleanup(curl);
		return HTTP_FAILED;
	}

	if (remoteFileSize > 0)
	{
		LOG("Resuming '%s' at %lld bytes", filename, remoteFileSize);
		fsize -= remoteFileSize;
		fseek(fd, (long)remoteFileSize, SEEK_SET);
		curl_easy_setopt(curl, CURLOPT_APPEND, 1L);
	}

	/* enable uploading */
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	/* specify target */
	curl_easy_setopt(curl, CURLOPT_URL, remote_url);
	// set file data transfer
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0);
	/* please ignore the IP in the PASV response */
	curl_easy_setopt(curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);
	/* we want to use our own read function */
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, fread);
	/* now specify which file to upload */
	curl_easy_setopt(curl, CURLOPT_READDATA, fd);
	/* Set the size of the file to upload (optional). */
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fsize);

	if (show_progress)
	{
		init_progress_bar("Uploading...");
		/* pass the struct pointer into the xferinfo function */
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &upload_progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*) filename);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	/* Now run off and do what you have been told! */
	res = curl_easy_perform(curl);

	if (res == CURLE_SSL_CONNECT_ERROR)
	{
		curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_NONE);
		res = curl_easy_perform(curl);
	}

	/* close the local file */
	fclose(fd);

	/* always cleanup */
	curl_easy_cleanup(curl);

	if (show_progress)
		end_progress_bar();

	/* Check for errors */
	if(res != CURLE_OK)
	{
		LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
		return HTTP_FAILED;
	}

	return HTTP_SUCCESS;
}
