// Copyright Universe Project. All Rights Reserved.

#include "UniverseHash.h"

namespace UniverseHash
{
    uint64 HashString(const TCHAR* Text)
    {
        // FNV-1a over UTF-16 code units.
        //
        // Code units rather than bytes: wchar_t is 16 bits on Windows and 32
        // on Linux, so hashing the raw object representation would give a
        // different universe on different platforms. Masking each character to
        // 16 bits and feeding the halves in a fixed order makes the result
        // depend only on the text.
        uint64 Hash = 0xCBF29CE484222325ull;   // FNV offset basis

        if (Text != nullptr)
        {
            for (const TCHAR* Cursor = Text; *Cursor != 0; ++Cursor)
            {
                const uint32 CodeUnit = static_cast<uint32>(*Cursor) & 0xFFFFu;
                Hash ^= static_cast<uint64>(CodeUnit & 0xFFu);
                Hash *= 0x100000001B3ull;      // FNV prime
                Hash ^= static_cast<uint64>((CodeUnit >> 8) & 0xFFu);
                Hash *= 0x100000001B3ull;
            }
        }

        // FNV-1a has weak avalanche in its high bits; the finaliser fixes that
        // so short seed phrases ("a", "b") land far apart in the seed space.
        return Mix64(Hash);
    }
}
