#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace PGLite {

/**
 * @brief The engine's own stream for a mesh, resolved before PGLite's registration replaced it, so the input of a patch
 * is always what the engine itself would have parsed. Shared by the streams of a base path and its variants, which
 * read it one at a time.
 */
struct SourceStream {
    RE::BSTSmartPointer<RE::BSResource::Stream> stream;
    std::mutex mutex;
};

/**
 * @brief The stream the engine reads a registered path from (DESIGN.md section 4.2).
 *
 * One object is registered per path for the session; the engine's model entry for the path shares it. The bytes it
 * serves come from its provider at every open and are dropped at close, so PGLite keeps no mesh bytes between loads.
 * A direct open by path receives a clone with its own position and buffer. Writing is unsupported, as for the engine's
 * read-only streams.
 */
class PatchStream final : public RE::BSResource::Stream {
public:
    /**
     * @brief Produces the bytes a stream serves.
     */
    class Provider {
    public:
        Provider() = default;
        virtual ~Provider() = default;
        Provider(const Provider&) = delete;
        Provider& operator=(const Provider&) = delete;
        Provider(Provider&&) = delete;
        Provider& operator=(Provider&&) = delete;

        /**
         * @brief Returns the bytes of a registered path, or nullptr if they cannot be produced. Runs on the thread that
         * opens the stream and must not throw.
         *
         * @param path The registered path.
         * @param source The engine's stream for the base path of the family; a generated texture (TextureGenerator)
         * has none and gets an empty one.
         */
        [[nodiscard]] virtual std::shared_ptr<const std::vector<std::byte>> bytesFor(const std::string& path,
                                                                                     SourceStream& source) = 0;
    };

    /**
     * @brief Constructs a closed stream.
     *
     * @param path The registered path, as the registry stores it and the loader passes it on.
     * @param source The engine's stream for the base path of the family.
     * @param provider Produces the bytes at every open. Must outlive the stream.
     */
    PatchStream(std::string path,
                std::shared_ptr<SourceStream> source,
                Provider& provider);
    ~PatchStream() override = default;
    PatchStream(const PatchStream&) = delete;
    PatchStream& operator=(const PatchStream&) = delete;
    PatchStream(PatchStream&&) = delete;
    PatchStream& operator=(PatchStream&&) = delete;

    [[nodiscard]] const std::string& path() const { return m_path; }

    // StreamBase.
    RE::BSResource::ErrorCode DoOpen() override;
    void DoClose() override;
    RE::BSResource::ErrorCode DoGetInfo(RE::BSResource::Info& info) override;

    // Stream.
    void DoClone(RE::BSTSmartPointer<RE::BSResource::Stream>& stream) const override;
    RE::BSResource::ErrorCode DoRead(void* buffer,
                                     std::uint64_t toRead,
                                     std::uint64_t& read) const override;
    RE::BSResource::ErrorCode DoWrite(const void* buffer,
                                      std::uint64_t toWrite,
                                      std::uint64_t& written) const override;
    RE::BSResource::ErrorCode DoSeek(std::uint64_t toSeek,
                                     RE::BSResource::SeekMode mode,
                                     std::uint64_t& sought) const override;
    bool DoGetName(RE::BSFixedString& name) const override;

private:
    std::string m_path;
    std::shared_ptr<SourceStream> m_source;
    Provider* m_provider;

    // The bytes exist between open and close only. The engine reads and seeks through const functions.
    mutable std::shared_ptr<const std::vector<std::byte>> m_bytes;
    mutable std::uint64_t m_position = 0;
};

}
