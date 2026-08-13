#include "test.h"

#include "assets/asset_id.h"

#include <cstring>

TEST(asset_id, canonical_literal_matches_runtime_normalization) {
    constexpr AssetId literal = asset_id("textures/uv_test.tga");
    static_assert(literal != ASSET_ID_NULL, "canonical asset literal must hash");

    AssetId runtime = ASSET_ID_NULL;
    CHECK(asset_id_from_path("Textures\\.\\UV_TEST.TGA", &runtime));
    CHECK(runtime == literal);

    char normalized[ASSET_PATH_MAX];
    size_t length = 0u;
    CHECK(!asset_path_normalize("textures//ui/../bad", normalized,
                                sizeof(normalized), &length));
    CHECK(asset_path_normalize("Textures\\//UI/./Icon.TGA/", normalized,
                               sizeof(normalized), &length));
    CHECK(std::strcmp(normalized, "textures/ui/icon.tga") == 0);
    CHECK(length == std::strlen("textures/ui/icon.tga"));
}

TEST(asset_id, invalid_paths_fail_without_touching_outputs) {
    const char* invalid[] = {
        "", "/absolute.tga", "\\absolute.tga", "C:/drive.tga",
        "../escape.tga", "safe/../../escape.tga", "safe/control\x01.tga",
    };
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char normalized[32] = "untouched";
        size_t length = 77u;
        AssetId id = 0x1122334455667788ULL;
        CHECK(!asset_path_normalize(invalid[i], normalized, sizeof(normalized), &length));
        CHECK(std::strcmp(normalized, "untouched") == 0);
        CHECK(length == 77u);
        CHECK(!asset_id_from_path(invalid[i], &id));
        CHECK(id == 0x1122334455667788ULL);
    }

    char tiny[4] = "old";
    size_t tiny_length = 9u;
    CHECK(!asset_path_normalize("long/path.tga", tiny, sizeof(tiny), &tiny_length));
    CHECK(std::strcmp(tiny, "old") == 0 && tiny_length == 9u);
    CHECK(asset_id("Not/Canonical.TGA") == ASSET_ID_NULL);
    CHECK(asset_id("bad//path.tga") == ASSET_ID_NULL);
}
