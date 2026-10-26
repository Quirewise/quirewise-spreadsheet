#include "ole_compound.h"

#include <stdlib.h>
#include <string.h>

enum {
    OLE_HEADER_BYTES = 512u,
    OLE_DIRECTORY_ENTRY_BYTES = 128u,
    OLE_MINI_SECTOR_BYTES = 64u,
    OLE_DIFAT_HEADER_ENTRIES = 109u,
    OLE_MAX_DIRECTORY_BYTES = 16u * 1024u * 1024u,
    OLE_MAX_MINISTREAM_BYTES = 256u * 1024u * 1024u
};

#define OLE_FREE_SECTOR UINT32_C(0xffffffff)
#define OLE_END_OF_CHAIN UINT32_C(0xfffffffe)
#define OLE_FAT_SECTOR UINT32_C(0xfffffffd)
#define OLE_DIFAT_SECTOR UINT32_C(0xfffffffc)

typedef struct OleCompound {
    const uint8_t *data;
    size_t size;
    size_t sector_size;
    size_t sector_count;
    uint32_t *fat;
    size_t fat_entry_count;
    uint8_t *sector_marks;
} OleCompound;

static uint16_t ole_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ole_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t ole_u64(const uint8_t *p)
{
    return (uint64_t)ole_u32(p) | ((uint64_t)ole_u32(p + 4u) << 32);
}

int qw_ole_compound_has_signature(const uint8_t *data, size_t size)
{
    static const uint8_t signature[8] = {
        0xd0u, 0xcfu, 0x11u, 0xe0u, 0xa1u, 0xb1u, 0x1au, 0xe1u
    };
    return data && size >= sizeof(signature) &&
           memcmp(data, signature, sizeof(signature)) == 0;
}

static void ole_drop(OleCompound *compound)
{
    if (!compound) return;
    free(compound->fat);
    free(compound->sector_marks);
    memset(compound, 0, sizeof(*compound));
}

static int ole_sector(const OleCompound *compound, uint32_t sector_id,
                      const uint8_t **out_sector)
{
    size_t offset;
    if (!compound || !out_sector ||
        sector_id == OLE_FREE_SECTOR || sector_id >= compound->sector_count ||
        (size_t)sector_id > SIZE_MAX / compound->sector_size - 1u) {
        return 0;
    }
    offset = ((size_t)sector_id + 1u) * compound->sector_size;
    if (offset > compound->size ||
        compound->sector_size > compound->size - offset) return 0;
    *out_sector = compound->data + offset;
    return 1;
}

static int ole_read_chain(OleCompound *compound, const uint32_t *table,
                          size_t table_count, size_t chain_limit,
                          uint32_t first_sector, uint8_t *output,
                          size_t output_size, size_t *out_written)
{
    uint32_t sector_id = first_sector;
    size_t written = 0u;
    size_t steps = 0u;
    if (out_written) *out_written = 0u;
    if (!compound || !table || !output || !out_written ||
        first_sector >= table_count || !chain_limit) return 0;
    memset(compound->sector_marks, 0, compound->sector_count);
    while (sector_id != OLE_END_OF_CHAIN) {
        const uint8_t *sector;
        uint32_t next;
        size_t copy_size;
        if (sector_id >= table_count || sector_id >= chain_limit ||
            sector_id >= compound->sector_count ||
            compound->sector_marks[sector_id] ||
            !ole_sector(compound, sector_id, &sector) ||
            steps++ >= chain_limit) return 0;
        compound->sector_marks[sector_id] = 1u;
        next = table[sector_id];
        copy_size = output_size - written;
        if (copy_size > compound->sector_size)
            copy_size = compound->sector_size;
        if (copy_size) memcpy(output + written, sector, copy_size);
        written += copy_size;
        if (written == output_size) {
            *out_written = written;
            return 1;
        }
        if (next == OLE_FREE_SECTOR || next == OLE_FAT_SECTOR ||
            next == OLE_DIFAT_SECTOR) return 0;
        sector_id = next;
    }
    *out_written = written;
    return written == output_size;
}

