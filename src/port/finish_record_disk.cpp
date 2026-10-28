// fin.dat finish-record decoder, see port/finish_record_disk.h.

#include "port/finish_record_disk.h"

#include "port/types.h"
#include "port/disk_layouts.h"

#include <stddef.h>

extern "C" int FinishRecordDisk_Decode(void *nativeRecords, const void *file, size_t size,
                                       unsigned int *outCount)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)nativeRecords;
    size_t count;
    size_t i;

    if (nativeRecords == NULL || file == NULL || size == 0 ||
            size % FIN_RECORD_DISK_SIZE != 0)
        return 0;
    count = size / FIN_RECORD_DISK_SIZE;
    for (i = 0; i < count; i++) {
        const unsigned char *record = src + i * FIN_RECORD_DISK_SIZE;
        FinRecord *native = (FinRecord *)(dst + i * sizeof(FinRecord));

        Disk_Unpack(native, record, &g_diskLayout_FinRecord);
        // +0x28 is the sector index in the file; pNext stays NULL until
        // Sector_RebuildStageGrid links the records.
        native->sector = (int)Disk_Offset(record, 0x28);
    }
    if (outCount != NULL)
        *outCount = (unsigned int)count;
    return 1;
}
