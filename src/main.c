#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <zlib.h>

#define MAX_GZIP_OUTPUT (64u * 1024u * 1024u)  /* guard against decompression bombs */

const char cookie_expected_start1[] = ".ROBLOSECURITY=";
const char cookie_expected_start2[] = "_|WARNING:-DO-NOT-SHARE-THIS.--Sharing-this-will-allow-someone-to-log-in-as-you-and-to-steal-your-ROBUX-and-items.|";
const char request1_expected_start_font[] = "{\"locations\":[{\"assetFormat\":\"Font\",\"location\":\"";
const char request1_expected_start_decal[] = "{\"locations\":[{\"assetFormat\":\"Decal\",\"location\":\"";
const char request1_expected_start_source[] = "{\"locations\":[{\"assetFormat\":\"source\",\"location\":\"";

struct DataStruct {
        int memerror;
        char *memory;
        size_t size;
};

size_t write_data(char *buffer, size_t size, size_t nmemb, void *userp) {
        size *= nmemb;

        struct DataStruct *data = userp;

        data->memory = realloc(data->memory, data->size + size + 1);
        if (data->memory == NULL) {
                data->memerror = errno;
                return 0;
        }

        memcpy(&(data->memory[data->size]), buffer, size);
        data->size += size;
        data->memory[data->size] = 0;

        return size;
}

int makeAssetRequest(CURL *curl_handle, struct DataStruct *data, int64_t assetid, const char *cookie, const char *asset_type, const char *asset_format, const char *roblox_asset_format) {
        CURLcode error_code = CURLE_OK;
        int fail = 0;

        char url[100];
        snprintf(url, sizeof(url), "https://assetdelivery.roblox.com/v2/assetId/%ld", assetid);

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);

        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, data);

        struct curl_slist *headers = NULL;
 
        headers = curl_slist_append(headers, "Accept-Encoding: json");
        if (cookie) {
                char header[2048];
                snprintf(header, sizeof(header), "Cookie: %s", cookie);
                headers = curl_slist_append(headers, header);
        }
        if (asset_type) {
                char header[256];
                snprintf(header, sizeof(header), "AssetType: %s", asset_type);
                headers = curl_slist_append(headers, header);
        }
        if (asset_format) {
                char header[256];
                snprintf(header, sizeof(header), "AssetFormat: %s", asset_format);
                headers = curl_slist_append(headers, header);
        }
        if (roblox_asset_format) {
                char header[256];
                snprintf(header, sizeof(header), "Roblox-AssetFormat: %s", roblox_asset_format);
                headers = curl_slist_append(headers, header);
        }

        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

        error_code = curl_easy_perform(curl_handle);
        if (data->memerror) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to allocate memory for asset request data: %s\n", strerror(data->memerror));
        }
        if (error_code != CURLE_OK) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to perform asset request: %s\n", curl_easy_strerror(error_code));
        }

        curl_slist_free_all(headers);

        return fail;
}

int extractUrl(struct DataStruct *data, char *url, size_t expected_start_size) {
        size_t url_start = expected_start_size - 1;
        size_t url_end = url_start + 1;
        for (; url_end < data->size; url_end++) {
                if (data->memory[url_end] == '"') {
                        strncpy(url, data->memory + url_start, url_end - url_start);
                        return 0;
                }
        }

        return 1;
}

