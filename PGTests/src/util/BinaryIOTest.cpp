#include "util/BinaryIO.hpp"

#include "TestUtil.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class Kind : uint8_t { A = 1, B = 2 };

std::vector<std::byte> toBytes(const std::string& text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text)
        bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

TEST(BinaryIOTest,
     RoundTripsPrimitives)
{
    BinaryIO::Writer writer;
    writer.write<uint8_t>(0x12);
    writer.write<int32_t>(-5);
    writer.write<uint64_t>(0x0102030405060708ULL);
    writer.writeBool(true);
    writer.writeBool(false);
    writer.write(1.5F);
    writer.write(-2.25);
    writer.write(Kind::B);

    EXPECT_EQ(writer.size(), 28U);
    EXPECT_EQ(writer.data().size(), writer.size());

    BinaryIO::Reader reader(writer.data());
    EXPECT_EQ(reader.remaining(), writer.size());
    EXPECT_FALSE(reader.atEnd());
    EXPECT_EQ(reader.read<uint8_t>(), 0x12);
    EXPECT_EQ(reader.read<int32_t>(), -5);
    EXPECT_EQ(reader.read<uint64_t>(), 0x0102030405060708ULL);
    EXPECT_TRUE(reader.readBool());
    EXPECT_FALSE(reader.readBool());
    EXPECT_EQ(reader.read<float>(), 1.5F);
    EXPECT_EQ(reader.read<double>(), -2.25);
    EXPECT_EQ(reader.read<Kind>(), Kind::B);
    EXPECT_TRUE(reader.atEnd());
    EXPECT_EQ(reader.remaining(), 0U);
}

TEST(BinaryIOTest,
     LayoutIsLittleEndianWithLengthPrefixedStrings)
{
    BinaryIO::Writer writer;
    writer.write<uint32_t>(0x04030201U);
    writer.writeString("abc");
    writer.writeWString(L"aé");
    writer.writeBool(true);

    const std::vector<std::byte> expected = toBytes(std::string("\x01\x02\x03\x04"
                                                                "\x03\x00\x00\x00"
                                                                "abc"
                                                                "\x02\x00\x00\x00"
                                                                "a\x00\xE9\x00"
                                                                "\x01",
                                                                20));
    EXPECT_EQ(writer.data(), expected);
}

TEST(BinaryIOTest,
     RoundTripsStringsAndRawBytes)
{
    BinaryIO::Writer writer;
    writer.writeString("");
    writer.writeString("hello\0world");
    writer.writeWString(L"");
    writer.writeWString(L"meshes\\ü日.nif");
    const std::string raw = "RAW!";
    writer.writeBytes(raw.data(), raw.size());
    writer.writeBytes(nullptr, 0); // no-op

    BinaryIO::Reader reader(writer.data());
    EXPECT_EQ(reader.readString(), "");
    EXPECT_EQ(reader.readString(), "hello");
    EXPECT_EQ(reader.readWString(), L"");
    EXPECT_EQ(reader.readWString(), L"meshes\\ü日.nif");
    std::string readRaw(4, '\0');
    reader.readBytes(readRaw.data(), readRaw.size());
    EXPECT_EQ(readRaw, "RAW!");
    EXPECT_TRUE(reader.atEnd());

    // Zero-size reads are allowed even at the end.
    reader.readBytes(nullptr, 0);
    EXPECT_TRUE(reader.atEnd());
}

TEST(BinaryIOTest,
     ReaderThrowsPastTheEnd)
{
    const std::vector<std::byte> twoBytes { std::byte { 1 }, std::byte { 0 } };
    BinaryIO::Reader reader(twoBytes);
    EXPECT_THROW(reader.read<uint32_t>(), std::runtime_error);
    EXPECT_EQ(reader.remaining(), 2U); // a failed read consumes nothing
    EXPECT_EQ(reader.read<uint16_t>(), 1);
    EXPECT_THROW(reader.read<uint8_t>(), std::runtime_error);
    EXPECT_THROW(reader.readBool(), std::runtime_error);
    char dest = 0;
    EXPECT_THROW(reader.readBytes(&dest, 1), std::runtime_error);
}

TEST(BinaryIOTest,
     ReaderThrowsOnTruncatedStrings)
{
    // Length says 10 bytes, only 2 follow.
    const auto truncatedNarrow = toBytes(std::string("\x0A\x00\x00\x00"
                                                     "ab",
                                                     6));
    BinaryIO::Reader narrow(truncatedNarrow);
    EXPECT_THROW(narrow.readString(), std::runtime_error);

    // Length says 3 code units, only 1 follows.
    const auto truncatedWide = toBytes(std::string("\x03\x00\x00\x00"
                                                   "a\x00",
                                                   6));
    BinaryIO::Reader wide(truncatedWide);
    EXPECT_THROW(wide.readWString(), std::runtime_error);

    BinaryIO::Reader empty(std::span<const std::byte> { });
    EXPECT_TRUE(empty.atEnd());
    EXPECT_THROW(empty.readString(), std::runtime_error);
    EXPECT_THROW(empty.readWString(), std::runtime_error);
}

TEST(BinaryIOTest,
     SaveToFileWritesAtomicallyAndCreatesFolders)
{
    const TestUtil::TempDir dir;
    const auto target = dir / L"sub\\folder\\cache.bin";

    BinaryIO::Writer writer;
    writer.writeString("first");
    ASSERT_TRUE(writer.saveToFile(target));
    EXPECT_EQ(TestUtil::readFile(target),
              std::string("\x05\x00\x00\x00"
                          "first",
                          9));
    EXPECT_FALSE(std::filesystem::exists(dir / L"sub\\folder\\cache.bin.tmp"));

    // Saving again replaces the existing file.
    BinaryIO::Writer second;
    second.writeString("second!");
    ASSERT_TRUE(second.saveToFile(target));
    EXPECT_EQ(TestUtil::readFile(target),
              std::string("\x07\x00\x00\x00"
                          "second!",
                          11));
    EXPECT_FALSE(std::filesystem::exists(dir / L"sub\\folder\\cache.bin.tmp"));
}

TEST(BinaryIOTest,
     SaveToFileFailsWhenTheTargetCannotBeCreated)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"afile", "x");

    // The parent of the target is a file, so nothing can be created below it.
    const BinaryIO::Writer writer;
    EXPECT_FALSE(writer.saveToFile(dir / L"afile\\cache.bin"));
    EXPECT_EQ(TestUtil::readFile(dir / L"afile"), "x");
}

}
