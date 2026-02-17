/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * FreeRDP Proxy Server - Credential Mapping
 *
 * Copyright 2026 FreeRDP Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <winpr/crt.h>
#include <winpr/file.h>
#include <winpr/path.h>
#include <winpr/string.h>

#include <freerdp/server/proxy/proxy_log.h>

#include "pf_credentials.h"

#define TAG PROXY_TAG("credentials")

/* Simple JSON parser for credential mapping file */
static char* read_file_contents(const char* filepath, size_t* out_size)
{
	FILE* fp = NULL;
	char* buffer = NULL;
	size_t file_size = 0;
	size_t read_size = 0;

	fp = winpr_fopen(filepath, "rb");
	if (!fp)
	{
		WLog_ERR(TAG, "Failed to open credential mapping file: %s (errno=%d)", filepath, errno);
		return NULL;
	}

	/* Get file size */
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		WLog_ERR(TAG, "Failed to seek to end of file: %s", filepath);
		fclose(fp);
		return NULL;
	}

	file_size = ftell(fp);
	if (file_size == (size_t)-1)
	{
		WLog_ERR(TAG, "Failed to get file size: %s", filepath);
		fclose(fp);
		return NULL;
	}

	rewind(fp);

	/* Allocate buffer */
	buffer = (char*)calloc(1, file_size + 1);
	if (!buffer)
	{
		WLog_ERR(TAG, "Failed to allocate memory for file contents");
		fclose(fp);
		return NULL;
	}

	/* Read file */
	read_size = fread(buffer, 1, file_size, fp);
	fclose(fp);

	if (read_size != file_size)
	{
		WLog_ERR(TAG, "Failed to read entire file: %s", filepath);
		free(buffer);
		return NULL;
	}

	buffer[file_size] = '\0';
	if (out_size)
		*out_size = file_size;

	return buffer;
}

/* Simple JSON string extraction helper */
static char* extract_json_string(const char* json, const char* key)
{
	char search_key[256];
	const char* key_pos = NULL;
	const char* value_start = NULL;
	const char* value_end = NULL;
	size_t value_len = 0;
	char* result = NULL;

	if (!json || !key)
		return NULL;

	/* Build search pattern: "key": " */
	snprintf(search_key, sizeof(search_key), "\"%s\"", key);
	key_pos = strstr(json, search_key);
	if (!key_pos)
		return NULL;

	/* Find the opening quote of the value */
	value_start = strchr(key_pos + strlen(search_key), '"');
	if (!value_start)
		return NULL;
	value_start++; /* Skip the opening quote */

	/* Find the closing quote */
	value_end = strchr(value_start, '"');
	if (!value_end)
		return NULL;

	/* Extract the value */
	value_len = value_end - value_start;
	result = (char*)calloc(1, value_len + 1);
	if (!result)
		return NULL;

	memcpy(result, value_start, value_len);
	result[value_len] = '\0';

	return result;
}

/* Simple JSON number extraction helper */
static UINT16 extract_json_uint16(const char* json, const char* key, UINT16 default_value)
{
	char search_key[256];
	const char* key_pos = NULL;
	const char* value_start = NULL;
	unsigned long value = 0;

	if (!json || !key)
		return default_value;

	/* Build search pattern: "key": */
	snprintf(search_key, sizeof(search_key), "\"%s\":", key);
	key_pos = strstr(json, search_key);
	if (!key_pos)
		return default_value;

	/* Skip to the value */
	value_start = key_pos + strlen(search_key);
	while (*value_start == ' ' || *value_start == '\t')
		value_start++;

	value = strtoul(value_start, NULL, 10);
	if (value > 65535)
		return default_value;

	return (UINT16)value;
}

static CredentialEntry* parse_credential_entry(const char* json_block)
{
	CredentialEntry* entry = NULL;

	if (!json_block)
		return NULL;

	entry = (CredentialEntry*)calloc(1, sizeof(CredentialEntry));
	if (!entry)
		return NULL;

	entry->username = extract_json_string(json_block, "username");
	entry->password = extract_json_string(json_block, "password");
	entry->domain = extract_json_string(json_block, "domain");
	entry->target_host = extract_json_string(json_block, "target_host");
	entry->target_port = extract_json_uint16(json_block, "target_port", 3389);

	/* Username and password are required */
	if (!entry->username || !entry->password)
	{
		pf_credentials_free_entry(entry);
		return NULL;
	}

	return entry;
}

void pf_credentials_free_entry(CredentialEntry* entry)
{
	if (!entry)
		return;

	free(entry->username);
	free(entry->password);
	free(entry->domain);
	free(entry->target_host);
	free(entry);
}

static void credential_entry_free_fn(void* obj)
{
	pf_credentials_free_entry((CredentialEntry*)obj);
}

