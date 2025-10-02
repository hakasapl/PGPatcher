#include <boost/crc.hpp> // for boost::crc_32_type
#include <ostream>

class CRC32OStreamBuf : public std::streambuf {
private:
    std::ostream* m_underlying;
    boost::crc_32_type m_crc;

protected:
    // called when buffer overflows
    auto overflow(int_type ch) -> int_type override
    {
        if (ch != traits_type::eof()) {
            const char c = static_cast<char>(ch);
            m_underlying->put(c); // write to actual stream
            m_crc.process_byte(static_cast<unsigned char>(c)); // update CRC
        }
        return ch;
    }

    auto xsputn(const char* s, std::streamsize n) -> std::streamsize override
    {
        m_underlying->write(s, n); // write to actual stream
        m_crc.process_bytes(s, n); // update CRC
        return n;
    }

public:
    explicit CRC32OStreamBuf(std::ostream& out)
        : m_underlying(&out)
    {
    }

    [[nodiscard]] auto checksum() const -> uint32_t { return m_crc.checksum(); }
};