int inflateBuffer(const char *src, size_t src_size, char **out, size_t *out_size) {
        z_stream strm;
        memset(&strm, 0, sizeof strm);

        /* 15 + 32 = auto-detect zlib or gzip header */
        if (inflateInit2(&strm, 15 + 32) != Z_OK) {
                fprintf(stderr, "[ERROR] Failed to init gzip stream\n");
                return 1;
        }

        size_t cap = src_size * 4 > 4096 ? src_size * 4 : 4096;
        unsigned char *buf = malloc(cap + 1);
        if (!buf) {
                inflateEnd(&strm);
                fprintf(stderr, "[ERROR] Failed to allocate initial gzip buffer\n");
                return 1;
        }

        strm.next_in  = (Bytef *)src;
        strm.avail_in = (uInt)src_size;

        int ret;
        do {
                if (strm.total_out == cap) {
                        if (cap >= MAX_GZIP_OUTPUT) {
                                fprintf(stderr, "[ERROR] Gzip (%zu) cap exceeded max output (%u)\n", cap, MAX_GZIP_OUTPUT);
                                goto INFLATE_FAIL;
                        }
                        cap *= 2;
                        unsigned char *tmp = realloc(buf, cap + 1);
                        if (!tmp) {
                                fprintf(stderr, "[ERROR] Failed to reallocate gzip buffer\n");
                                goto INFLATE_FAIL;
                        }
                        buf = tmp;
                }

                strm.next_out  = buf + strm.total_out;
                strm.avail_out = (uInt)(cap - strm.total_out);

                ret = inflate(&strm, Z_NO_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END) {
                        fprintf(stderr, "[ERROR] Failed to inflate (decompress)\n");
                        goto INFLATE_FAIL;
                }
        } while (ret != Z_STREAM_END);

        buf[strm.total_out] = '\0';
        *out = (char *)buf;
        *out_size = strm.total_out;
        inflateEnd(&strm);
        return 0;

INFLATE_FAIL:
        free(buf);
        inflateEnd(&strm);
        return 1;
}

int makeRequest2(CURL *curl_handle, struct DataStruct *data1, char *output_path, size_t expected_start_size, int is_compressed) {
        CURLcode error_code = CURLE_OK;
        int fail = 0;

        char url[1024];
        memset(url, 0, sizeof(url));

        struct DataStruct compressed_data = {0};

        if (extractUrl(data1, url, expected_start_size)) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to extract url from asset request\n");
                goto REQUEST2_END;
        }

        FILE *output_file = NULL;
        if (output_path) {
                output_file = fopen(output_path, "w");
                if (output_file == NULL) {
                        int err = errno;
                        fail = 1;
                        fprintf(stderr, "[ERROR] Failed to open file '%s': %s\n", output_path, strerror(err));
                        goto REQUEST2_END;
                }
        } else
                output_file = stdout;

        struct curl_slist *headers = NULL;

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        if (is_compressed) {
                curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
                curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &compressed_data);
                headers = curl_slist_append(headers, "Accept-Encoding: gzip");
        } else
                curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, output_file);
 
        if (headers)
                curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);

        error_code = curl_easy_perform(curl_handle);

        if (headers)
                curl_slist_free_all(headers);

        if (error_code != CURLE_OK) {
                fail = 1;
                fprintf(stderr, "[ERROR] Failed to perform request 2: %s\n", curl_easy_strerror(error_code));
                goto REQUEST2_END;
        }

        if (is_compressed) {
                if (compressed_data.memerror) {
                        fail = 1;
                        fprintf(stderr, "[ERROR] Failed to allocate memory for second request data: %s\n", strerror(compressed_data.memerror));
                        goto REQUEST2_END;
                }

                char *decompressed;
                size_t decompressed_size;

                if (inflateBuffer(compressed_data.memory, compressed_data.size, &decompressed, &decompressed_size)) {
                        fprintf(stderr, "Compressed: %.*s\n", (int)(compressed_data.size), compressed_data.memory);
                        fail = 1;
                        goto REQUEST2_END;
                }

                if (fwrite(decompressed, sizeof(char), decompressed_size, output_file) < decompressed_size) {
                        fail = 1;
                        fprintf(stderr, "[ERROR] Failed to write to '%s'\n", output_path ? output_path : "[stdout]");
                        goto REQUEST2_END;
                }
                free(decompressed);
        }
        if (output_path) {
                fclose(output_file);
                printf("Successfully written to '%s'\n", output_path);
        }

REQUEST2_END:
        if (compressed_data.size)
                free(compressed_data.memory);

        return fail;
}

int checkRequest1Data(struct DataStruct *data, const char *expected_start) {
        size_t expected_start_len = strlen(expected_start);

        // 50 is just to roughly account for the rest of the message
        if (data->size < expected_start_len + 50) {
                fprintf(stderr, "[ERROR] Asset request did not contain enough bytes\n");
                return 1;
        }
        // TODO: why did i make a copy why can't i just rely on the strncmp n param?
        size_t tmp_size = expected_start_len - 1;
        char tmp[tmp_size];
        strncpy(tmp, data->memory, tmp_size);
        if (strncmp(tmp, expected_start, tmp_size) != 0) {
                fprintf(stderr, "[ERROR] Asset request did not match the expected start.\nData: %.*s\n", (int)(data->size), data->memory);
                return 1;
        }
        return 0;
}

