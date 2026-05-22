#pragma once
#include "order_book.hpp"
#include "csv_parser.hpp" // For OrderMsg
#include <vector>
#include <cstdint>
#include <cstdio>

// ─── Byte swapping intrinsics ────────────────────────────────────────────────
#if defined(_MSC_VER)
    #include <stdlib.h>
    #define BSWAP_16(x) _byteswap_ushort(x)
    #define BSWAP_32(x) _byteswap_ulong(x)
    #define BSWAP_64(x) _byteswap_uint64(x)
#else
    #define BSWAP_16(x) __builtin_bswap16(x)
    #define BSWAP_32(x) __builtin_bswap32(x)
    #define BSWAP_64(x) __builtin_bswap64(x)
#endif

// ─── ITCH 5.0 structs (packed, network byte order) ───────────────────────────
#pragma pack(push, 1)
struct ItchAddOrder {
    char     type;         // 'A'
    uint16_t locate;
    uint16_t tracking;
    uint8_t  timestamp[6]; // 48-bit integer
    uint64_t ref_num;
    char     side;         // 'B' or 'S'
    uint32_t shares;
    char     stock[8];
    uint32_t price;
};

struct ItchOrderCancel {
    char     type;         // 'X'
    uint16_t locate;
    uint16_t tracking;
    uint8_t  timestamp[6];
    uint64_t ref_num;
    uint32_t shares;
};

struct ItchOrderDelete {
    char     type;         // 'D'
    uint16_t locate;
    uint16_t tracking;
    uint8_t  timestamp[6];
    uint64_t ref_num;
};
#pragma pack(pop)

static inline uint64_t parse_ts(const uint8_t* ts) {
    uint64_t t = 0;
    t |= ((uint64_t)ts[0] << 40);
    t |= ((uint64_t)ts[1] << 32);
    t |= ((uint64_t)ts[2] << 24);
    t |= ((uint64_t)ts[3] << 16);
    t |= ((uint64_t)ts[4] << 8);
    t |= ((uint64_t)ts[5]);
    return t;
}

// OS-specific memory mapping headers
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

inline std::vector<OrderMsg> load_itch(const char* path) {
    std::vector<OrderMsg> msgs;
    msgs.reserve(1'000'000);

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { fprintf(stderr, "Cannot open %s\n", path); return {}; }
    
    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    size_t sz = fileSize.QuadPart;

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    const char* data = (const char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "Cannot open %s\n", path); return {}; }
    
    struct stat st;
    fstat(fd, &st);
    size_t sz = st.st_size;

    const char* data = (const char*)mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    madvise((void*)data, sz, MADV_SEQUENTIAL); // OS hint to aggressively prefetch pages
#endif

    if (!data) return msgs;

    const char* p = data;
    const char* end = data + sz;

    while (p < end) {
        char type = *p;
        if (type == 'A') {
            const ItchAddOrder* msg = (const ItchAddOrder*)p;
            OrderMsg m;
            m.order_id = BSWAP_64(msg->ref_num);
            m.type = OrderType::LIMIT;
            m.side = (msg->side == 'B') ? Side::BUY : Side::SELL;
            m.quantity = BSWAP_32(msg->shares);
            m.price = BSWAP_32(msg->price); 
            m.timestamp = parse_ts(msg->timestamp);
            msgs.push_back(m);
            p += sizeof(ItchAddOrder);
        }
        else if (type == 'X') {
            const ItchOrderCancel* msg = (const ItchOrderCancel*)p;
            OrderMsg m;
            m.order_id = BSWAP_64(msg->ref_num);
            m.type = OrderType::CANCEL;
            m.price = m.order_id; // Using price field for target_id in our replay
            m.timestamp = parse_ts(msg->timestamp);
            msgs.push_back(m);
            p += sizeof(ItchOrderCancel);
        }
        else if (type == 'D') {
            const ItchOrderDelete* msg = (const ItchOrderDelete*)p;
            OrderMsg m;
            m.order_id = BSWAP_64(msg->ref_num);
            m.type = OrderType::CANCEL;
            m.price = m.order_id;
            m.timestamp = parse_ts(msg->timestamp);
            msgs.push_back(m);
            p += sizeof(ItchOrderDelete);
        }
        else {
            // Unrecognized/padding byte, skip to avoid infinite loop
            p++;
        }
    }

#if defined(_WIN32)
    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    munmap((void*)data, sz);
    close(fd);
#endif

    return msgs;
}
