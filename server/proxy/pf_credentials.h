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

#ifndef FREERDP_SERVER_PROXY_CREDENTIALS_H
#define FREERDP_SERVER_PROXY_CREDENTIALS_H

#include <winpr/wtypes.h>
#include <winpr/collections.h>

#ifdef __cplusplus
extern "C"
{
#endif

	/**
	 * @brief Credential entry for a specific mstshash
	 */
	typedef struct
	{
		char* username;
		char* password;
		char* domain;
		char* target_host; /* Optional: override target per hash */
		UINT16 target_port;
	} CredentialEntry;

	/**
	 * @brief Load credential mapping from a JSON file
	 *
	 * @param filepath Path to the JSON credential mapping file
	 * @return HashTable* mapping mstshash -> CredentialEntry*, or NULL on failure
	 */
	wHashTable* pf_credentials_load_mapping(const char* filepath);

	/**
	 * @brief Free credential mapping and all entries
	 *
	 * @param map The credential mapping to free
	 */
	void pf_credentials_free_mapping(wHashTable* map);

	/**
	 * @brief Lookup credentials by mstshash
	 *
	 * @param map The credential mapping
	 * @param mstshash The mstshash to lookup
	 * @return CredentialEntry* or NULL if not found
	 */
	CredentialEntry* pf_credentials_lookup(wHashTable* map, const char* mstshash);

	/**
	 * @brief Free a single credential entry
	 *
	 * @param entry The entry to free
	 */
	void pf_credentials_free_entry(CredentialEntry* entry);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_SERVER_PROXY_CREDENTIALS_H */
