// Native copies of file records, see port/disk.h.

#include "port/disk.h"
#include "port/types.h"
#include "port/sys.h"

#include <stddef.h>
#include <stdlib.h>

#include <vector>

namespace {

struct Attachment {
    const char *fileData;
    void *native;
    void *owner;
};

struct BufferRecord {
    void *buffer;
    size_t size;
};

// Few at a time: one per loaded stage mesh file, and one per open file buffer.
std::vector<Attachment> s_attachments;
std::vector<BufferRecord> s_buffers;

size_t FindBuffer(void *buffer)
{
    for (size_t i = 0; i < s_buffers.size(); i++) {
        if (s_buffers[i].buffer == buffer)
            return i;
    }
    return s_buffers.size();
}

} // namespace

void Disk_Unpack(void *native, const void *record, const DiskLayout *layout)
{
    memset(native, 0, layout->nativeSize);
    for (int i = 0; i < layout->runCount; i++) {
        const DiskRun &run = layout->runs[i];
        memcpy((char *)native + run.native, (const char *)record + run.disk, run.size);
    }
    for (int i = 0; i < layout->pointerCount; i++)
        memset((char *)native + layout->pointers[i].native, 0, sizeof(void *));
}

void Disk_Pack(void *record, const void *native, const DiskLayout *layout)
{
    memset(record, 0, layout->diskSize);
    for (int i = 0; i < layout->runCount; i++) {
        const DiskRun &run = layout->runs[i];
        memcpy((char *)record + run.disk, (const char *)native + run.native, run.size);
    }
    for (int i = 0; i < layout->pointerCount; i++) {
        uintptr_t pointer;
        memcpy(&pointer, (const char *)native + layout->pointers[i].native, sizeof(pointer));
        uint32_t low = (uint32_t)pointer;
        WriteLE32((char *)record + layout->pointers[i].disk, low);
    }
}

void Disk_AttachToBuffer(const void *fileData, void *native)
{
    const char *data = (const char *)fileData;
    void *owner = NULL;
    size_t ownerSize = 0;
    for (size_t i = 0; i < s_buffers.size(); i++) {
        const char *begin = (const char *)s_buffers[i].buffer;
        if (data >= begin && data < begin + s_buffers[i].size &&
                (owner == NULL || s_buffers[i].size < ownerSize)) {
            owner = s_buffers[i].buffer;
            ownerSize = s_buffers[i].size;
        }
    }
    if (owner == NULL)
        Sys_Log("disk: attachment %p is not inside a registered buffer", fileData);
    s_attachments.push_back({data, native, owner});
}

void Disk_RegisterBuffer(void *buffer, size_t size)
{
    if (buffer == NULL)
        return;
    size_t index = FindBuffer(buffer);
    if (index < s_buffers.size()) {
        s_buffers[index].size = size;
        return;
    }
    s_buffers.push_back({buffer, size});
}

void Disk_ReleaseBuffer(void *buffer)
{
    if (buffer == NULL)
        return;

    for (size_t i = 0; i < s_attachments.size();) {
        if (s_attachments[i].owner == buffer) {
            free(s_attachments[i].native);
            s_attachments[i] = s_attachments.back();
            s_attachments.pop_back();
        } else {
            i++;
        }
    }

    size_t index = FindBuffer(buffer);
    if (index < s_buffers.size()) {
        s_buffers[index] = s_buffers.back();
        s_buffers.pop_back();
    }
}

void Disk_BadOffset(const void *record, unsigned off, uint32_t value)
{
    Sys_Log("disk: record %p field 0x%x holds offset 0x%x outside its array", record, off, (unsigned)value);
}
