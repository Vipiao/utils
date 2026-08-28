// ByteStream.h — byte codecs for trivially copyable values: the unit of exchange
// for anything serialized (e.g. save games, network messages).
//
// Values are copied in the host's own layout, so a stream is only readable by a
// build that agrees on byte order, size and padding. That covers two processes of
// the same build talking to each other and a file read back by the build that
// wrote it; it does not cover a file kept across a layout change, nor two machines
// of different endianness. Anything needing either has to stamp its own version
// and lay its fields out explicitly.
#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

class ByteWriter {
public:
    template <typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ByteStream only carries trivially copyable values");
        std::size_t offset{m_data.size()};
        m_data.resize(offset + sizeof(T));
        std::memcpy(m_data.data() + offset, &value, sizeof(T));
    }

    // A whole span in one copy, the single-value path costing a call and a resize
    // per element.
    template <typename T>
    void writeArray(const T* values, std::size_t count) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ByteStream only carries trivially copyable values");
        std::size_t bytes{count * sizeof(T)};
        std::size_t offset{m_data.size()};
        m_data.resize(offset + bytes);
        std::memcpy(m_data.data() + offset, values, bytes);
    }

    std::vector<std::byte> take() { return std::move(m_data); }

private:
    std::vector<std::byte> m_data{};
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::byte>& data) : m_data{data} {}

    // False once the stream runs out: the buffer is truncated or malformed and the
    // caller must drop it. Every read is checked, so bad input can only cause a
    // drop, never an overrun.
    template <typename T>
    bool read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ByteStream only carries trivially copyable values");
        if (m_offset + sizeof(T) > m_data.size()) {
            return false;
        }
        std::memcpy(&out, m_data.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }

    // A whole span in one copy, checked as the single-value path is. Room left is
    // measured by subtraction: a count off a malformed stream could overflow the
    // sum.
    template <typename T>
    bool readArray(T* out, std::size_t count) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ByteStream only carries trivially copyable values");
        std::size_t bytes{count * sizeof(T)};
        if (bytes > m_data.size() - m_offset) {
            return false;
        }
        std::memcpy(out, m_data.data() + m_offset, bytes);
        m_offset += bytes;
        return true;
    }

private:
    const std::vector<std::byte>& m_data;
    std::size_t m_offset{0};
};
