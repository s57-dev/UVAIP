#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SFC_MAGIC 0xCAFEF00DU
#define SFC_VERSION 1U
#define SFC_DEFAULT_SHM_NAME "camera_app_vlc_frames"
#define SFC_SLOT_COUNT 2U
#define SFC_MAX_WIDTH 1920U
#define SFC_MAX_HEIGHT 1080U
#define SFC_BYTES_PER_PIXEL 3U
#define SFC_SLOT_DATA_BYTES (SFC_MAX_WIDTH * SFC_MAX_HEIGHT * SFC_BYTES_PER_PIXEL)

typedef struct sfc_header
{
    uint32_t magic;
    uint32_t version;
    uint32_t slot_count;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t bytes_per_pixel;
    volatile uint64_t frame_sequence;
    volatile uint32_t latest_slot;
} sfc_header_t;

typedef struct sfc_slot_header
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t data_bytes;
} sfc_slot_header_t;

typedef struct sfc_producer
{
    int shm_fd;
    void* mapping;
    size_t mapping_size;
    sfc_header_t* header;
    uint8_t* slot_data[SFC_SLOT_COUNT];
    uint32_t write_slot;
    char shm_name[256];
} sfc_producer_t;

typedef struct sfc_consumer
{
    int shm_fd;
    void* mapping;
    size_t mapping_size;
    const sfc_header_t* header;
    const uint8_t* slot_data[SFC_SLOT_COUNT];
    uint64_t last_sequence;
    char shm_name[256];
} sfc_consumer_t;

size_t sfc_shared_memory_size(void);

int sfc_producer_open(sfc_producer_t* producer, const char* shm_name, int create);
void sfc_producer_close(sfc_producer_t* producer);

int sfc_producer_publish_bgr24(sfc_producer_t* producer,
                               const uint8_t* bgr_data,
                               uint32_t width,
                               uint32_t height,
                               uint32_t stride);

int sfc_consumer_open(sfc_consumer_t* consumer, const char* shm_name);
void sfc_consumer_close(sfc_consumer_t* consumer);

int sfc_consumer_read_latest_bgr24(sfc_consumer_t* consumer,
                                   uint8_t* bgr_out,
                                   size_t bgr_out_capacity,
                                   uint32_t* width,
                                   uint32_t* height,
                                   uint32_t* stride);

#ifdef __cplusplus
}
#endif
