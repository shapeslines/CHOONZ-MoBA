#include "serialize/byte_io.h"

#include <cstring>

static bool writer_reserve(ByteWriter* writer, size_t size) {
    if (!writer || !writer->ok) return false;
    if (writer->offset > writer->capacity || size > writer->capacity - writer->offset) {
        writer->ok = false;
        return false;
    }
    return true;
}

static bool reader_reserve(ByteReader* reader, size_t size) {
    if (!reader || !reader->ok) return false;
    if (reader->offset > reader->size || size > reader->size - reader->offset) {
        reader->ok = false;
        return false;
    }
    return true;
}

void byte_writer_init(ByteWriter* writer, void* data, size_t capacity) {
    if (!writer) return;
    writer->data = static_cast<uint8_t*>(data);
    writer->capacity = capacity;
    writer->offset = 0;
    writer->ok = writer->data != nullptr || capacity == 0;
}

bool byte_writer_ok(const ByteWriter* writer) { return writer && writer->ok; }
size_t byte_writer_size(const ByteWriter* writer) { return writer ? writer->offset : 0; }

bool byte_writer_write_bytes(ByteWriter* writer, const void* data, size_t size) {
    if (!writer || !writer->ok) return false;
    if (size > 0 && !data) {
        writer->ok = false;
        return false;
    }
    if (!writer_reserve(writer, size)) return false;
    if (size > 0) std::memcpy(writer->data + writer->offset, data, size);
    writer->offset += size;
    return true;
}

bool byte_writer_write_u8(ByteWriter* writer, uint8_t value) {
    return byte_writer_write_bytes(writer, &value, sizeof(value));
}

bool byte_writer_write_u16(ByteWriter* writer, uint16_t value) {
    uint8_t bytes[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    return byte_writer_write_bytes(writer, bytes, sizeof(bytes));
}

bool byte_writer_write_u32(ByteWriter* writer, uint32_t value) {
    uint8_t bytes[4];
    for (uint32_t i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    return byte_writer_write_bytes(writer, bytes, sizeof(bytes));
}

bool byte_writer_write_u64(ByteWriter* writer, uint64_t value) {
    uint8_t bytes[8];
    for (uint32_t i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    return byte_writer_write_bytes(writer, bytes, sizeof(bytes));
}

bool byte_writer_write_i8(ByteWriter* writer, int8_t value) {
    uint8_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return byte_writer_write_u8(writer, bits);
}

bool byte_writer_write_i16(ByteWriter* writer, int16_t value) {
    uint16_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return byte_writer_write_u16(writer, bits);
}

bool byte_writer_write_i32(ByteWriter* writer, int32_t value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return byte_writer_write_u32(writer, bits);
}

bool byte_writer_write_i64(ByteWriter* writer, int64_t value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return byte_writer_write_u64(writer, bits);
}

void byte_reader_init(ByteReader* reader, const void* data, size_t size) {
    if (!reader) return;
    reader->data = static_cast<const uint8_t*>(data);
    reader->size = size;
    reader->offset = 0;
    reader->ok = reader->data != nullptr || size == 0;
}

bool byte_reader_ok(const ByteReader* reader) { return reader && reader->ok; }

size_t byte_reader_remaining(const ByteReader* reader) {
    if (!reader || reader->offset > reader->size) return 0;
    return reader->size - reader->offset;
}

bool byte_reader_read_bytes(ByteReader* reader, void* out, size_t size) {
    if (!reader || !reader->ok) return false;
    if (size > 0 && !out) {
        reader->ok = false;
        return false;
    }
    if (!reader_reserve(reader, size)) return false;
    if (size > 0) std::memcpy(out, reader->data + reader->offset, size);
    reader->offset += size;
    return true;
}

bool byte_reader_read_u8(ByteReader* reader, uint8_t* out) {
    return byte_reader_read_bytes(reader, out, sizeof(*out));
}

bool byte_reader_read_u16(ByteReader* reader, uint16_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    if (!reader_reserve(reader, 2)) return false;
    const uint8_t* p = reader->data + reader->offset;
    uint16_t value = static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
    *out = value;
    reader->offset += 2;
    return true;
}

bool byte_reader_read_u32(ByteReader* reader, uint32_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    if (!reader_reserve(reader, 4)) return false;
    const uint8_t* p = reader->data + reader->offset;
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(p[i]) << (i * 8);
    *out = value;
    reader->offset += 4;
    return true;
}

bool byte_reader_read_u64(ByteReader* reader, uint64_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    if (!reader_reserve(reader, 8)) return false;
    const uint8_t* p = reader->data + reader->offset;
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (i * 8);
    *out = value;
    reader->offset += 8;
    return true;
}

bool byte_reader_read_i8(ByteReader* reader, int8_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    uint8_t bits = 0;
    if (!byte_reader_read_u8(reader, &bits)) return false;
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}

bool byte_reader_read_i16(ByteReader* reader, int16_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    uint16_t bits = 0;
    if (!byte_reader_read_u16(reader, &bits)) return false;
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}

bool byte_reader_read_i32(ByteReader* reader, int32_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    uint32_t bits = 0;
    if (!byte_reader_read_u32(reader, &bits)) return false;
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}

bool byte_reader_read_i64(ByteReader* reader, int64_t* out) {
    if (!out) {
        if (reader) reader->ok = false;
        return false;
    }
    uint64_t bits = 0;
    if (!byte_reader_read_u64(reader, &bits)) return false;
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}
