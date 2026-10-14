#include "PCH.h"

#include "PatchStream.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PGLite {

PatchStream::PatchStream(std::string path,
                         std::shared_ptr<SourceStream> source,
                         Provider& provider)
    : m_path(std::move(path))
    , m_source(std::move(source))
    , m_provider(&provider)
{
}

RE::BSResource::ErrorCode PatchStream::DoOpen()
{
    m_position = 0;
    m_bytes = m_provider->bytesFor(m_path, *m_source);
    if (!m_bytes) {
        totalSize = 0;
        return RE::BSResource::ErrorCode::kFileError;
    }

    totalSize = static_cast<std::uint32_t>(m_bytes->size());
    return RE::BSResource::ErrorCode::kNone;
}

void PatchStream::DoClose()
{
    m_bytes.reset();
    m_position = 0;
}

RE::BSResource::ErrorCode PatchStream::DoGetInfo(RE::BSResource::Info& info)
{
    info.modifyTime = { };
    info.createTime = { };
    info.fileSize = m_bytes ? m_bytes->size() : 0;
    return RE::BSResource::ErrorCode::kNone;
}

void PatchStream::DoClone(RE::BSTSmartPointer<RE::BSResource::Stream>& stream) const
{
    stream.reset(new PatchStream(m_path, m_source, *m_provider));
}

RE::BSResource::ErrorCode PatchStream::DoRead(void* buffer,
                                              std::uint64_t toRead,
                                              std::uint64_t& read) const
{
    read = 0;
    if (!m_bytes)
        return RE::BSResource::ErrorCode::kFileError;

    const std::uint64_t remaining = m_bytes->size() - std::min<std::uint64_t>(m_position, m_bytes->size());
    read = std::min(toRead, remaining);
    if (read)
        std::memcpy(buffer, m_bytes->data() + m_position, read);

    m_position += read;
    return RE::BSResource::ErrorCode::kNone;
}

RE::BSResource::ErrorCode PatchStream::DoWrite([[maybe_unused]] const void* buffer,
                                               [[maybe_unused]] std::uint64_t toWrite,
                                               std::uint64_t& written) const
{
    written = 0;
    return RE::BSResource::ErrorCode::kUnsupported;
}

RE::BSResource::ErrorCode PatchStream::DoSeek(std::uint64_t toSeek,
                                              RE::BSResource::SeekMode mode,
                                              std::uint64_t& sought) const
{
    const std::uint64_t size = m_bytes ? m_bytes->size() : 0;

    std::uint64_t target = 0;
    switch (mode) {
    case RE::BSResource::SeekMode::kSet:
        target = toSeek;
        break;
    case RE::BSResource::SeekMode::kCur:
        target = m_position + toSeek;
        break;
    case RE::BSResource::SeekMode::kEnd:
        target = size + toSeek;
        break;
    }

    m_position = std::min(target, size);
    sought = m_position;
    return RE::BSResource::ErrorCode::kNone;
}

bool PatchStream::DoGetName(RE::BSFixedString& name) const
{
    name = m_path.c_str();
    return true;
}

}
