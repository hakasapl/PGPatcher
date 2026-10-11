#include "util/Logger.hpp"

#include "TestUtil.hpp"
#include "util/StringUtil.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::uniqueMessage;
using TestUtil::uniqueMessageW;

// Messages handed to the thread capture callback, which has to be a plain function pointer.
std::vector<std::pair<spdlog::level::level_enum, std::wstring>>
    s_captured; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void captureCallback(spdlog::level::level_enum level,
                     const std::wstring& message)
{
    s_captured.emplace_back(level, message);
}

TEST(LoggerTest,
     FormatsNarrowAndWideMessages)
{
    const auto narrow = uniqueMessage("narrow");
    const auto wide = uniqueMessageW(L"wide");
    Logger::info("{} {} {}", narrow, 42, 1.5);
    Logger::info(L"{} {} {}", wide, 42, 1.5);

    EXPECT_TRUE(TestLog::contains(spdlog::level::info, narrow + " 42 1.5"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, StringUtil::utf16toUTF8(wide) + " 42 1.5"));
}

TEST(LoggerTest,
     EachLevelReachesSpdlogAtItsLevel)
{
    const auto tag = uniqueMessage("levels");
    Logger::trace("{} trace", tag);
    Logger::debug("{} debug", tag);
    Logger::info("{} info", tag);
    Logger::warn("{} warn", tag);
    Logger::error("{} error", tag);
    Logger::critical("{} critical", tag);
    Logger::trace(L"{} wtrace", StringUtil::utf8toUTF16(tag));
    Logger::debug(L"{} wdebug", StringUtil::utf8toUTF16(tag));
    Logger::warn(L"{} wwarn", StringUtil::utf8toUTF16(tag));
    Logger::error(L"{} werror", StringUtil::utf8toUTF16(tag));
    Logger::critical(L"{} wcritical", StringUtil::utf8toUTF16(tag));

    EXPECT_TRUE(TestLog::contains(spdlog::level::trace, tag + " trace"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, tag + " debug"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " info"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, tag + " warn"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, tag + " error"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, tag + " critical"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::trace, tag + " wtrace"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, tag + " wdebug"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, tag + " wwarn"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, tag + " werror"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, tag + " wcritical"));
}

TEST(LoggerTest,
     PrefixStackIsPrependedToDebugAndTraceOnly)
{
    const auto tag = uniqueMessage("prefix");
    {
        const Logger::Prefix outer(L"Outer");
        {
            const Logger::Prefix inner(std::string("Inner"));
            Logger::debug("{} nested", tag);
            Logger::trace(L"{} wnested", StringUtil::utf8toUTF16(tag));
            Logger::info("{} info", tag);
            Logger::warn("{} warn", tag);
        }
        Logger::debug("{} outer", tag);
    }
    Logger::debug("{} none", tag);

    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, "[Outer] [Inner] " + tag + " nested"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::trace, "[Outer] [Inner] " + tag + " wnested"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, "[Outer] " + tag + " outer"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, tag + " none"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::debug, "[Outer] " + tag + " none"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " info"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::info, "[Outer] [Inner] " + tag + " info"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, "[Outer]"));
}

TEST(LoggerTest,
     PrefixStackIsPerThread)
{
    const auto tag = uniqueMessage("threadprefix");
    const Logger::Prefix prefix(L"Main");
    std::thread([&tag] { Logger::debug("{} other", tag); }).join();
    Logger::debug("{} main", tag);

    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, tag + " other"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::debug, "[Main] " + tag + " other"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, "[Main] " + tag + " main"));
}

TEST(LoggerTest,
     WarningsErrorsAndCriticalsAreLoggedOnce)
{
    const auto warn = uniqueMessage("dup warn");
    const auto error = uniqueMessage("dup error");
    const auto critical = uniqueMessage("dup critical");
    const auto info = uniqueMessage("dup info");
    for (int i = 0; i < 3; i++) {
        Logger::warn("{}", warn);
        Logger::error("{}", error);
        Logger::critical("{}", critical);
        Logger::info("{}", info);
        Logger::debug("{}", info);
    }

    EXPECT_EQ(TestLog::count(spdlog::level::warn, warn), 1U);
    EXPECT_EQ(TestLog::count(spdlog::level::err, error), 1U);
    EXPECT_EQ(TestLog::count(spdlog::level::critical, critical), 1U);
    EXPECT_EQ(TestLog::count(spdlog::level::info, info), 3U);
    EXPECT_EQ(TestLog::count(spdlog::level::debug, info), 3U);

    // Suppression compares the resolved text, so the narrow and wide overload share one entry.
    Logger::warn(L"{}", StringUtil::utf8toUTF16(warn));
    EXPECT_EQ(TestLog::count(spdlog::level::warn, warn), 1U);
}

