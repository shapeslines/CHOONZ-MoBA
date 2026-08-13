#pragma once

#include <stdint.h>

#include "assets/asset_id.h"

typedef enum AssetType {
    ASSET_TYPE_NONE = 0,
    ASSET_TYPE_TEXTURE = 1,
    ASSET_TYPE_SOUND = 2,
} AssetType;

// Generated catalogs expose only immutable POD views. Logical paths own AssetId;
// baked paths name the corresponding file beneath the configured baked root.
typedef struct AssetCatalogEntry {
    AssetId    id;
    AssetType  type;
    const char* logical_path;
    const char* baked_path;
} AssetCatalogEntry;

typedef struct AssetCatalog {
    const AssetCatalogEntry* entries;
    uint32_t                 count;
} AssetCatalog;
