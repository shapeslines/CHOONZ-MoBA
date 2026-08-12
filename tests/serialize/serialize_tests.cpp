#include "test.h"
#include "serialize/byte_io.h"

#include <cstring>

TEST(serialize, integer_roundtrip_is_little_endian) {
    uint8_t bytes[30] = {};
    ByteWriter writer;
    byte_writer_init(&writer, bytes, sizeof(bytes));
    CHECK(byte_writer_write_u8(&writer, 0x12u));
    CHECK(byte_writer_write_u16(&writer, 0x3456u));
    CHECK(byte_writer_write_u32(&writer, 0x789abcdeu));
    CHECK(byte_writer_write_u64(&writer, 0x0123456789abcdefULL));
    CHECK(byte_writer_write_i8(&writer, -2));
    CHECK(byte_writer_write_i16(&writer, -3));
    CHECK(byte_writer_write_i32(&writer, -4));
    CHECK(byte_writer_write_i64(&writer, -5));
    CHECK(byte_writer_ok(&writer));
    CHECK(byte_writer_size(&writer) == sizeof(bytes));

    const uint8_t expected[30] = {
        0x12, 0x56, 0x34, 0xde, 0xbc, 0x9a, 0x78,
        0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
        0xfe, 0xfd, 0xff, 0xfc, 0xff, 0xff, 0xff,
        0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    CHECK(std::memcmp(bytes, expected, sizeof(bytes)) == 0);

    ByteReader reader;
    byte_reader_init(&reader, bytes, sizeof(bytes));
    uint8_t u8 = 0; uint16_t u16 = 0; uint32_t u32 = 0; uint64_t u64 = 0;
    int8_t i8 = 0; int16_t i16 = 0; int32_t i32 = 0; int64_t i64 = 0;
    CHECK(byte_reader_read_u8(&reader, &u8) && u8 == 0x12u);
    CHECK(byte_reader_read_u16(&reader, &u16) && u16 == 0x3456u);
    CHECK(byte_reader_read_u32(&reader, &u32) && u32 == 0x789abcdeu);
    CHECK(byte_reader_read_u64(&reader, &u64) && u64 == 0x0123456789abcdefULL);
    CHECK(byte_reader_read_i8(&reader, &i8) && i8 == -2);
    CHECK(byte_reader_read_i16(&reader, &i16) && i16 == -3);
    CHECK(byte_reader_read_i32(&reader, &i32) && i32 == -4);
    CHECK(byte_reader_read_i64(&reader, &i64) && i64 == -5);
    CHECK(byte_reader_ok(&reader));
    CHECK(byte_reader_remaining(&reader) == 0);
}

TEST(serialize, bounds_failure_is_atomic_and_sticky) {
    uint8_t bytes[4] = {0xaa, 0xaa, 0xaa, 0xaa};
    ByteWriter writer;
    byte_writer_init(&writer, bytes, 3);
    CHECK(byte_writer_write_u16(&writer, 0x1234u));
    CHECK(!byte_writer_write_u32(&writer, 0x55667788u));
    CHECK(!byte_writer_ok(&writer));
    CHECK(byte_writer_size(&writer) == 2);
    CHECK(bytes[0] == 0x34 && bytes[1] == 0x12 && bytes[2] == 0xaa && bytes[3] == 0xaa);
    CHECK(!byte_writer_write_u8(&writer, 0xffu));
    CHECK(byte_writer_size(&writer) == 2 && bytes[2] == 0xaa);

    ByteReader reader;
    byte_reader_init(&reader, bytes, 3);
    uint16_t first = 0;
    uint32_t untouched = 0xdeadbeefu;
    CHECK(byte_reader_read_u16(&reader, &first) && first == 0x1234u);
    CHECK(!byte_reader_read_u32(&reader, &untouched));
    CHECK(untouched == 0xdeadbeefu);
    CHECK(!byte_reader_ok(&reader));
    CHECK(byte_reader_remaining(&reader) == 1);
    CHECK(!byte_reader_read_u16(&reader, &first));
    CHECK(byte_reader_remaining(&reader) == 1);
}

TEST(serialize, null_contracts_fail_without_access) {
    ByteWriter writer;
    byte_writer_init(&writer, nullptr, 1);
    CHECK(!byte_writer_ok(&writer));
    CHECK(!byte_writer_write_u8(&writer, 1));

    ByteReader reader;
    byte_reader_init(&reader, nullptr, 1);
    CHECK(!byte_reader_ok(&reader));
    uint8_t out = 7;
    CHECK(!byte_reader_read_u8(&reader, &out));
    CHECK(out == 7);
}