TEST(LoggerTest,
     ResetToRunStartForgetsMessagesLoggedAfterTheMark)
{
    const auto before = uniqueMessage("before mark");
    const auto after = uniqueMessage("after mark");
    Logger::warn("{}", before);
    Logger::markRunStart();
    Logger::warn("{}", after);
    Logger::resetToRunStart();

    Logger::warn("{}", before);
    Logger::warn("{}", after);
    EXPECT_EQ(TestLog::count(spdlog::level::warn, before), 1U);
    EXPECT_EQ(TestLog::count(spdlog::level::warn, after), 2U);
}

TEST(LoggerTest,
     ThreadMessageCaptureSeesEveryWarningEvenDuplicates)
{
    const auto warn = uniqueMessage("captured warn");
    const auto error = uniqueMessageW(L"captured error");
    const auto info = uniqueMessage("captured info");
    s_captured.clear();
    Logger::setThreadMessageCapture(&captureCallback);
    Logger::warn("{}", warn);
    Logger::warn("{}", warn);
    Logger::error(L"{}", error);
    Logger::critical("{}", warn + " critical");
    Logger::info("{}", info);
    Logger::debug("{}", info);
    Logger::setThreadMessageCapture(nullptr);
    Logger::warn("{}", warn + " after");

    ASSERT_EQ(s_captured.size(), 4U);
    EXPECT_EQ(s_captured[0].first, spdlog::level::warn);
    EXPECT_EQ(s_captured[0].second, StringUtil::utf8toUTF16(warn));
    EXPECT_EQ(s_captured[1].second, StringUtil::utf8toUTF16(warn));
    EXPECT_EQ(s_captured[2].first, spdlog::level::err);
    EXPECT_EQ(s_captured[2].second, error);
    EXPECT_EQ(s_captured[3].first, spdlog::level::critical);
    EXPECT_EQ(TestLog::count(spdlog::level::warn, warn), 2U); // the original and the " after" one
}

TEST(LoggerTest,
     ThreadMessageCaptureIsPerThread)
{
    const auto tag = uniqueMessage("thread capture");
    s_captured.clear();
    Logger::setThreadMessageCapture(&captureCallback);
    std::thread([&tag] { Logger::warn("{} other thread", tag); }).join();
    Logger::warn("{} main thread", tag);
    Logger::setThreadMessageCapture(nullptr);

    ASSERT_EQ(s_captured.size(), 1U);
    EXPECT_EQ(s_captured[0].second, StringUtil::utf8toUTF16(tag + " main thread"));
}

TEST(LoggerTest,
     ThreadedBufferHoldsMessagesUntilFlushed)
{
    const auto tag = uniqueMessage("buffered");
    Logger::startThreadedBuffer();
    Logger::info("{} one", tag);
    Logger::warn(L"{} two", StringUtil::utf8toUTF16(tag));
    Logger::debug("{} three", tag);
    EXPECT_FALSE(TestLog::contains(spdlog::level::info, tag + " one"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, tag + " two"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::debug, tag + " three"));

    Logger::flushThreadedBuffer();
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " one"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, tag + " two"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::debug, tag + " three"));

    // Buffering is off again after the flush.
    Logger::info("{} four", tag);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " four"));
}

TEST(LoggerTest,
     ThreadedBufferKeepsOrderAndIsPerThread)
{
    const auto tag = uniqueMessage("ordered");
    std::thread([&tag] {
        Logger::startThreadedBuffer();
        Logger::info("{} a", tag);
        Logger::info("{} b", tag);
        Logger::flushThreadedBuffer();
    }).join();

    // Starting a buffer on another thread does not buffer this thread.
    Logger::info("{} main", tag);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " main"));

    const auto entries = TestLog::entries();
    size_t indexA = 0;
    size_t indexB = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].message == tag + " a")
            indexA = i;
        if (entries[i].message == tag + " b")
            indexB = i;
    }
    EXPECT_LT(indexA, indexB);
}

TEST(LoggerTest,
     DuplicateSuppressionAppliesInsideTheThreadedBuffer)
{
    const auto tag = uniqueMessage("buffered dup");
    Logger::startThreadedBuffer();
    Logger::warn("{}", tag);
    Logger::warn("{}", tag);
    Logger::flushThreadedBuffer();
    EXPECT_EQ(TestLog::count(spdlog::level::warn, tag), 1U);
}

}
