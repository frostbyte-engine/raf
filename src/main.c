#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <curl/easy.h>

const char fontrequest1_expected_start[] = "{\"locations\":[{\"assetFormat\":\"Font\",\"location\":\"";

struct DataStruct {
        int error;
        char *memory;
        size_t size;
};

size_t write_data(char *buffer, size_t size, size_t nmemb, void *userp) {
        size *= nmemb;

        struct DataStruct *data = userp;

        data->memory = realloc(data->memory, data->size + size + 1);
        if (data->memory == NULL) {
                data->error = errno;
                return 0;
        }

        memcpy(&(data->memory[data->size]), buffer, size);
        data->size += size;
        data->memory[data->size] = 0;

        return size;
}

int makeFontRequest1(CURL *curl_handle, struct DataStruct *data1, int64_t assetid) {
        CURLcode error_code = CURLE_OK;
        int fail = 0;

        char url[100];
        snprintf(url, sizeof(url), "https://assetdelivery.roblox.com/v2/assetId/%ld", assetid);

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);

        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, data1);

        struct curl_slist *headers = NULL;
 
        headers = curl_slist_append(headers, "Accept-Encoding: json");
        headers = curl_slist_append(headers, "AssetType: Font");
        headers = curl_slist_append(headers, "AssetFormat: Font");
        headers = curl_slist_append(headers, "Roblox-AssetFormat: Font");

        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

        error_code = curl_easy_perform(curl_handle);
        if (data1->error) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to allocate memory for font request 1 data: %s\n", strerror(data1->error));
        }
        if (error_code != CURLE_OK) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to perform font request 1: %s\n", curl_easy_strerror(error_code));
        }

        curl_slist_free_all(headers);

        return fail;
}
int makeFontRequest2(CURL *curl_handle, struct DataStruct *data1, char *output_path) {
        CURLcode error_code = CURLE_OK;
        int fail = 0;

        char url[1024];
        memset(url, 0, sizeof(url));

        int good_url = 0;
        size_t url_start = sizeof(fontrequest1_expected_start) - 1;
        size_t url_end = url_start + 1;
        for (; url_end < data1->size; url_end++) {
                if (data1->memory[url_end] == '"') {
                        good_url = 1;
                        break;
                }
        }
        if (!good_url) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to extract url from font request 1\n");
                goto REQUEST2_END;
        }

        strncpy(url, data1->memory + url_start, url_end - url_start);

        FILE *output_file = fopen(output_path, "w");
        if (output_file == NULL) {
                int err = errno;
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to open file '%s': %s\n", output_path, strerror(err));
                goto REQUEST2_END;
        }

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, output_file);

        error_code = curl_easy_perform(curl_handle);
        if (error_code != CURLE_OK) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to perform font request 2: %s\n", curl_easy_strerror(error_code));
        }

        if (!fail) {
                fclose(output_file);
                printf("Successfully written to '%s'\n", output_path);
        }

        REQUEST2_END:

        return fail;
}

enum AssetType {
        ASSET_NONE,
        ASSET_FONT
};

void displayHelp(void) {
        printf("usage: raf TYPE ASSETID [options]\n\ntypes:\n  font\n\noptions:\n  --output OUTPUTFILEPATH\n");
}

int readI64(const char *str, int64_t *out) {
    char *end;
    errno = 0;
    long long v = strtoll(str, &end, 10);

    if (end == str)
            return 1;
    if (*end != '\0')
            return 1;
    if (errno == ERANGE)
            return 1;

    *out = (int64_t)v;
    return 0;
}

int main(int argc, char **argv) {
        if (argc < 2) {
                displayHelp();
                return 0;
        }

        enum AssetType asset_type = ASSET_NONE;
        int64_t assetid = 0;
        char *output_path = NULL;

        for (int i = 1; i < argc; i++) {
                char *arg = argv[i];
                if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
                        displayHelp();
                        return 0;
                } else if (strcmp(arg, "font") == 0) {
                        asset_type = ASSET_FONT;
                        if (++i >= argc) {
                                fprintf(stderr, "[ERROR] Missing PLACEID after 'font'\n");
                                return 1;
                        }
                        arg = argv[i];
                        if (readI64(arg, &assetid)) {
                                fprintf(stderr, "[ERROR] Invalid PLACEID (not an int64)\n");
                                return 1;
                        }
                } else if (strcmp(arg, "--output") == 0) {
                        if (++i >= argc) {
                                fprintf(stderr, "[ERROR] Missing OUTPUTFILEPATH after '--output'\n");
                                return 1;
                        }
                        output_path = argv[i];
                } else {
                        fprintf(stderr, "[ERROR] Invalid argument '%s'\n", arg);
                        return 1;
                }
        }

        CURLcode error_code = curl_global_init(CURL_GLOBAL_ALL);
        if (error_code != CURLE_OK) {
                fprintf(stderr, "[ERROR] Failed to init curl: %s\n", curl_easy_strerror(error_code));
                return 1;
        }

        CURL *curl_handle = curl_easy_init();

        struct DataStruct data1 = {0};

        int fail = 0;
        switch (asset_type) {
        case ASSET_NONE:
                fprintf(stderr, "[ERROR] You must choose an asset type\n");
                fail = 1;
                break;
        case ASSET_FONT:
                if (!output_path) {
                        fprintf(stderr, "[ERROR] You must provide an output file for type 'font'\n");
                        fail = 1;
                        break;
                }

                fail = makeFontRequest1(curl_handle, &data1, assetid);

                curl_easy_reset(curl_handle);

                if (!fail) {
                        // 50 is just to roughly account for the rest of the message
                        if (data1.size < sizeof(fontrequest1_expected_start) + 50) {
                                fprintf(stderr, "[ERROR] Font request 1 did not have enough bytes\n");
                                fail = 1;
                        }
                        size_t tmp_size = sizeof(fontrequest1_expected_start) - 1;
                        char tmp[tmp_size];
                        strncpy(tmp, data1.memory, tmp_size);
                        if (strncmp(tmp, fontrequest1_expected_start, tmp_size) != 0) {
                                fprintf(stderr, "[ERROR] Font request 1 did not match the expected start.\nData: %.*s\n", (int)(data1.size), data1.memory);
                                fail = 1;
                        }
                }

                // check fail again because we may set fail again above
                if (!fail)
                        fail = makeFontRequest2(curl_handle, &data1, output_path);
        }

        curl_easy_cleanup(curl_handle);

        curl_global_cleanup();
        return fail;
}