int getNewCookie(const char *model_type, char *cookie, char *newcookie, size_t newcookie_size) {
        if (!cookie) {
                fprintf(stderr, "[ERROR] You must provider either --cookie or RAF_COOKIE environment variable for type '%s'\n", model_type);
                return 1;
        }

        size_t cookie_len = strlen(cookie);
        // NOTE: we only need to check start2 len since it's bigger than start1
        if (cookie_len < sizeof(cookie_expected_start2)) {
                fprintf(stderr, "[ERROR] Cookie is too small (should be at LEAST %zu bytes, but only was %zu)\n", sizeof(cookie_expected_start2), cookie_len);
                return 1;
        }

        int startswith_start1 = strncmp(cookie, cookie_expected_start1, sizeof(cookie_expected_start1) - 1) == 0;
        int startswith_start2 = strncmp(cookie, cookie_expected_start2, sizeof(cookie_expected_start2) - 1) == 0;

        if (!(startswith_start1 || startswith_start2)) {
                fprintf(stderr, "[ERROR] Invalid cookie. Expected it to start with either '.ROBLOSECURITY=' or '_|WARNING:-DO-NOT-SHARE-THIS...'\n");
                return 1;
        }

        size_t newcookie_pos = 0;
        if (startswith_start2) {
                printf("Cookie didn't start with '%s', so I'm inserting it...\n", cookie_expected_start1);

                strncpy(newcookie, cookie_expected_start1, newcookie_size);
                newcookie_pos += sizeof(cookie_expected_start1) - 1;
        }
        if (newcookie_pos + cookie_len >= 2048) {
                // TODO: is 'at MOST 2048' accurate here?
                fprintf(stderr, "[ERROR] Cookie is too big (should be at MOST 2048 bytes, but was %zu)\n", cookie_len);
                return 1;
        }
        strncpy(newcookie + newcookie_pos, cookie, cookie_len);
        return 0;
}

enum AssetType {
        ASSET_NONE,
        ASSET_FONT,
        ASSET_DECAL,
        ASSET_IMAGE,
        ASSET_MODEL,
        ASSET_PLUGIN,
        ASSET_PLACE,
        ASSET_AUDIO,
        ASSET_UNKNOWN1,
        ASSET_UNKNOWN2
};