static int ole_read_chain_dynamic(OleCompound *compound, uint32_t first_sector,
                                  size_t max_bytes, uint8_t **out_data,
                                  size_t *out_size)
{
    uint32_t sector_id = first_sector;
    uint8_t *buffer = NULL;
    size_t capacity = 0u;
    size_t size = 0u;
    size_t steps = 0u;
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0u;
    if (!compound || !out_data || !out_size ||
        first_sector >= compound->fat_entry_count || !max_bytes) return 0;
    memset(compound->sector_marks, 0, compound->sector_count);
    while (sector_id != OLE_END_OF_CHAIN) {
        const uint8_t *sector;
        uint32_t next;
        size_t required;
        size_t grown_capacity;
        uint8_t *grown;
        if (sector_id >= compound->fat_entry_count ||
            sector_id >= compound->sector_count ||
            compound->sector_marks[sector_id] ||
            !ole_sector(compound, sector_id, &sector) ||
            steps++ >= compound->sector_count) {
            free(buffer);
            return 0;
        }
        compound->sector_marks[sector_id] = 1u;
        if (compound->sector_size > max_bytes - size) {
            free(buffer);
            return 0;
        }
        required = size + compound->sector_size;
        if (required > capacity) {
            grown_capacity = capacity ? capacity : compound->sector_size;
            while (grown_capacity < required) {
                if (grown_capacity > max_bytes / 2u) {
                    grown_capacity = max_bytes;
                    break;
                }
                grown_capacity *= 2u;
            }
            grown = (uint8_t *)realloc(buffer, grown_capacity);
            if (!grown) {
                free(buffer);
                return 0;
            }
            buffer = grown;
            capacity = grown_capacity;
        }
        memcpy(buffer + size, sector, compound->sector_size);
        size += compound->sector_size;
        next = compound->fat[sector_id];
        if (next == OLE_FREE_SECTOR || next == OLE_FAT_SECTOR ||
            next == OLE_DIFAT_SECTOR) {
            free(buffer);
            return 0;
        }
        sector_id = next;
    }
    if (!size) {
        free(buffer);
        return 0;
    }
    *out_data = buffer;
    *out_size = size;
    return 1;
}

