#include "shared_frame_channel.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t sfc_slot_region_size(void)
{
    return sizeof(sfc_slot_header_t) + SFC_SLOT_DATA_BYTES;
}

size_t sfc_shared_memory_size(void)
{
    return sizeof(sfc_header_t) + SFC_SLOT_COUNT * sfc_slot_region_size();
}

static int sfc_open_shm(const char* shm_name, int create, int* shm_fd, void** mapping, size_t mapping_size)
{
    const int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    *shm_fd = shm_open(shm_name, flags, 0666);
    if (*shm_fd < 0)
    {
        return -1;
    }

    if (create)
    {
        if (ftruncate(*shm_fd, (off_t)mapping_size) != 0)
        {
            close(*shm_fd);
            *shm_fd = -1;
            return -1;
        }
    }

    *mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, *shm_fd, 0);
    if (*mapping == MAP_FAILED)
    {
        close(*shm_fd);
        *shm_fd = -1;
        *mapping = NULL;
        return -1;
    }

    return 0;
}

static void sfc_map_slots(uint8_t* base, uint8_t** slots)
{
    uint8_t* cursor = base;
    for (uint32_t i = 0; i < SFC_SLOT_COUNT; ++i)
    {
        slots[i] = cursor;
        cursor += sfc_slot_region_size();
    }
}

static void sfc_map_slots_const(const uint8_t* base, const uint8_t** slots)
{
    const uint8_t* cursor = base;
    for (uint32_t i = 0; i < SFC_SLOT_COUNT; ++i)
    {
        slots[i] = cursor;
        cursor += sfc_slot_region_size();
    }
}

int sfc_producer_open(sfc_producer_t* producer, const char* shm_name, int create)
{
    if (!producer || !shm_name)
    {
        return -1;
    }

    memset(producer, 0, sizeof(*producer));
    strncpy(producer->shm_name, shm_name, sizeof(producer->shm_name) - 1);

    producer->mapping_size = sfc_shared_memory_size();
    if (sfc_open_shm(producer->shm_name, create, &producer->shm_fd, &producer->mapping,
                     producer->mapping_size) != 0)
    {
        return -1;
    }

    producer->header = (sfc_header_t*)producer->mapping;
    sfc_map_slots((uint8_t*)producer->mapping + sizeof(sfc_header_t), producer->slot_data);

    if (create)
    {
        memset(producer->mapping, 0, producer->mapping_size);
        producer->header->magic = SFC_MAGIC;
        producer->header->version = SFC_VERSION;
        producer->header->slot_count = SFC_SLOT_COUNT;
        producer->header->max_width = SFC_MAX_WIDTH;
        producer->header->max_height = SFC_MAX_HEIGHT;
        producer->header->bytes_per_pixel = SFC_BYTES_PER_PIXEL;
        producer->header->frame_sequence = 0;
        producer->header->latest_slot = 0;
    }
    else if (producer->header->magic != SFC_MAGIC)
    {
        sfc_producer_close(producer);
        return -1;
    }

    producer->write_slot = 0;
    return 0;
}

void sfc_producer_close(sfc_producer_t* producer)
{
    if (!producer)
    {
        return;
    }

    if (producer->mapping && producer->mapping != MAP_FAILED)
    {
        munmap(producer->mapping, producer->mapping_size);
    }

    if (producer->shm_fd >= 0)
    {
        close(producer->shm_fd);
    }

    memset(producer, 0, sizeof(*producer));
    producer->shm_fd = -1;
}

