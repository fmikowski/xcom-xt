/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ITEMORD_H
#define ITEMORD_H

/* The items in DOS's order (OpenXcom lists them as items.rul does: a weapon
   and then its ammunition): a string literal of item numbers, as the -zc
   section reads no named table; tools/itemorder.py writes it and
   `itemorder.py --check` tests it. Each overlay section that lists items
   (trade.c: Purchase, Sell, Stores; xfer.c: Transfer) includes it. */
#define ITEM_ORDER "\x40\x41\x42\x43\x3E\x3F\x55\x56\x44\x45\x63\x68\x64\x69\x65\x66" \
                 "\x67\x6A\x00\x09\x01\x0A\x02\x0B\x5A\x5B\x38\x39\x5C\x5D\x3A\x3B" \
                 "\x5E\x5F\x03\x04\x05\x0F\x60\x61\x11\x1A\x16\x46\x17\x62\x08\x0E" \
                 "\x07\x0D\x06\x0C\x3C\x3D\x18\x19\x10\x15\x47\x50\x52\x54\x53\x51" \
                 "\x48\x49\x4A\x4F\x4B\x4C\x4D\x14\x4E\x12\x13\x6B\x1B\x1C\x1D\x1E" \
                 "\x1F\x20\x27\x28\x29\x2A\x2B\x2F\x30\x31\x2C\x2D\x2E\x21\x22\x23" \
                 "\x24\x25\x26\x34\x36\x35\x37\x33\x32"
#define ITEM_ORDER_N 105

#endif