void displayHelp(void) {
        printf("usage: raf TYPE ASSETID [options]\n"
               "\ntypes:\n"
               "  font\n"
               "  decal\n"
               "  image\n"
               "  model\n"
               "  plugin\n"
               "  place  -  the provided cookie's account must have access to the place\n"
               "  audio\n"
               "\noptions:\n"
               "  --output OUTPUTFILEPATH  -  needed for font, image, model, plugin, place, & audio\n"
               "  --cookie COOKIE          -  needed for decal, image, model, plugin, place & audio (you can also use RAF_COOKIE environment variable)\n");
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
        char *cookie = NULL;

        for (int i = 1; i < argc; i++) {
                char *arg = argv[i];
                if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
                        displayHelp();
                        return 0;
                } else if (strcmp(arg, "font") == 0) {
                        asset_type = ASSET_FONT;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "decal") == 0) {
                        asset_type = ASSET_DECAL;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "image") == 0) {
                        asset_type = ASSET_IMAGE;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "model") == 0) {
                        asset_type = ASSET_MODEL;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "plugin") == 0) {
                        asset_type = ASSET_PLUGIN;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "place") == 0) {
                        asset_type = ASSET_PLACE;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "audio") == 0) {
                        asset_type = ASSET_AUDIO;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "unknown1") == 0) {
                        asset_type = ASSET_UNKNOWN1;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "unknown2") == 0) {
                        asset_type = ASSET_UNKNOWN2;
                        goto READ_ASSETID_ARG;
                } else if (strcmp(arg, "--output") == 0) {
                        if (++i >= argc) {
                                fprintf(stderr, "[ERROR] Missing OUTPUTFILEPATH after '--output'\n");
                                return 1;
                        }
                        output_path = argv[i];
                } else if (strcmp(arg, "--cookie") == 0) {
                        if (++i >= argc) {
                                fprintf(stderr, "[ERROR] Missing COOKIE after '--cookie'\n");
                                return 1;
                        }
                        cookie = argv[i];
                } else {
                        fprintf(stderr, "[ERROR] Invalid argument '%s'\n", arg);
                        return 1;
                }

                continue;

        READ_ASSETID_ARG:
                if (++i >= argc) {
                        fprintf(stderr, "[ERROR] Missing ASSETID after '%s'\n", arg);
                        return 1;
                }
                arg = argv[i];
                if (readI64(arg, &assetid)) {
                        fprintf(stderr, "[ERROR] Invalid ASSETID (not an int64)\n");
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

        const char *asset_type_string = NULL;
        int needs_output = 0;
        int needs_cookie = 0;
        const char *request_asset_type = NULL;
        const char *request_asset_format = NULL;
        const char *request_roblox_asset_format = NULL;
        const char *expected_start = NULL;
        size_t expected_start_size = 0;
        int is_compressed = 0;

        int fail = 0;
        switch (asset_type) {
        case ASSET_NONE:
                fprintf(stderr, "[ERROR] You must choose an asset type\n");
                fail = 1;
                goto FINISHED;
                break;
        case ASSET_FONT:
                asset_type_string = "font";
                needs_output = 1;

                request_asset_type = request_asset_format = request_roblox_asset_format = "Font";
                expected_start = request1_expected_start_font;
                expected_start_size = sizeof(request1_expected_start_font);

                is_compressed = 1;
                break;
        case ASSET_DECAL:
                asset_type_string = "decal";
                needs_cookie = 1;

                request_asset_type = request_asset_format = request_roblox_asset_format = "Decal";
                expected_start = request1_expected_start_decal;
                expected_start_size = sizeof(request1_expected_start_decal);

                is_compressed = 1;
                break;
        case ASSET_IMAGE:
                asset_type_string = "image";
                needs_output = 1;
                needs_cookie = 1;

                request_asset_type = request_asset_format = request_roblox_asset_format = "Image";
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_MODEL:
                asset_type_string = "model";
                needs_output = 1;
                needs_cookie = 1;

                request_asset_type = "Model";
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_PLUGIN:
                asset_type_string = "plugin";
                needs_output = 1;
                needs_cookie = 1;

                request_asset_type = "Plugin";
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_PLACE:
                asset_type_string = "place";
                needs_output = 1;
                needs_cookie = 1;

                request_asset_type = "Place";
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_AUDIO:
                asset_type_string = "audio";
                needs_output = 1;
                needs_cookie = 1;

                request_asset_type = "Audio";
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_UNKNOWN1:
                asset_type_string = "unknown1";

                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        case ASSET_UNKNOWN2:
                asset_type_string = "unknown2";

                // TODO: the idea here was that we would check something other than the 'source' start,
                // which probably means we will need to dynamically get the url and not check really anything
                expected_start = request1_expected_start_source;
                expected_start_size = sizeof(request1_expected_start_source);

                is_compressed = 1;
                break;
        }

        if (needs_output && !output_path) {
                fprintf(stderr, "[ERROR] You must provide an output file for type '%s'\n", asset_type_string);
                fail = 1;
                goto FINISHED;
        }

        if (!cookie)
                cookie = getenv("RAF_COOKIE");

        char newcookie[2048];
        if ((needs_cookie || cookie) && getNewCookie(asset_type_string, cookie, newcookie, sizeof(newcookie))) {
                fail = 1;
                goto FINISHED;
        }

        fail = makeAssetRequest(curl_handle, &data1, assetid, needs_cookie ? newcookie : NULL, request_asset_type, request_asset_format, request_roblox_asset_format);
        curl_easy_reset(curl_handle);
        if (fail)
                goto FINISHED;

        if (checkRequest1Data(&data1, expected_start)) {
                fail = 1;
                goto FINISHED;
        }

        fail = makeRequest2(curl_handle, &data1, output_path, expected_start_size, is_compressed);

FINISHED:
        if (data1.size)
                free(data1.memory);
        curl_easy_cleanup(curl_handle);

        curl_global_cleanup();
        return fail;
}
