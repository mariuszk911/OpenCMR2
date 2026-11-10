// Native copies of archive members with little-endian records, see
// port/member_decode.h.

#include "port/member_decode.h"

#include "FileBuffer.h"
#include "port/car_info_disk.h"
#include "port/cod_disk.h"
#include "port/disk.h"
#include "port/light_preset_disk.h"
#include "port/stage_camera_disk.h"
#include "port/stage_light_transform_disk.h"
#include "port/start_table_disk.h"
#include "port/sys.h"

#include <string.h>

namespace {

// A decoder writes size bytes of native data from the file bytes; true if the
// member is well formed.
typedef bool (*DecodeFn)(void *native, size_t size, const void *file);

bool DecodeCod(void *native, size_t size, const void *file)
{
    return CodDisk_Decode(native, size, file, size, NULL) != 0;
}

bool DecodeStageCamera(void *native, size_t size, const void *file)
{
    return StageCameraDisk_Decode(native, size, file, size, NULL) != 0;
}

bool DecodeStartTable(void *native, size_t size, const void *file)
{
    return StartTableDisk_Decode(native, size, file, size, NULL) != 0;
}

bool DecodeStageLightTransform(void *native, size_t size, const void *file)
{
    return StageLightTransformDisk_Decode(native, size, file, size) != 0;
}

bool DecodeLightPreset(void *native, size_t size, const void *file)
{
    return LightPresetDisk_Decode(native, size, file, size) != 0;
}

struct Format {
    const char *extension;
    DecodeFn decode;
};

const Format kFormats[] = {
    { ".cod", DecodeCod },          // co-driver calls
    { ".tsc", DecodeStageCamera },  // trackside cameras
    { ".csp", DecodeStartTable },   // car start positions and angles
    { ".bsp", DecodeStageLightTransform }, // stage light placement
    { ".hor", DecodeLightPreset },  // weather lighting presets
};

bool HasExtension(const char *name, const char *extension)
{
    size_t length = strlen(name);
    size_t extensionLength = strlen(extension);
    size_t i;

    if (length < extensionLength)
        return false;
    for (i = 0; i < extensionLength; i++) {
        char c = name[length - extensionLength + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != extension[i])
            return false;
    }
    return true;
}

// Standalone files are decoded where they lie.
typedef bool (*InPlaceFn)(void *data, size_t size);

bool DecodeCarInfoInPlace(void *data, size_t size)
{
    return CarInfoDisk_Decode(data, size, data, size) != 0;
}

const struct {
    const char *extension;
    InPlaceFn decode;
} kFiles[] = {
    { ".cin", DecodeCarInfoInPlace },   // car info sections
};

} // namespace

extern "C" void *MemberDecode_Find(const char *name, void *data, DWORD size)
{
    if (name == NULL || data == NULL)
        return data;
    for (const Format &format : kFormats) {
        if (!HasExtension(name, format.extension))
            continue;
        if (size == 0)
            return NULL;
        void *native = CFileBuffer::AllocateLockedBuffer((size_t)size + 1);
        if (native == NULL)
            return NULL;
        if (!format.decode(native, size, data)) {
            Sys_Log("member %s: rejected (%u bytes)", name, (unsigned)size);
            CFileBuffer::FreeGenericFileBuffer(native);
            return NULL;
        }
        Disk_AttachToBuffer(data, native);
        return native;
    }
    return data;
}

extern "C" int FileDecode_InPlace(const char *name, void *data, DWORD size)
{
    if (name == NULL || data == NULL)
        return 1;
    for (const auto &file : kFiles) {
        if (!HasExtension(name, file.extension))
            continue;
        if (size == 0 || !file.decode(data, size)) {
            Sys_Log("file %s: rejected (%u bytes)", name, (unsigned)size);
            return 0;
        }
        return 1;
    }
    return 1;
}
