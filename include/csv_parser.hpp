#pragma once
#include "order_book.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

// ─── Parsed order message (intermediate representation) ───────────────────────

struct OrderMsg {
    uint64_t  order_id;
    uint64_t  price;      // also used as cancel_target_id for CANCEL type
    uint64_t  quantity;
    uint64_t  timestamp;
    OrderType type;
    Side      side;
};

// ─── Fast integer parser — no stringstream, no atoi overhead ─────────────────

static inline uint64_t parse_u64(const char*& p) {
    uint64_t v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + static_cast<uint64_t>(*p++ - '0');
    if (*p == ',' || *p == '\n' || *p == '\r') ++p;
    return v;
}

static inline char parse_char(const char*& p) {
    char c = *p++;
    if (*p == ',' || *p == '\n' || *p == '\r') ++p;
    return c;
}

static inline void skip_to_next_line(const char*& p) {
    while (*p && *p != '\n') ++p;
    if (*p) ++p;
}

// ─── Load CSV into vector<OrderMsg> ──────────────────────────────────────────
// CSV format: order_id,type,side,price,quantity,timestamp
// type: L=limit, M=market, C=cancel
// side: B=buy, S=sell, empty for cancel

inline std::vector<OrderMsg> load_csv(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", path);
        return {};
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    char* buf = static_cast<char*>(malloc(sz + 2));
    fread(buf, 1, sz, f);
    buf[sz]     = '\n';
    buf[sz + 1] = '\0';
    fclose(f);

    std::vector<OrderMsg> msgs;
    msgs.reserve(1'000'000);

    const char* p = buf;
    skip_to_next_line(p);  // skip header

    while (*p && p < buf + sz) {
        if (*p == '\n' || *p == '\r') { ++p; continue; }

        OrderMsg m{};
        m.order_id  = parse_u64(p);

        char type_c = parse_char(p);
        if      (type_c == 'L') m.type = OrderType::LIMIT;
        else if (type_c == 'M') m.type = OrderType::MARKET;
        else if (type_c == 'C') m.type = OrderType::CANCEL;
        else { skip_to_next_line(p); continue; }

        // Side field: 'B', 'S', or empty (for cancel)
        if (*p != ',') {
            char side_c = parse_char(p);
            m.side = (side_c == 'B') ? Side::BUY : Side::SELL;
        } else {
            ++p;  // skip comma
            m.side = Side::BUY;  // unused for cancel
        }

        m.price     = parse_u64(p);  // for CANCEL: holds target order_id
        m.quantity  = parse_u64(p);
        m.timestamp = parse_u64(p);

        msgs.push_back(m);
    }

    free(buf);
    return msgs;
}

// ─── Replay loaded messages into an OrderBook ─────────────────────────────────

inline void replay(OrderBook& book, const std::vector<OrderMsg>& msgs) {
    for (const auto& m : msgs) {
        switch (m.type) {
            case OrderType::LIMIT:
                book.add_limit_order(m.order_id, m.side, m.price, m.quantity);
                break;
            case OrderType::MARKET:
                book.add_market_order(m.order_id, m.side, m.quantity);
                break;
            case OrderType::CANCEL:
                book.cancel_order(m.price);  // price field holds cancel target id
                break;
        }
    }
}
