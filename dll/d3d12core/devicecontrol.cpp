/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device wide controls: power, background work, shader caches, removal, meta commands
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "metacommand.hpp"
#include "shadercache.hpp"

using PFN_RX_DEVELOPER_MODE = HRESULT (WINAPI*)(BOOL*);

/* Meta commands applications see without developer mode; drivers may list others */
static const GUID RxApprovedMetaCommands[] =
{
    { 0x018516cf, 0x0e95, 0x41d8, { 0x80, 0xa7, 0x07, 0x73, 0xa4, 0x74, 0x78, 0x0e } },
    { 0x02d5b129, 0x9c45, 0x4372, { 0x9b, 0xfd, 0xe3, 0x3e, 0x0c, 0x70, 0xdc, 0x02 } },
    { 0x06b684e6, 0xa1d0, 0x44bd, { 0xac, 0xdb, 0xb6, 0x72, 0x37, 0x24, 0xe3, 0x3d } },
    { 0x08b1a398, 0x28f9, 0x4520, { 0xb8, 0x5b, 0x55, 0xa7, 0x87, 0xe9, 0xcd, 0x6e } },
    { 0x0e6a11b7, 0x03b8, 0x4264, { 0x9d, 0xc6, 0xb9, 0x86, 0x95, 0x8a, 0xd5, 0x1d } },
    { 0x11f637de, 0xf6b1, 0x48be, { 0x9d, 0xf3, 0x92, 0x54, 0x7a, 0xb7, 0x0c, 0x2b } },
    { 0x12b417c3, 0x9bb1, 0x454e, { 0x9b, 0xd6, 0xee, 0x0c, 0xd3, 0x64, 0xb2, 0x24 } },
    { 0x16e02999, 0xd6c9, 0x4f3c, { 0xa8, 0xf0, 0x86, 0xfa, 0x2d, 0xb6, 0x49, 0x3e } },
    { 0x17804d6b, 0xebfe, 0x426f, { 0x88, 0xfc, 0xfe, 0xa7, 0x2e, 0x3f, 0x33, 0x56 } },
    { 0x1ba0ee9e, 0x25b7, 0x467b, { 0xbf, 0x1c, 0x65, 0xeb, 0x24, 0xe2, 0xe3, 0xed } },
    { 0x1bddd090, 0xc47e, 0x459c, { 0x8f, 0x81, 0x42, 0xc9, 0xf9, 0x7a, 0x53, 0x08 } },
    { 0x1dd0ae25, 0x4caf, 0x42e1, { 0xb5, 0xd4, 0xa8, 0x2c, 0x1f, 0x94, 0x85, 0xf6 } },
    { 0x1deb77d1, 0x456e, 0x4793, { 0xbf, 0x00, 0x07, 0x99, 0xf6, 0xf2, 0xcf, 0x0d } },
    { 0x1e52ebab, 0x25ba, 0x463a, { 0xa2, 0x85, 0x0a, 0x78, 0x8e, 0xef, 0x5d, 0x01 } },
    { 0x1eb9b8fe, 0x2e36, 0x4a13, { 0xa2, 0x99, 0x15, 0xa1, 0x38, 0x57, 0x39, 0xb5 } },
    { 0x1ef5ce59, 0x6227, 0x4eea, { 0x97, 0xff, 0x28, 0xe8, 0x4d, 0x50, 0xe7, 0xc0 } },
    { 0x20582c39, 0x8613, 0x4f4c, { 0xa2, 0xd6, 0x3b, 0x23, 0xec, 0x0e, 0x81, 0xb1 } },
    { 0x214a6468, 0x339f, 0x412d, { 0xa7, 0xe6, 0x2f, 0x72, 0x92, 0x72, 0xd4, 0x18 } },
    { 0x22453183, 0x6f2a, 0x4d17, { 0xb1, 0xfc, 0xcb, 0x7b, 0xd7, 0x27, 0x0b, 0x0d } },
    { 0x26014594, 0xfb6e, 0x4ad8, { 0x92, 0x0e, 0x24, 0x78, 0x6c, 0x57, 0x46, 0xe1 } },
    { 0x2c229fb5, 0x8230, 0x4462, { 0x88, 0x80, 0xe3, 0x85, 0x82, 0xe4, 0xe4, 0x4e } },
    { 0x2c41400a, 0xbbb3, 0x4bec, { 0xa2, 0xec, 0xb9, 0x1c, 0x41, 0x3f, 0xb0, 0xb0 } },
    { 0x2efc8a65, 0x3b1a, 0x4ef3, { 0xb5, 0x19, 0x11, 0x77, 0x51, 0xa5, 0x2c, 0xbe } },
    { 0x3039a0c0, 0x6ab7, 0x4968, { 0xa7, 0x59, 0x36, 0xbe, 0x39, 0xea, 0xa1, 0x50 } },
    { 0x31102981, 0x1dbc, 0x457b, { 0xb1, 0x3b, 0xef, 0xc1, 0x14, 0x02, 0x02, 0xb1 } },
    { 0x3552efc5, 0x80d1, 0x40e1, { 0x9a, 0x20, 0x7b, 0x6b, 0x70, 0x39, 0x6d, 0xd1 } },
    { 0x389b62e2, 0xe71f, 0x4696, { 0x82, 0xd3, 0x2f, 0x8c, 0x40, 0xf7, 0xb1, 0xa9 } },
    { 0x3a1cc6a2, 0x68df, 0x4e8e, { 0xa1, 0xe7, 0x39, 0x58, 0xf8, 0x28, 0x9c, 0x04 } },
    { 0x3ed87179, 0xbcbe, 0x4d5f, { 0x91, 0xdf, 0xe6, 0x02, 0xa4, 0x7a, 0xf3, 0xeb } },
    { 0x4276e0d9, 0x7b81, 0x4f20, { 0x91, 0x37, 0xb6, 0xe5, 0xd3, 0x59, 0xde, 0x7a } },
    { 0x42be9300, 0xf85a, 0x438a, { 0xac, 0xc1, 0x10, 0x00, 0x56, 0xc0, 0x7d, 0xa1 } },
    { 0x43b4fd75, 0x115f, 0x40e7, { 0x89, 0x77, 0x28, 0xe3, 0x60, 0xda, 0xfc, 0x34 } },
    { 0x48b67e80, 0xc8ef, 0x4108, { 0x89, 0xce, 0xbd, 0x78, 0x4a, 0x6f, 0xf8, 0xd3 } },
    { 0x4f8a68bb, 0x4d33, 0x46a9, { 0xbb, 0x63, 0xa8, 0xb7, 0x44, 0x4f, 0x7d, 0xeb } },
    { 0x51a13afe, 0xc299, 0x40d4, { 0xb7, 0xaa, 0x43, 0x0c, 0xe0, 0x28, 0x23, 0x34 } },
    { 0x5428b5ed, 0x9f25, 0x4016, { 0x80, 0x9d, 0x99, 0xb3, 0xe9, 0x8d, 0xba, 0x86 } },
    { 0x5511f5c7, 0x166d, 0x4265, { 0x82, 0xfc, 0xe2, 0xf5, 0x8e, 0xb4, 0x61, 0x75 } },
    { 0x56ca4a29, 0x63e2, 0x4600, { 0xa4, 0x39, 0x0a, 0x65, 0x48, 0xbb, 0xd2, 0x79 } },
    { 0x56e2a324, 0xb0c4, 0x49ec, { 0xa6, 0xac, 0x0e, 0xb7, 0x51, 0x25, 0xdf, 0x6f } },
    { 0x57c3cb9b, 0x31fa, 0x4d58, { 0xae, 0x1e, 0x18, 0x17, 0x18, 0xbd, 0x9e, 0x6c } },
    { 0x5992a492, 0xe845, 0x415d, { 0xb8, 0xf3, 0xed, 0x94, 0x06, 0x7f, 0x32, 0x69 } },
    { 0x5aed9b57, 0x79cb, 0x4863, { 0x9c, 0xab, 0xf0, 0xc8, 0x08, 0x5e, 0x5f, 0xc8 } },
    { 0x5ca28c6c, 0x298f, 0x4fc6, { 0x8b, 0x28, 0x76, 0x81, 0xcb, 0x55, 0x98, 0xb2 } },
    { 0x5dfe0d7a, 0xabd8, 0x4086, { 0x81, 0xc6, 0x6d, 0x13, 0x89, 0xd6, 0xe5, 0x79 } },
    { 0x5e9c19de, 0x693e, 0x445b, { 0xaa, 0xc5, 0x23, 0x89, 0x2e, 0x71, 0x0c, 0x15 } },
    { 0x5f150063, 0x087e, 0x4614, { 0x89, 0x07, 0xf9, 0x99, 0x97, 0x04, 0x53, 0x35 } },
    { 0x5fca64e7, 0xf784, 0x4825, { 0x8a, 0x16, 0x47, 0x53, 0xac, 0x1f, 0xd5, 0x1b } },
    { 0x625cac98, 0xde15, 0x4cf6, { 0x82, 0x3d, 0x87, 0x87, 0xa6, 0x11, 0x7a, 0xa0 } },
    { 0x639745d9, 0x4e9b, 0x4f29, { 0xbb, 0xbf, 0x81, 0x05, 0xa4, 0x96, 0x9c, 0x33 } },
    { 0x639c69d9, 0xa276, 0x40b2, { 0x8f, 0x12, 0x74, 0xa5, 0xd5, 0x5e, 0x35, 0xf8 } },
    { 0x66afd7b3, 0xf7b9, 0x4e76, { 0xac, 0xa3, 0x52, 0x6d, 0xf0, 0x3c, 0xf0, 0x7f } },
    { 0x68f20d14, 0x849e, 0x4042, { 0x88, 0x63, 0x59, 0x61, 0x5b, 0x07, 0xa0, 0xdb } },
    { 0x69a8dcc9, 0x48be, 0x4b0f, { 0x89, 0xb4, 0x09, 0xae, 0x79, 0xea, 0xed, 0x60 } },
    { 0x6b6152f1, 0x6921, 0x481c, { 0x85, 0xbc, 0xe7, 0x1a, 0x69, 0x06, 0x5e, 0x9d } },
    { 0x6e0990b2, 0x87b8, 0x4933, { 0x9a, 0x7e, 0x80, 0x22, 0x87, 0xe4, 0x7f, 0xd3 } },
    { 0x6e7d61c3, 0xfa6d, 0x4540, { 0x8e, 0x0b, 0x9d, 0x40, 0xcc, 0x85, 0x62, 0x00 } },
    { 0x6f130ee4, 0xdc84, 0x4018, { 0x9c, 0x5e, 0xa5, 0xb9, 0x78, 0xac, 0xdb, 0x9d } },
    { 0x71fde7be, 0x1295, 0x4e13, { 0x84, 0x7c, 0x40, 0x16, 0x45, 0xf8, 0x23, 0x5e } },
    { 0x7460d38f, 0x5b5f, 0x4f03, { 0xa2, 0xb2, 0x9c, 0xa4, 0x81, 0x9e, 0x24, 0x1c } },
    { 0x75e1690f, 0xb37b, 0x44c5, { 0xa5, 0xd5, 0x7b, 0xc6, 0xb3, 0xc8, 0x14, 0xcd } },
    { 0x801671a2, 0x1f20, 0x401a, { 0xa7, 0xa2, 0x64, 0x03, 0x07, 0xcc, 0x49, 0x16 } },
    { 0x84ed5562, 0xea4f, 0x4204, { 0x98, 0x4f, 0x76, 0xa8, 0xc4, 0x3e, 0x32, 0xca } },
    { 0x88685729, 0x4aef, 0x42f5, { 0xb3, 0x55, 0xec, 0x7e, 0xb2, 0xa3, 0xc8, 0x1c } },
    { 0x8d5fae79, 0x108f, 0x42d7, { 0xa9, 0x0a, 0x08, 0xdb, 0x67, 0xc8, 0x12, 0xb0 } },
    { 0x8eb9cf5d, 0xdffb, 0x44aa, { 0xbe, 0x99, 0xb5, 0xd0, 0x3c, 0x26, 0x79, 0xcc } },
    { 0x8fce63aa, 0x0c84, 0x495c, { 0x9a, 0xc7, 0x13, 0xc1, 0x36, 0xb8, 0xe8, 0xa8 } },
    { 0x90a01169, 0x93fd, 0x4ab2, { 0xaf, 0x84, 0xdf, 0x33, 0x75, 0x7e, 0x8a, 0xfd } },
    { 0x90a359f1, 0xb301, 0x4fa7, { 0xa1, 0x58, 0xd6, 0x8c, 0xa5, 0xbd, 0x7f, 0x1e } },
    { 0x936f7f01, 0x203f, 0x44d3, { 0x9e, 0x18, 0x41, 0x98, 0xd2, 0x43, 0xb4, 0xea } },
    { 0x980bfe94, 0xd9fc, 0x4aa7, { 0x83, 0x7b, 0xac, 0xd6, 0x79, 0x83, 0x83, 0xf7 } },
    { 0x98460d89, 0x6d1c, 0x45a1, { 0x9e, 0x54, 0x48, 0x4a, 0x82, 0x54, 0x45, 0x3e } },
    { 0x9a5920fb, 0x0917, 0x4307, { 0x80, 0xf3, 0x40, 0xb3, 0xa7, 0xcb, 0xee, 0x00 } },
    { 0x9aed688e, 0x7405, 0x45e3, { 0x8e, 0xaf, 0x1c, 0x43, 0x59, 0xad, 0x90, 0x36 } },
    { 0xa1aaaa89, 0x37be, 0x4021, { 0x85, 0xe1, 0xdb, 0x1b, 0x54, 0x3c, 0x7d, 0x17 } },
    { 0xa1e392eb, 0x234d, 0x486c, { 0x81, 0x19, 0xc1, 0xba, 0xba, 0xdb, 0x14, 0xc6 } },
    { 0xa6b7c5b7, 0x9ec2, 0x4613, { 0xb4, 0x4b, 0x00, 0x21, 0x27, 0xeb, 0xa2, 0x7d } },
    { 0xa9092592, 0x0129, 0x4b9c, { 0x80, 0x1d, 0x46, 0x1f, 0x8c, 0x06, 0xff, 0xde } },
    { 0xab0faff7, 0xdf1e, 0x452b, { 0xbd, 0x38, 0x9e, 0xda, 0x6c, 0x6e, 0x53, 0xbc } },
    { 0xac34b667, 0xc0e4, 0x4371, { 0xaf, 0x55, 0x63, 0x0a, 0x7c, 0xf1, 0x98, 0xd9 } },
    { 0xafeffe6f, 0x487a, 0x46f3, { 0xa9, 0x8d, 0xff, 0xf1, 0xf4, 0x41, 0x81, 0x1f } },
    { 0xb145b72d, 0x5da6, 0x4a0c, { 0xb7, 0x8f, 0x0f, 0xd2, 0x10, 0xe0, 0x35, 0xc8 } },
    { 0xb274486f, 0x3a65, 0x4ec5, { 0x94, 0x85, 0xf2, 0xea, 0x21, 0x26, 0x33, 0xcd } },
    { 0xb2f4da77, 0x1a64, 0x4231, { 0x94, 0xe8, 0x97, 0x18, 0x5d, 0xc8, 0x99, 0x2d } },
    { 0xb76c884a, 0xf78c, 0x4d3f, { 0x88, 0x09, 0xcd, 0x9f, 0x4d, 0xf2, 0x9a, 0x12 } },
    { 0xb94e90ad, 0xdf15, 0x4b97, { 0xa1, 0xa0, 0x22, 0xed, 0xef, 0x00, 0x2d, 0x03 } },
    { 0xbdf21e38, 0x7c36, 0x4d30, { 0xad, 0xce, 0x46, 0x4b, 0x60, 0x10, 0x3c, 0xdd } },
    { 0xbe5a62b8, 0x0128, 0x43c4, { 0xbc, 0xa0, 0xec, 0x05, 0xbc, 0x7c, 0xdc, 0x01 } },
    { 0xbe9083fd, 0x7211, 0x42ee, { 0xa9, 0x09, 0x1a, 0xca, 0x96, 0xa5, 0x05, 0xf6 } },
    { 0xbe927be1, 0x8ea1, 0x43fd, { 0x98, 0x9b, 0xe7, 0x16, 0x32, 0xaf, 0x05, 0x25 } },
    { 0xbfd3b451, 0xc3c2, 0x401b, { 0x86, 0x37, 0x9c, 0x8d, 0x94, 0x27, 0xf9, 0xe9 } },
    { 0xbfd92d43, 0xd1bf, 0x472b, { 0x95, 0xe8, 0xca, 0xe9, 0xa9, 0x22, 0x5a, 0x69 } },
    { 0xc02c4a03, 0x5fdb, 0x4131, { 0xbd, 0x89, 0xd9, 0xd2, 0x52, 0x9b, 0x27, 0x6d } },
    { 0xc0d66fa7, 0xf8d5, 0x4a8f, { 0xa2, 0x98, 0x46, 0xbf, 0x0f, 0x43, 0xe3, 0x3a } },
    { 0xc10f066a, 0x2621, 0x47b7, { 0xa5, 0xde, 0xb2, 0x4d, 0x52, 0x91, 0xf6, 0xd5 } },
    { 0xc33637d6, 0xe34e, 0x4c34, { 0xb1, 0x57, 0xfb, 0xfb, 0x19, 0x01, 0x02, 0xe5 } },
    { 0xc52eb3c7, 0x7d28, 0x4aa0, { 0x97, 0x9a, 0x46, 0xda, 0x51, 0xd2, 0x21, 0xa2 } },
    { 0xd0cc8343, 0x6537, 0x45b6, { 0xa0, 0x27, 0x26, 0xb7, 0xad, 0x82, 0xbd, 0x1f } },
    { 0xd1572d4a, 0x7031, 0x434c, { 0xa6, 0x58, 0x62, 0x23, 0x29, 0xcf, 0x6f, 0xaa } },
    { 0xd1c2a8b2, 0xcf26, 0x48e8, { 0xa9, 0xfe, 0xac, 0x68, 0xeb, 0x40, 0x24, 0xe8 } },
    { 0xd227339f, 0xf5a7, 0x4a67, { 0xb2, 0xb7, 0x01, 0x9b, 0xe0, 0x40, 0xc0, 0x35 } },
    { 0xd26603c5, 0x12cf, 0x4e2d, { 0xa0, 0x90, 0x22, 0x32, 0xfc, 0x10, 0xc3, 0x49 } },
    { 0xdbf70665, 0xf8b6, 0x46c1, { 0xb6, 0x44, 0x15, 0x1b, 0x50, 0xa3, 0xe0, 0xd8 } },
    { 0xdc6d11d0, 0x1264, 0x4de4, { 0xb6, 0x38, 0xb5, 0xbc, 0x22, 0xfc, 0x36, 0x1d } },
    { 0xde8813ef, 0xeab2, 0x484d, { 0xa3, 0xbe, 0xaf, 0x25, 0xd6, 0xb7, 0x5f, 0x1b } },
    { 0xe026891d, 0xa4ce, 0x428d, { 0x94, 0x31, 0xeb, 0xf8, 0x26, 0x0c, 0x37, 0x65 } },
    { 0xe1f78ee9, 0x893f, 0x4cb2, { 0x9d, 0x0e, 0xbd, 0x4e, 0x02, 0x67, 0x14, 0xe5 } },
    { 0xe6c87d57, 0x68ab, 0x4d4c, { 0x97, 0x29, 0xc9, 0x65, 0x7b, 0x17, 0x22, 0x55 } },
    { 0xe7ab4322, 0xd08d, 0x4683, { 0xb6, 0x9a, 0xc6, 0xa4, 0xfb, 0x41, 0x0f, 0x6d } },
    { 0xe7e16ee2, 0xb85b, 0x4590, { 0xb6, 0x4f, 0xb4, 0x48, 0xd5, 0x24, 0x5e, 0xde } },
    { 0xe7e25020, 0x1ab2, 0x4428, { 0x99, 0x00, 0x16, 0xa7, 0x2e, 0xdf, 0x6c, 0xb4 } },
    { 0xebd630bc, 0x0c8f, 0x4039, { 0xb9, 0x45, 0x47, 0xa7, 0xf5, 0x09, 0xad, 0x5b } },
    { 0xeee40dec, 0x1131, 0x43d8, { 0x80, 0x0d, 0xb5, 0x2b, 0xa6, 0xaa, 0xc7, 0xcb } },
    { 0xf1b28a09, 0xb181, 0x41ec, { 0x8b, 0x61, 0xaa, 0xd2, 0x9c, 0xd8, 0x6a, 0x12 } },
    { 0xfa004f7d, 0x7249, 0x415c, { 0xa7, 0xfa, 0x44, 0xd2, 0x4d, 0x54, 0xf7, 0x9c } },
    { 0xfaf24e4f, 0xc6d0, 0x418f, { 0xae, 0xe1, 0x3d, 0xae, 0xc7, 0xb6, 0x29, 0x1b } },
    { 0xfe82495c, 0xd151, 0x4d77, { 0xbc, 0x52, 0x56, 0xe5, 0x8d, 0x86, 0xc2, 0x76 } },
    { 0xffa1a41c, 0xe479, 0x48af, { 0xb2, 0x07, 0xb3, 0x52, 0xf2, 0x9c, 0x47, 0x57 } }
};

