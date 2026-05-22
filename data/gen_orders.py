#!/usr/bin/env python3
"""
Synthetic order generator for NanoMatch.
Generates realistic order flow with controlled edge cases.

Usage:
    python3 data/gen_orders.py               # 1M orders (default)
    python3 data/gen_orders.py -n 5000000    # 5M orders
    python3 data/gen_orders.py -n 100000 -o data/small.csv
"""

import csv
import random
import argparse

def gen_orders(n: int, out_path: str, seed: int = 42):
    random.seed(seed)

    BASE_PRICE  = 10000   # $100.00 in ticks (2 decimal places)
    TICK_RANGE  = 200     # orders cluster within ±$1.00 of base
    MAX_QTY     = 500

    order_id    = 1
    active_ids  = []      # tracks resting order ids for cancels

    with open(out_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['order_id', 'type', 'side', 'price', 'quantity', 'timestamp'])

        for i in range(n):
            ts = i * 1000  # synthetic nanosecond timestamps

            r = random.random()

            # ── 65% Limit orders ───────────────────────────────────────────
            if r < 0.65:
                side  = 'B' if random.random() < 0.5 else 'S'
                # Gaussian price distribution centered on BASE_PRICE
                drift = int(random.gauss(0, TICK_RANGE / 3))
                drift = max(-TICK_RANGE, min(TICK_RANGE, drift))
                price = BASE_PRICE + drift
                qty   = random.randint(1, MAX_QTY)
                writer.writerow([order_id, 'L', side, price, qty, ts])
                active_ids.append(order_id)
                # Keep active_ids bounded to avoid unbounded memory
                if len(active_ids) > 50_000:
                    active_ids.pop(0)
                order_id += 1

            # ── 20% Market orders ──────────────────────────────────────────
            elif r < 0.85:
                side = 'B' if random.random() < 0.5 else 'S'
                qty  = random.randint(1, 100)
                writer.writerow([order_id, 'M', side, 0, qty, ts])
                order_id += 1

            # ── 15% Cancel orders ──────────────────────────────────────────
            else:
                if active_ids:
                    target = random.choice(active_ids)
                    active_ids.remove(target)
                    # For cancel: price field holds the target order_id
                    writer.writerow([order_id, 'C', '', target, 0, ts])
                    order_id += 1

    print(f"Generated {n:,} orders → {out_path}")
    print(f"  Last order_id : {order_id - 1}")
    print(f"  Price range   : ${(BASE_PRICE - TICK_RANGE)/100:.2f} – "
          f"${(BASE_PRICE + TICK_RANGE)/100:.2f}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='NanoMatch order generator')
    parser.add_argument('-n', type=int, default=1_000_000, help='number of orders')
    parser.add_argument('-o', default='data/orders_1m.csv', help='output CSV path')
    parser.add_argument('--seed', type=int, default=42, help='random seed')
    args = parser.parse_args()

    gen_orders(args.n, args.o, args.seed)
