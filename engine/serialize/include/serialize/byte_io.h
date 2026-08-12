#pragma once

#include <stddef.h>
#include <stdint.h>

// Bounded, allocation-free little-endian byte I/O. An operation either completes
// fully or leaves the offset and destination bytes untouched. The first failure is
// sticky: every later operation returns false until the object is reinitialized.
typedef struct ByteWriter {
    uint8_t* data;
    size_t capacity;
    size_t offset;
    bool ok;
} ByteWriter;

void   byte_writer_init(ByteWriter* writer, void* data, size_t capacity);
bool   byte_writer_ok(const ByteWriter* writer);
size_t byte_writer_size(const ByteWriter* writer);
bool   byte_writer_write_bytes(ByteWriter* writer, const void* data, size_t size);
bool   byte_writer_write_u8(ByteWriter* writer, uint8_t value);
bool   byte_writer_write_u16(ByteWriter* writer, uint16_t value);
bool   byte_writer_write_u32(ByteWriter* writer, uint32_t value);
bool   byte_writer_write_u64(ByteWriter* writer, uint64_t value);
bool   byte_writer_write_i8(ByteWriter* writer, int8_t value);
bool   byte_writer_write_i16(ByteWriter* writer, int16_t value);
bool   byte_writer_write_i32(ByteWriter* writer, int32_t value);
bool   byte_writer_write_i64(ByteWriter* writer, int64_t value);

typedef struct ByteReader {
    const uint8_t* data;
    size_t size;
    size_t offset;
    bool ok;
} ByteReader;

void   byte_reader_init(ByteReader* reader, const void* data, size_t size);
bool   byte_reader_ok(const ByteReader* reader);
size_t byte_reader_remaining(const ByteReader* reader);
bool   byte_reader_read_bytes(ByteReader* reader, void* out, size_t size);
bool   byte_reader_read_u8(ByteReader* reader, uint8_t* out);
bool   byte_reader_read_u16(ByteReader* reader, uint16_t* out);
bool   byte_reader_read_u32(ByteReader* reader, uint32_t* out);
bool   byte_reader_read_u64(ByteReader* reader, uint64_t* out);
bool   byte_reader_read_i8(ByteReader* reader, int8_t* out);
bool   byte_reader_read_i16(ByteReader* reader, int16_t* out);
bool   byte_reader_read_i32(ByteReader* reader, int32_t* out);
bool   byte_reader_read_i64(ByteReader* reader, int64_t* out);