static int ole_init(OleCompound *compound, const uint8_t *data, size_t size)
{
    uint16_t sector_shift;
    uint16_t mini_sector_shift;
    uint32_t num_fat_sectors;
    uint32_t first_directory_sector;
    uint32_t mini_stream_cutoff;
    uint32_t first_mini_fat_sector;
    uint32_t num_mini_fat_sectors;
    uint32_t first_difat_sector;
    uint32_t num_difat_sectors;
    uint32_t *fat_sector_ids = NULL;
    size_t fat_sector_count = 0u;
    size_t entries_per_fat_sector;
    size_t i;
    int ok = 0;
    memset(compound, 0, sizeof(*compound));
    if (!qw_ole_compound_has_signature(data, size) ||
        size < OLE_HEADER_BYTES) return 0;
    if (ole_u16(data + 28u) != 0xfffeu) return 0;
    sector_shift = ole_u16(data + 30u);
    mini_sector_shift = ole_u16(data + 32u);
    if ((sector_shift != 9u && sector_shift != 12u) ||
        mini_sector_shift != 6u) return 0;
    compound->sector_size = (size_t)1u << sector_shift;
    if (size < compound->sector_size + OLE_HEADER_BYTES ||
        size % compound->sector_size != 0u) return 0;
    compound->data = data;
    compound->size = size;
    compound->sector_count = size / compound->sector_size - 1u;
    if (!compound->sector_count) return 0;
    compound->sector_marks = (uint8_t *)calloc(compound->sector_count, 1u);
    if (!compound->sector_marks) goto cleanup;

    num_fat_sectors = ole_u32(data + 44u);
    first_directory_sector = ole_u32(data + 48u);
    mini_stream_cutoff = ole_u32(data + 56u);
    first_mini_fat_sector = ole_u32(data + 60u);
    num_mini_fat_sectors = ole_u32(data + 64u);
    first_difat_sector = ole_u32(data + 68u);
    num_difat_sectors = ole_u32(data + 72u);
    if (!num_fat_sectors || num_fat_sectors > compound->sector_count ||
        num_difat_sectors > compound->sector_count ||
        first_directory_sector >= compound->sector_count ||
        mini_stream_cutoff != 4096u ||
        (num_mini_fat_sectors &&
         first_mini_fat_sector >= compound->sector_count)) goto cleanup;
    fat_sector_ids = (uint32_t *)malloc(
        (size_t)num_fat_sectors * sizeof(*fat_sector_ids));
    if (!fat_sector_ids) goto cleanup;
    for (i = 0u; i < OLE_DIFAT_HEADER_ENTRIES &&
                fat_sector_count < num_fat_sectors; ++i) {
        uint32_t sector_id = ole_u32(data + 76u + i * 4u);
        if (sector_id == OLE_FREE_SECTOR) continue;
        if (sector_id >= compound->sector_count) goto cleanup;
        fat_sector_ids[fat_sector_count++] = sector_id;
    }
    if (num_difat_sectors) {
        uint32_t difat_sector = first_difat_sector;
        size_t difat_entries = compound->sector_size / 4u - 1u;
        for (i = 0u; i < num_difat_sectors; ++i) {
            const uint8_t *sector;
            size_t j;
            if (difat_sector >= compound->sector_count ||
                !ole_sector(compound, difat_sector, &sector) ||
                compound->sector_marks[difat_sector]) goto cleanup;
            compound->sector_marks[difat_sector] = 1u;
            for (j = 0u; j < difat_entries &&
                        fat_sector_count < num_fat_sectors; ++j) {
                uint32_t fat_sector = ole_u32(sector + j * 4u);
                if (fat_sector == OLE_FREE_SECTOR) continue;
                if (fat_sector >= compound->sector_count) goto cleanup;
                fat_sector_ids[fat_sector_count++] = fat_sector;
            }
            difat_sector = ole_u32(sector + difat_entries * 4u);
            if (i + 1u < num_difat_sectors &&
                difat_sector >= compound->sector_count) goto cleanup;
        }
        memset(compound->sector_marks, 0, compound->sector_count);
    }
    if (fat_sector_count != num_fat_sectors) goto cleanup;
    entries_per_fat_sector = compound->sector_size / 4u;
    if ((size_t)num_fat_sectors >
        SIZE_MAX / entries_per_fat_sector / sizeof(*compound->fat)) goto cleanup;
    compound->fat_entry_count =
        (size_t)num_fat_sectors * entries_per_fat_sector;
    compound->fat = (uint32_t *)malloc(
        compound->fat_entry_count * sizeof(*compound->fat));
    if (!compound->fat) goto cleanup;
    for (i = 0u; i < num_fat_sectors; ++i) {
        const uint8_t *sector;
        size_t j;
        if (!ole_sector(compound, fat_sector_ids[i], &sector)) goto cleanup;
        for (j = 0u; j < entries_per_fat_sector; ++j) {
            compound->fat[i * entries_per_fat_sector + j] =
                ole_u32(sector + j * 4u);
        }
    }
    if (compound->fat_entry_count < compound->sector_count ||
        first_directory_sector >= compound->fat_entry_count) goto cleanup;
    compound->sector_marks[first_directory_sector] = 0u;
    ok = 1;

cleanup:
    free(fat_sector_ids);
    if (!ok) ole_drop(compound);
    return ok;
}

static int ole_read_normal_stream(OleCompound *compound, uint32_t first_sector,
                                  uint64_t stream_size, size_t max_prefix,
                                  uint8_t **out_data, size_t *out_size)
{
    size_t wanted;
    uint8_t *buffer;
    size_t written = 0u;
    if (stream_size > SIZE_MAX) return 0;
    wanted = (size_t)stream_size;
    if (wanted > max_prefix) wanted = max_prefix;
    if (!wanted) return 0;
    buffer = (uint8_t *)malloc(wanted);
    if (!buffer) return 0;
    if (!ole_read_chain(compound, compound->fat,
                        compound->fat_entry_count,
                        compound->sector_count, first_sector,
                        buffer, wanted, &written) || written != wanted) {
        free(buffer);
        return 0;
    }
    *out_data = buffer;
    *out_size = wanted;
    return 1;
}

