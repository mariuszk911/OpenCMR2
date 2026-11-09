// Native copies of archive members with little-endian records, see
// port/member_decode.h.

#include "port/member_decode.h"

#include "FileBuffer.h"
#include "port/cod_disk.h"
#include "port/disk.h"
#include "port/stage_camera_disk.h"
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

struct Format {
    const char *extension;
    DecodeFn decode;
};

const Format kFormats[] = {
    { ".cod", DecodeCod },          // co-driver calls
    { ".tsc", DecodeStageCamera },  // trackside cameras
};

bool HasExtension(const char *name, const char *extension)
{
    size_t length = strlen(name);
    size_t extensionLength = strlen(extension);
    return length >= extensionLength && strcmp(name + length - extensionLength, extension) == 0;
}

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