wHashTable* pf_credentials_load_mapping(const char* filepath)
{
	char* file_contents = NULL;
	size_t file_size = 0;
	wHashTable* map = NULL;
	const char* credentials_section = NULL;
	const char* current_pos = NULL;
	const char* hash_start = NULL;
	const char* hash_end = NULL;
	const char* block_start = NULL;
	const char* block_end = NULL;

	if (!filepath)
	{
		WLog_ERR(TAG, "Credential mapping filepath is NULL");
		return NULL;
	}

	WLog_INFO(TAG, "Loading credential mapping from: %s", filepath);

	/* Read file contents */
	file_contents = read_file_contents(filepath, &file_size);
	if (!file_contents)
		return NULL;

	/* Create hash table */
	map = HashTable_New(TRUE);
	if (!map)
	{
		WLog_ERR(TAG, "Failed to create hash table for credential mapping");
		free(file_contents);
		return NULL;
	}

	/* Set value free function */
	if (!HashTable_SetupForStringData(map, FALSE))
	{
		WLog_ERR(TAG, "Failed to setup hash table for string keys");
		HashTable_Free(map);
		free(file_contents);
		return NULL;
	}

	wObject* obj = HashTable_ValueObject(map);
	if (obj)
		obj->fnObjectFree = credential_entry_free_fn;

	/* Find "credentials" section */
	credentials_section = strstr(file_contents, "\"credentials\"");
	if (!credentials_section)
	{
		WLog_ERR(TAG, "No 'credentials' section found in mapping file");
		HashTable_Free(map);
		free(file_contents);
		return NULL;
	}

	/* Find the opening brace of credentials object */
	current_pos = strchr(credentials_section, '{');
	if (!current_pos)
	{
		WLog_ERR(TAG, "Invalid JSON format in credentials section");
		HashTable_Free(map);
		free(file_contents);
		return NULL;
	}
	current_pos++; /* Skip opening brace */

	/* Parse each credential entry */
	while ((hash_start = strchr(current_pos, '"')) != NULL)
	{
		char* mstshash = NULL;
		CredentialEntry* entry = NULL;

		/* Check if we've reached the end of credentials object */
		if (hash_start > credentials_section && *(hash_start - 1) == '}')
			break;

		hash_start++; /* Skip opening quote */
		hash_end = strchr(hash_start, '"');
		if (!hash_end)
			break;

		/* Extract mstshash key */
		size_t hash_len = hash_end - hash_start;
		mstshash = (char*)calloc(1, hash_len + 1);
		if (!mstshash)
			break;
		memcpy(mstshash, hash_start, hash_len);
		mstshash[hash_len] = '\0';

		/* Find the credential block for this hash */
		block_start = strchr(hash_end, '{');
		if (!block_start)
		{
			free(mstshash);
			break;
		}

		/* Find matching closing brace */
		int brace_count = 1;
		block_end = block_start + 1;
		while (*block_end && brace_count > 0)
		{
			if (*block_end == '{')
				brace_count++;
			else if (*block_end == '}')
				brace_count--;
			block_end++;
		}

		if (brace_count != 0)
		{
			free(mstshash);
			break;
		}

		/* Parse credential entry */
		size_t block_len = block_end - block_start;
		char* block_str = (char*)calloc(1, block_len + 1);
		if (block_str)
		{
			memcpy(block_str, block_start, block_len);
			block_str[block_len] = '\0';

			entry = parse_credential_entry(block_str);
			free(block_str);

			if (entry)
			{
				if (!HashTable_Insert(map, mstshash, entry))
				{
					WLog_WARN(TAG, "Failed to insert credential entry for hash: %s", mstshash);
					pf_credentials_free_entry(entry);
					free(mstshash);
				}
				else
				{
					WLog_INFO(TAG, "Loaded credentials for mstshash: %s (user: %s)", mstshash,
					          entry->username);
					/* mstshash is now owned by the hash table, don't free it */
				}
			}
			else
			{
				WLog_WARN(TAG, "Failed to parse credential entry for hash: %s", mstshash);
				free(mstshash);
			}
		}
		else
		{
			free(mstshash);
		}

		current_pos = block_end;
	}

	free(file_contents);

	size_t count = HashTable_Count(map);
	WLog_INFO(TAG, "Loaded %zu credential mappings", count);

	if (count == 0)
	{
		WLog_WARN(TAG, "No valid credential entries found in mapping file");
		HashTable_Free(map);
		return NULL;
	}

	return map;
}

void pf_credentials_free_mapping(wHashTable* map)
{
	if (!map)
		return;

	HashTable_Free(map);
}

CredentialEntry* pf_credentials_lookup(wHashTable* map, const char* mstshash)
{
	if (!map || !mstshash)
		return NULL;

	return (CredentialEntry*)HashTable_GetItemValue(map, mstshash);
}
