#include <ostream>

#include "CRC32OStreamBuf.hpp"

class CRC32OStream : public std::ostream {
private:
    CRC32OStreamBuf m_buf;

public:
    explicit CRC32OStream(std::ostream& out)
        : std::ostream(&m_buf)
        , m_buf(out)
    {
    }

    [[nodiscard]] auto checksum() const -> uint32_t { return m_buf.checksum(); }
};