int sfc_producer_publish_bgr24(sfc_producer_t* producer,
                               const uint8_t* bgr_data,
                               uint32_t width,
                               uint32_t height,
                               uint32_t stride)
{
    if (!producer || !producer->header || !bgr_data || width == 0 || height == 0)
    {
        return -1;
    }

    if (width > SFC_MAX_WIDTH || height > SFC_MAX_HEIGHT)
    {
        return -1;
    }

    const uint32_t required_stride = width * SFC_BYTES_PER_PIXEL;
    if (stride < required_stride)
    {
        return -1;
    }

    const uint32_t data_bytes = required_stride * height;
    if (data_bytes > SFC_SLOT_DATA_BYTES)
    {
        return -1;
    }

    const uint32_t slot_index = producer->write_slot % SFC_SLOT_COUNT;
    sfc_slot_header_t* slot_header = (sfc_slot_header_t*)producer->slot_data[slot_index];
    uint8_t* slot_pixels = producer->slot_data[slot_index] + sizeof(sfc_slot_header_t);

    slot_header->width = width;
    slot_header->height = height;
    slot_header->stride = required_stride;
    slot_header->data_bytes = data_bytes;

    for (uint32_t y = 0; y < height; ++y)
    {
        memcpy(slot_pixels + y * required_stride, bgr_data + y * stride, required_stride);
    }

    producer->header->latest_slot = slot_index;
    producer->header->frame_sequence += 1U;
    producer->write_slot = (slot_index + 1U) % SFC_SLOT_COUNT;
    return 0;
}

int sfc_consumer_open(sfc_consumer_t* consumer, const char* shm_name)
{
    if (!consumer || !shm_name)
    {
        return -1;
    }

    memset(consumer, 0, sizeof(*consumer));
    strncpy(consumer->shm_name, shm_name, sizeof(consumer->shm_name) - 1);

    consumer->mapping_size = sfc_shared_memory_size();
    int shm_fd = -1;
    void* mapping = NULL;
    if (sfc_open_shm(consumer->shm_name, 0, &shm_fd, &mapping, consumer->mapping_size) != 0)
    {
        return -1;
    }

    consumer->shm_fd = shm_fd;
    consumer->mapping = mapping;
    consumer->header = (const sfc_header_t*)mapping;
    sfc_map_slots_const((const uint8_t*)mapping + sizeof(sfc_header_t), consumer->slot_data);

    if (consumer->header->magic != SFC_MAGIC)
    {
        sfc_consumer_close(consumer);
        return -1;
    }

    consumer->last_sequence = consumer->header->frame_sequence;
    return 0;
}

void sfc_consumer_close(sfc_consumer_t* consumer)
{
    if (!consumer)
    {
        return;
    }

    if (consumer->mapping && consumer->mapping != MAP_FAILED)
    {
        munmap(consumer->mapping, consumer->mapping_size);
    }

    if (consumer->shm_fd >= 0)
    {
        close(consumer->shm_fd);
    }

    memset(consumer, 0, sizeof(*consumer));
    consumer->shm_fd = -1;
}

int sfc_consumer_read_latest_bgr24(sfc_consumer_t* consumer,
                                     uint8_t* bgr_out,
                                     size_t bgr_out_capacity,
                                     uint32_t* width,
                                     uint32_t* height,
                                     uint32_t* stride)
{
    if (!consumer || !consumer->header || !bgr_out || !width || !height || !stride)
    {
        return -1;
    }

    const uint64_t sequence = consumer->header->frame_sequence;
    if (sequence == 0 || sequence == consumer->last_sequence)
    {
        return 0;
    }

    const uint32_t slot_index = consumer->header->latest_slot % SFC_SLOT_COUNT;
    const sfc_slot_header_t* slot_header =
        (const sfc_slot_header_t*)consumer->slot_data[slot_index];
    const uint8_t* slot_pixels = consumer->slot_data[slot_index] + sizeof(sfc_slot_header_t);

    if (slot_header->data_bytes == 0 || slot_header->data_bytes > bgr_out_capacity)
    {
        return -1;
    }

  *width = slot_header->width;
  *height = slot_header->height;
  *stride = slot_header->stride;
    memcpy(bgr_out, slot_pixels, slot_header->data_bytes);
    consumer->last_sequence = sequence;
    return 1;
}