static int ole_read_mini_stream(OleCompound *compound,
                                const uint32_t *mini_fat,
                                size_t mini_fat_count,
                                const uint8_t *mini_stream,
                                size_t mini_stream_size,
                                uint32_t first_mini_sector,
                                uint64_t stream_size,
                                size_t max_prefix,
                                uint8_t **out_data,
                                size_t *out_size)
{
    size_t wanted;
    uint32_t mini_sector = first_mini_sector;
    uint8_t *buffer;
    size_t written = 0u;
    size_t steps = 0u;
    uint8_t *marks;
    if (stream_size > SIZE_MAX || !mini_fat_count ||
        first_mini_sector >= mini_fat_count) return 0;
    wanted = (size_t)stream_size;
    if (wanted > max_prefix) wanted = max_prefix;
    if (!wanted) return 0;
    buffer = (uint8_t *)malloc(wanted);
    marks = (uint8_t *)calloc(mini_fat_count, 1u);
    if (!buffer || !marks) {
        free(buffer);
        free(marks);
        return 0;
    }
    while (written < wanted) {
        size_t offset;
        size_t copy_size;
        uint32_t next;
        if (mini_sector >= mini_fat_count || marks[mini_sector] ||
            steps++ >= mini_fat_count) {
            free(buffer);
            free(marks);
            return 0;
        }
#if SIZE_MAX <= UINT32_MAX
        if (mini_sector > (uint32_t)(SIZE_MAX / OLE_MINI_SECTOR_BYTES)) {
            free(buffer);
            free(marks);
            return 0;
        }
#endif
        marks[mini_sector] = 1u;
        offset = (size_t)mini_sector * OLE_MINI_SECTOR_BYTES;
        if (offset > mini_stream_size ||
            OLE_MINI_SECTOR_BYTES > mini_stream_size - offset) {
            free(buffer);
            free(marks);
            return 0;
        }
        copy_size = wanted - written;
        if (copy_size > OLE_MINI_SECTOR_BYTES)
            copy_size = OLE_MINI_SECTOR_BYTES;
        memcpy(buffer + written, mini_stream + offset, copy_size);
        written += copy_size;
        next = mini_fat[mini_sector];
        if (written == wanted) break;
        if (next == OLE_FREE_SECTOR || next == OLE_FAT_SECTOR ||
            next == OLE_DIFAT_SECTOR || next == OLE_END_OF_CHAIN) {
            free(buffer);
            free(marks);
            return 0;
        }
        mini_sector = next;
    }
    free(marks);
    *out_data = buffer;
    *out_size = written;
    (void)compound;
    return written == wanted;
}

static int ole_entry_name_matches(const uint8_t *entry, const char *name)
{
    size_t name_bytes = ole_u16(entry + 64u);
    size_t wanted_length;
    size_t i;
    if (name_bytes < 2u || name_bytes > 64u || (name_bytes & 1u) != 0u)
        return 0;
    wanted_length = strlen(name);
    if (name_bytes / 2u != wanted_length + 1u ||
        ole_u16(entry + name_bytes - 2u) != 0u) return 0;
    for (i = 0u; i < wanted_length; ++i) {
        uint16_t ch = ole_u16(entry + i * 2u);
        unsigned char wanted = (unsigned char)name[i];
        if (ch > 0x7fu) return 0;
        if (ch >= 'A' && ch <= 'Z') ch = (uint16_t)(ch + ('a' - 'A'));
        if (wanted >= 'A' && wanted <= 'Z')
            wanted = (unsigned char)(wanted + ('a' - 'A'));
        if (ch != wanted) return 0;
    }
    return 1;
}