/* Whether developer mode is on, read once as the system runtime does at device creation */
static
BOOLEAN
RxDeveloperMode12(VOID)
{
    static INIT_ONCE Once = INIT_ONCE_STATIC_INIT;
    static BOOLEAN Enabled = FALSE;
    BOOL Pending = FALSE;

    if (InitOnceBeginInitialize(&Once, 0, &Pending, nullptr) && Pending)
    {
        HMODULE Module = LoadLibraryExW(L"api-ms-win-appmodel-unlock-l1-1-0.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        PFN_RX_DEVELOPER_MODE Query = nullptr;
        BOOL Value = FALSE;

        if (Module != NULL)
            Query = reinterpret_cast<PFN_RX_DEVELOPER_MODE>(GetProcAddress(Module, "IsDeveloperModeEnabled"));
        if (Query != nullptr && SUCCEEDED(Query(&Value)))
            Enabled = Value != FALSE;
        InitOnceComplete(&Once, 0, nullptr);
    }

    return Enabled;
}

static
BOOLEAN
RxIsApprovedMetaCommand(
    _In_ const GUID& Id)
{
    for (const GUID& Approved : RxApprovedMetaCommands)
    {
        if (IsEqualGUID(Approved, Id))
            return TRUE;
    }
    return FALSE;
}

/*
 * The meta commands the driver lists that applications may use, in the driver's order.
 * Descs is allocated with new[] and may be nullptr when Count is 0.
 */
static
HRESULT
RxListMetaCommands(
    _In_ RxDevice12& Device,
    _Outptr_result_maybenull_ D3D12_META_COMMAND_DESC** Descs,
    _Out_ UINT* Count)
{
    D3D12DDIARG_META_COMMAND_DESC* Driver;
    D3D12_META_COMMAND_DESC* Usable;
    UINT DriverCount = 0;
    UINT Index;
    HRESULT hr;

    *Descs = nullptr;
    *Count = 0;

    if (Device.Ddi().pfnEnumerateMetaCommands == nullptr)
        return S_OK;

    hr = Device.Ddi().pfnEnumerateMetaCommands(Device.Handle(), &DriverCount, nullptr);
    if (FAILED(hr) || DriverCount == 0)
        return hr;

    Driver = new (std::nothrow) D3D12DDIARG_META_COMMAND_DESC[DriverCount]();
    Usable = new (std::nothrow) D3D12_META_COMMAND_DESC[DriverCount]();
    if (Driver == nullptr || Usable == nullptr)
    {
        delete[] Driver;
        delete[] Usable;
        return E_OUTOFMEMORY;
    }

    hr = Device.Ddi().pfnEnumerateMetaCommands(Device.Handle(), &DriverCount, Driver);
    if (SUCCEEDED(hr))
    {
        for (Index = 0; Index < DriverCount; Index++)
        {
            if (!RxIsApprovedMetaCommand(Driver[Index].Id) && !RxDeveloperMode12())
                continue;

            Usable[*Count].Id = Driver[Index].Id;
            Usable[*Count].Name = Driver[Index].Name;
            Usable[*Count].InitializationDirtyState = (D3D12_GRAPHICS_STATES)Driver[Index].InitializationDirtyState;
            Usable[*Count].ExecutionDirtyState = (D3D12_GRAPHICS_STATES)Driver[Index].ExecutionDirtyState;
            *Count += 1;
        }
    }

    delete[] Driver;
    if (FAILED(hr))
    {
        delete[] Usable;
        *Count = 0;
        return hr;
    }

    *Descs = Usable;
    return S_OK;
}

/* A command id the device lists for applications, or DXGI_ERROR_UNSUPPORTED */
static
HRESULT
RxCheckMetaCommand(
    _In_ RxDevice12& Device,
    _In_ const GUID& Id)
{
    D3D12_META_COMMAND_DESC* Descs;
    UINT Count;
    UINT Index;
    HRESULT hr;

    hr = RxListMetaCommands(Device, &Descs, &Count);
    if (FAILED(hr))
        return hr;

    hr = DXGI_ERROR_UNSUPPORTED;
    for (Index = 0; Index < Count; Index++)
    {
        if (IsEqualGUID(Descs[Index].Id, Id))
        {
            hr = S_OK;
            break;
        }
    }

    delete[] Descs;
    if (FAILED(hr))
        RX_TRACE(L"meta command is not one the device lists");
    return hr;
}

/* Power and background work */

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetStablePowerState(
    BOOL Enable)
{
    D3DKMT_SETSTABLEPOWERSTATE Power = {};
    const KmtThunks* Kmt = RxGetKmtThunks();

    /* Outside developer mode the request costs the application its device */
    if (!RxDeveloperMode12())
    {
        RX_TRACE(L"SetStablePowerState needs developer mode, removing the device");
        MarkRemoved(DXGI_ERROR_INVALID_CALL);
        return E_FAIL;
    }

    if (Kmt->SetStablePowerState == nullptr)
        return E_NOTIMPL;

    Power.hAdapter = Adapter->GetKmtHandle();
    Power.Enabled = Enable;
    return RxKernelResult(*this, Kmt->SetStablePowerState(&Power), "D3DKMTSetStablePowerState", nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetBackgroundProcessingMode(
    D3D12_BACKGROUND_PROCESSING_MODE Mode,
    D3D12_MEASUREMENTS_ACTION MeasurementsAction,
    _In_opt_ HANDLE hEventToSignalUponCompletion,
    _Out_opt_ BOOL* pbFurtherMeasurementsDesired)
{
    BOOL Further = FALSE;
    HRESULT hr = S_OK;

    if ((UINT)Mode > D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM ||
        (UINT)MeasurementsAction > D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS)
    {
        return E_INVALIDARG;
    }

    /* Only committing results finishes in a way an event can report */
    if (hEventToSignalUponCompletion != nullptr && MeasurementsAction != D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS &&
        MeasurementsAction != D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY)
    {
        hr = E_INVALIDARG;
    }

    if ((Mode == D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_BACKGROUND_WORK ||
         Mode == D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM ||
         MeasurementsAction == D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY) &&
        !RxDeveloperMode12())
    {
        hr = DXGI_ERROR_SDK_COMPONENT_MISSING;
    }
    if (FAILED(hr))
        return hr;

    if (!BackgroundProcessing || CoreFunctions.pfnSetBackgroundProcessingMode == nullptr)
    {
        if (hEventToSignalUponCompletion != nullptr)
            SetEvent(hEventToSignalUponCompletion);
        if (pbFurtherMeasurementsDesired != nullptr)
            *pbFurtherMeasurementsDesired = FALSE;
        return S_OK;
    }

    CoreFunctions.pfnSetBackgroundProcessingMode(DriverDevice, (D3D12DDI_BACKGROUND_PROCESSING_MODE_0062)Mode,
                                                 (D3D12DDI_MEASUREMENTS_ACTION_0062)MeasurementsAction,
                                                 pbFurtherMeasurementsDesired != nullptr ? pbFurtherMeasurementsDesired
                                                                                         : &Further);
    if (hEventToSignalUponCompletion != nullptr)
        RxSignalAfterBackgroundWork12(hEventToSignalUponCompletion);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxDevice12::RemoveDevice(VOID)
{
    RX_TRACE(L"application removed the device");
    MarkRemoved(DXGI_ERROR_DEVICE_REMOVED);
}

D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS
STDMETHODCALLTYPE
RxDevice12::CheckDriverMatchingIdentifier(
    _In_ D3D12_SERIALIZED_DATA_TYPE SerializedDataType,
    _In_ const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER* pIdentifierToCheck)
{
    static_assert(sizeof(D3D12DDI_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER_0054) ==
                  sizeof(D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER));

    return (D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS)CoreFunctions.pfnCheckDriverMatchingIdentifier(
        DriverDevice, (D3D12DDI_SERIALIZED_DATA_TYPE)SerializedDataType,
        reinterpret_cast<const D3D12DDI_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER_0054*>(pIdentifierToCheck));
}

/* Shader caches */

HRESULT
STDMETHODCALLTYPE
RxDevice12::ShaderCacheControl(
    D3D12_SHADER_CACHE_KIND_FLAGS Kinds,
    D3D12_SHADER_CACHE_CONTROL_FLAGS Control)
{
    constexpr UINT Toggles = D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE | D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE;

    if ((Kinds & ~0x1F) != 0 || (Control & ~0x7) != 0 || Kinds == 0 || Control == 0 ||
        (Control & Toggles) == Toggles)
    {
        return E_INVALIDARG;
    }

    /* Enabling what is not disabled, or disabling what already is, is refused */
    if ((Control & D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE) != 0 && (Kinds & ~DisabledShaderCaches) != 0)
        return E_INVALIDARG;
    if ((Control & D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE) != 0 && (Kinds & DisabledShaderCaches) != 0)
        return E_INVALIDARG;

    if (!RxDeveloperMode12())
        return DXGI_ERROR_SDK_COMPONENT_MISSING;

    if ((Control & D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE) != 0)
        DisabledShaderCaches |= Kinds;
    else if ((Control & D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE) != 0)
        DisabledShaderCaches &= ~Kinds;

    /* Clearing empties the runtime's store for the driver and the stores of this device's sessions */
    if ((Control & D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR) != 0)
    {
        if ((Kinds & RxCacheKindDriver12) != 0 && DriverShaderCache != nullptr)
            DriverShaderCache->Clear();
        if ((Kinds & RxCacheKindApplication12) != 0)
            RxShaderCacheSession::ClearKinds(*this, Kinds);
    }

    /* The driver's own cache is told directly */
    if ((Kinds & D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_DRIVER_MANAGED) != 0 &&
        Options.DriverManagedShaderCachePresent && CoreFunctions.pfnImplicitShaderCacheControl != nullptr)
    {
        CoreFunctions.pfnImplicitShaderCacheControl(DriverDevice, (D3D12DDI_IMPLICIT_SHADER_CACHE_CONTROL_FLAGS_0080)Control);
    }

    return S_OK;
}

/* Lifetime trackers */

HRESULT
STDMETHODCALLTYPE
RxLifetimeTracker::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12LifetimeTracker) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12LifetimeTracker*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Objects come under a tracker only through protected workloads, so none is ever found */
HRESULT
STDMETHODCALLTYPE
RxLifetimeTracker::DestroyOwnedObject(
    _In_ ID3D12DeviceChild* pObject)
{
    UNREFERENCED_PARAMETER(pObject);
    return E_INVALIDARG;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateLifetimeTracker(
    _In_ ID3D12LifetimeOwner* pOwner,
    REFIID riid,
    _COM_Outptr_ void** ppvTracker)
{
    RxLifetimeTracker* Tracker;

    if (ppvTracker != nullptr)
        *ppvTracker = nullptr;
    if (pOwner == nullptr)
        return E_INVALIDARG;
    if (ppvTracker == nullptr)
        return S_FALSE;

    Tracker = new (std::nothrow) RxLifetimeTracker(*this, pOwner);
    if (Tracker == nullptr)
        return E_OUTOFMEMORY;

    return RxHandOut12(Tracker, riid, ppvTracker);
}

/* Meta commands */

HRESULT
RxMetaCommand::Create(
    _In_ RxDevice12& Device,
    _In_ const GUID& CommandId,
    _In_ UINT DriverNodeMask,
    _In_reads_bytes_opt_(ParametersSize) const void* Parameters,
    _In_ SIZE_T ParametersSize,
    _Outptr_ RxMetaCommand** Command)
{
    RxMetaCommand* Created;
    HRESULT hr;

    *Command = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return DXGI_ERROR_DEVICE_REMOVED;

    Created = new (std::nothrow) RxMetaCommand(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(CommandId, DriverNodeMask, Parameters, ParametersSize);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Command = Created;
    return S_OK;
}

HRESULT
RxMetaCommand::Initialize(
    _In_ const GUID& CommandId,
    _In_ UINT DriverNodeMask,
    _In_reads_bytes_opt_(ParametersSize) const void* Parameters,
    _In_ SIZE_T ParametersSize)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDI_HRTMETACOMMAND_0052 Runtime;
    SIZE_T Size;
    HRESULT hr;

    Size = Ddi.pfnCalcPrivateMetaCommandSize(Device.Handle(), CommandId, DriverNodeMask, Parameters, ParametersSize);
    DriverCommand.pDrvPrivate = Memory.Allocate(Size);
    if (DriverCommand.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    Runtime.handle = this;
    hr = Ddi.pfnCreateMetaCommand(Device.Handle(), CommandId, DriverNodeMask, Parameters, ParametersSize, DriverCommand,
                                  Runtime);
    if (SUCCEEDED(hr))
        hr = RxDevice12::TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateMetaCommand failed 0x%08lX", hr);
        return hr;
    }

    IsDriverCommandCreated = TRUE;
    return S_OK;
}

RxMetaCommand::~RxMetaCommand()
{
    if (IsDriverCommandCreated)
        Device.Ddi().pfnDestroyMetaCommand(Device.Handle(), DriverCommand);
}

HRESULT
STDMETHODCALLTYPE
RxMetaCommand::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12MetaCommand) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12MetaCommand*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

UINT64
STDMETHODCALLTYPE
RxMetaCommand::GetRequiredParameterResourceSize(
    _In_ D3D12_META_COMMAND_PARAMETER_STAGE Stage,
    _In_ UINT ParameterIndex)
{
    D3D12DDIARG_META_COMMAND_REQUIRED_PARAMETER_INFO Info = {};

    Device.Ddi().pfnGetMetaCommandRequiredParameterInfo(DriverCommand, (D3D12DDI_META_COMMAND_PARAMETER_STAGE)Stage,
                                                        ParameterIndex, &Info);
    return Info.ResourceSize;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::EnumerateMetaCommands(
    _Inout_ UINT* pNumMetaCommands,
    _Out_writes_opt_(*pNumMetaCommands) D3D12_META_COMMAND_DESC* pDescs)
{
    D3D12_META_COMMAND_DESC* Descs;
    UINT Count;
    UINT Room;
    HRESULT hr;

    if (pNumMetaCommands == nullptr)
        return E_INVALIDARG;

    hr = RxListMetaCommands(*this, &Descs, &Count);
    if (FAILED(hr))
        return hr;

    Room = *pNumMetaCommands;
    *pNumMetaCommands = Count;
    if (pDescs != nullptr)
        memcpy(pDescs, Descs, (Room < Count ? Room : Count) * sizeof(*pDescs));

    delete[] Descs;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::EnumerateMetaCommandParameters(
    _In_ REFGUID CommandId,
    _In_ D3D12_META_COMMAND_PARAMETER_STAGE Stage,
    _Out_opt_ UINT* pTotalStructureSizeInBytes,
    _Inout_ UINT* pParameterCount,
    _Out_writes_opt_(*pParameterCount) D3D12_META_COMMAND_PARAMETER_DESC* pParameterDescs)
{
    D3D12DDIARG_META_COMMAND_PARAMETER_DESC* Driver;
    D3D12_META_COMMAND_PARAMETER_DESC* Api;
    UINT Count = 0;
    UINT Offset = 0;
    UINT Room;
    UINT Index;
    HRESULT hr;

    if (pParameterCount == nullptr)
        return E_INVALIDARG;

    hr = RxCheckMetaCommand(*this, CommandId);
    if (FAILED(hr))
        return hr;

    hr = CoreFunctions.pfnEnumerateMetaCommandParameters(DriverDevice, CommandId,
                                                         (D3D12DDI_META_COMMAND_PARAMETER_STAGE)Stage, &Count, nullptr);
    if (FAILED(hr) || Count == 0)
        return hr;

    Driver = new (std::nothrow) D3D12DDIARG_META_COMMAND_PARAMETER_DESC[Count]();
    Api = new (std::nothrow) D3D12_META_COMMAND_PARAMETER_DESC[Count]();
    if (Driver == nullptr || Api == nullptr)
    {
        delete[] Driver;
        delete[] Api;
        return E_OUTOFMEMORY;
    }

    hr = CoreFunctions.pfnEnumerateMetaCommandParameters(DriverDevice, CommandId,
                                                         (D3D12DDI_META_COMMAND_PARAMETER_STAGE)Stage, &Count, Driver);

    /* Parameters sit back to back in the structure the application fills */
    for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
    {
        Api[Index].Name = Driver[Index].Name;
        Api[Index].Type = (D3D12_META_COMMAND_PARAMETER_TYPE)Driver[Index].Type;
        Api[Index].Flags = (D3D12_META_COMMAND_PARAMETER_FLAGS)Driver[Index].Flags;
        Api[Index].RequiredResourceState = (D3D12_RESOURCE_STATES)Driver[Index].RequiredResourceState;
        Api[Index].StructureOffset = Offset;

        switch (Api[Index].Type)
        {
            case D3D12_META_COMMAND_PARAMETER_TYPE_FLOAT:
                Offset += sizeof(FLOAT);
                break;
            case D3D12_META_COMMAND_PARAMETER_TYPE_UINT64:
            case D3D12_META_COMMAND_PARAMETER_TYPE_GPU_VIRTUAL_ADDRESS:
            case D3D12_META_COMMAND_PARAMETER_TYPE_CPU_DESCRIPTOR_HANDLE_HEAP_TYPE_CBV_SRV_UAV:
            case D3D12_META_COMMAND_PARAMETER_TYPE_GPU_DESCRIPTOR_HANDLE_HEAP_TYPE_CBV_SRV_UAV:
                Offset += sizeof(UINT64);
                break;
            default:
                hr = E_INVALIDARG;
                break;
        }
    }

    if (SUCCEEDED(hr))
    {
        if (pTotalStructureSizeInBytes != nullptr)
            *pTotalStructureSizeInBytes = Offset;

        Room = *pParameterCount;
        *pParameterCount = Count;
        if (pParameterDescs != nullptr)
            memcpy(pParameterDescs, Api, (Room < Count ? Room : Count) * sizeof(*pParameterDescs));
    }

    delete[] Driver;
    delete[] Api;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateMetaCommand(
    _In_ REFGUID CommandId,
    _In_ UINT NodeMask,
    _In_reads_bytes_opt_(CreationParametersDataSizeInBytes) const void* pCreationParametersData,
    _In_ SIZE_T CreationParametersDataSizeInBytes,
    REFIID riid,
    _COM_Outptr_ void** ppMetaCommand)
{
    RxMetaCommand* Command;
    UINT DriverMask;
    HRESULT hr;

    /* Validation failures leave the output as the application had it */
    hr = RxCheckMetaCommand(*this, CommandId);
    if (FAILED(hr))
        return hr;

    hr = TranslateNodeMask(NodeMask, TRUE, &DriverMask);
    if (FAILED(hr))
        return hr;
    if (ppMetaCommand == nullptr)
        return S_FALSE;
    *ppMetaCommand = nullptr;

    hr = RxMetaCommand::Create(*this, CommandId, DriverMask, pCreationParametersData, CreationParametersDataSizeInBytes,
                               &Command);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Command, riid, ppMetaCommand);
}
