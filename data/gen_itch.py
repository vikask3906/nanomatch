import struct
import random
import argparse

def pack_ts(ts):
    # Pack 64-bit int and take the last 6 bytes for the 48-bit timestamp
    return struct.pack('>Q', ts)[2:8]

def gen_itch(n, out_path, seed=42):
    random.seed(seed)
    BASE_PRICE = 10000
    TICK_RANGE = 200
    MAX_QTY = 500

    order_id = 1
    active_ids = []

    with open(out_path, 'wb') as f:
        for i in range(n):
            ts = i * 1000
            ts_bytes = pack_ts(ts)
            locate = 1
            tracking = i % 65535
            
            r = random.random()
            if r < 0.65:
                # Add Order (Limit) - 36 bytes
                side = b'B' if random.random() < 0.5 else b'S'
                drift = int(random.gauss(0, TICK_RANGE / 3))
                drift = max(-TICK_RANGE, min(TICK_RANGE, drift))
                price = BASE_PRICE + drift
                qty = random.randint(1, MAX_QTY)
                
                # type(1) locate(2) tracking(2) ts(6) ref(8) side(1) shares(4) stock(8) price(4)
                # Format: > c H H 6s Q c I 8s I
                msg = struct.pack('>cHH6sQcI8sI',
                                  b'A', locate, tracking, ts_bytes, order_id, side, qty, b'AAPL    ', price)
                f.write(msg)
                
                active_ids.append(order_id)
                if len(active_ids) > 50000:
                    active_ids.pop(0)
                order_id += 1
                
            elif r < 0.85:
                # Market order simulated as Add Order with extreme prices
                # ITCH price is uint32. So 0xFFFFFFFF for market buy, 0 for market sell.
                side = b'B' if random.random() < 0.5 else b'S'
                price = 0xFFFFFFFF if side == b'B' else 0
                qty = random.randint(1, 100)
                
                msg = struct.pack('>cHH6sQcI8sI',
                                  b'A', locate, tracking, ts_bytes, order_id, side, qty, b'AAPL    ', price)
                f.write(msg)
                order_id += 1
                
            else:
                # Delete Order - 19 bytes
                if active_ids:
                    target = random.choice(active_ids)
                    active_ids.remove(target)
                    
                    # type(1) locate(2) tracking(2) ts(6) ref(8)
                    msg = struct.pack('>cHH6sQ', b'D', locate, tracking, ts_bytes, target)
                    f.write(msg)
                    order_id += 1

    print(f"Generated {n:,} ITCH binary messages -> {out_path}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-n', type=int, default=500_000, help='number of orders')
    parser.add_argument('-o', default='data/orders_500k.itch', help='output binary path')
    parser.add_argument('--seed', type=int, default=42, help='random seed')
    args = parser.parse_args()

    gen_itch(args.n, args.o, args.seed)