int qw_ole_compound_read_stream(const uint8_t *data,
                                size_t size,
                                const char *stream_name,
                                size_t max_prefix,
                                uint8_t **out_data,
                                size_t *out_size,
                                uint64_t *out_stream_size)
{
    OleCompound compound;
    uint8_t *directory = NULL;
    size_t directory_size = 0u;
    uint8_t *mini_fat_data = NULL;
    size_t mini_fat_bytes = 0u;
    uint32_t *mini_fat = NULL;
    size_t mini_fat_count = 0u;
    uint8_t *mini_stream = NULL;
    size_t mini_stream_size = 0u;
    uint32_t root_start = OLE_END_OF_CHAIN;
    uint64_t root_size = 0u;
    uint32_t target_start = OLE_END_OF_CHAIN;
    uint64_t target_size = 0u;
    uint32_t first_directory_sector;
    uint32_t first_mini_fat_sector;
    uint32_t num_mini_fat_sectors;
    uint32_t mini_stream_cutoff;
    size_t i;
    int found_root = 0;
    int found_target = 0;
    int ok = 0;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0u;
    if (out_stream_size) *out_stream_size = 0u;
    if (!data || !stream_name || !max_prefix || !out_data || !out_size ||
        !out_stream_size || !ole_init(&compound, data, size)) return 0;
    first_directory_sector = ole_u32(data + 48u);
    mini_stream_cutoff = ole_u32(data + 56u);
    first_mini_fat_sector = ole_u32(data + 60u);
    num_mini_fat_sectors = ole_u32(data + 64u);
    if (!ole_read_chain_dynamic(&compound, first_directory_sector,
                                OLE_MAX_DIRECTORY_BYTES,
                                &directory, &directory_size) ||
        directory_size % OLE_DIRECTORY_ENTRY_BYTES != 0u) goto cleanup;

    for (i = 0u; i < directory_size; i += OLE_DIRECTORY_ENTRY_BYTES) {
        const uint8_t *entry = directory + i;
        uint8_t object_type = entry[66u];
        if (object_type == 5u && !found_root) {
            root_start = ole_u32(entry + 116u);
            root_size = ole_u64(entry + 120u);
            found_root = 1;
        } else if (object_type == 2u &&
                   ole_entry_name_matches(entry, stream_name)) {
            target_start = ole_u32(entry + 116u);
            target_size = ole_u64(entry + 120u);
            found_target = 1;
            break;
        }
    }
    if (!found_root || !found_target || !target_size ||
        target_size > SIZE_MAX) goto cleanup;
    *out_stream_size = target_size;
    if (target_size >= mini_stream_cutoff) {
        ok = ole_read_normal_stream(&compound, target_start, target_size,
                                    max_prefix, out_data, out_size);
        goto cleanup;
    }
    if (!num_mini_fat_sectors || root_size > OLE_MAX_MINISTREAM_BYTES ||
        root_size > SIZE_MAX ||
        (size_t)num_mini_fat_sectors >
            SIZE_MAX / compound.sector_size) goto cleanup;
    mini_fat_bytes = (size_t)num_mini_fat_sectors * compound.sector_size;
    if (mini_fat_bytes % sizeof(uint32_t) != 0u) goto cleanup;
    if (!ole_read_normal_stream(&compound, first_mini_fat_sector,
                                mini_fat_bytes, mini_fat_bytes,
                                &mini_fat_data, &mini_fat_bytes)) goto cleanup;
    mini_fat_count = mini_fat_bytes / sizeof(uint32_t);
    mini_fat = (uint32_t *)malloc(mini_fat_count * sizeof(*mini_fat));
    if (!mini_fat) goto cleanup;
    for (i = 0u; i < mini_fat_count; ++i)
        mini_fat[i] = ole_u32(mini_fat_data + i * 4u);
    if (root_size) {
        if (!ole_read_normal_stream(&compound, root_start, root_size,
                                    (size_t)root_size, &mini_stream,
                                    &mini_stream_size)) goto cleanup;
    }
    ok = ole_read_mini_stream(&compound, mini_fat, mini_fat_count,
                              mini_stream, mini_stream_size, target_start,
                              target_size, max_prefix, out_data, out_size);

cleanup:
    free(directory);
    free(mini_fat_data);
    free(mini_fat);
    free(mini_stream);
    ole_drop(&compound);
    if (!ok) {
        free(*out_data);
        *out_data = NULL;
        *out_size = 0u;
        *out_stream_size = 0u;
    }
    return ok;
}
