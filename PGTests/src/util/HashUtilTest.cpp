#include "util/HashUtil.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

enum class Kind : uint16_t { A = 1, B = 2 }; // NOLINT(performance-enum-size): the two byte type is what is hashed

uint64_t hashOfBytes(const std::string& bytes)
{
    HashUtil::Fnv1a64 hasher;
    hasher.addBytes(bytes.data(), bytes.size());
    return hasher.value();
}

TEST(HashUtilTest,
     MatchesFnv1a64ReferenceVectors)
{
    EXPECT_EQ(HashUtil::Fnv1a64 { }.value(), 0xcbf29ce484222325ULL);
    EXPECT_EQ(hashOfBytes("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(hashOfBytes("foobar"), 0x85944171f73967e8ULL);
}

TEST(HashUtilTest,
     EmptyAddBytesDoesNotChangeTheHash)
{
    HashUtil::Fnv1a64 hasher;
    hasher.addBytes(nullptr, 0);
    EXPECT_EQ(hasher.value(), HashUtil::Fnv1a64 { }.value());
}

TEST(HashUtilTest,
     IntegersHashAsLittleEndianBytes)
{
    HashUtil::Fnv1a64 viaValue;
    viaValue.add<uint32_t>(0x04030201U);

    const std::array<unsigned char, 4> bytes { 0x01, 0x02, 0x03, 0x04 };
    HashUtil::Fnv1a64 viaBytes;
    viaBytes.addBytes(bytes.data(), bytes.size());

    EXPECT_EQ(viaValue.value(), viaBytes.value());

    HashUtil::Fnv1a64 viaEnum;
    viaEnum.add(Kind::B);
    HashUtil::Fnv1a64 viaUnderlying;
    viaUnderlying.add<uint16_t>(2);
    EXPECT_EQ(viaEnum.value(), viaUnderlying.value());
}

TEST(HashUtilTest,
     StringsAreLengthPrefixed)
{
    // "ab" as one string and as two strings must differ, which a plain byte hash would not guarantee.
    HashUtil::Fnv1a64 whole;
    whole.add(std::wstring(L"ab"));
    HashUtil::Fnv1a64 split;
    split.add(std::wstring(L"a"));
    split.add(std::wstring(L"b"));
    EXPECT_NE(whole.value(), split.value());

    HashUtil::Fnv1a64 narrowWhole;
    narrowWhole.add(std::string("ab"));
    HashUtil::Fnv1a64 narrowSplit;
    narrowSplit.add(std::string("a"));
    narrowSplit.add(std::string("b"));
    EXPECT_NE(narrowWhole.value(), narrowSplit.value());

    // The prefix also makes a string hash differ from the hash of its raw bytes.
    EXPECT_NE(narrowWhole.value(), hashOfBytes("ab"));

    // A narrow and a wide string with the same letters hash differently (different byte representation).
    EXPECT_NE(whole.value(), narrowWhole.value());
}

TEST(HashUtilTest,
     SameInputGivesSameHashAcrossInstances)
{
    const auto compute = [] {
        HashUtil::Fnv1a64 hasher;
        hasher.add(std::wstring(L"meshes\\test.nif"));
        hasher.add<int32_t>(-7);
        hasher.add(1.5F);
        hasher.add(2.25);
        hasher.add(Kind::A);
        return hasher.value();
    };
    EXPECT_EQ(compute(), compute());
}

TEST(HashUtilTest,
     FloatingPointValuesHashTheirRepresentation)
{
    HashUtil::Fnv1a64 viaFloat;
    viaFloat.add(1.5F);
    const float value = 1.5F;
    HashUtil::Fnv1a64 viaBytes;
    viaBytes.addBytes(&value, sizeof(value));
    EXPECT_EQ(viaFloat.value(), viaBytes.value());

    HashUtil::Fnv1a64 viaDouble;
    viaDouble.add(1.5);
    EXPECT_NE(viaDouble.value(), viaFloat.value());

    HashUtil::Fnv1a64 negativeZero;
    negativeZero.add(-0.0F);
    HashUtil::Fnv1a64 positiveZero;
    positiveZero.add(0.0F);
    EXPECT_NE(negativeZero.value(), positiveZero.value());
}

}
