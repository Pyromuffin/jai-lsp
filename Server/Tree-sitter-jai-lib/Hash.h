#pragma once

#if _DEBUG
#include <string>
#endif

struct Hash
{
    uint64_t value;
    bool operator==(Hash const& rOther) const { return value == rOther.value; }
#if _DEBUG
    std::string debug_name;
#endif
};


namespace std {
    template <> struct hash<Hash>
    {
        inline size_t operator()(const Hash& h) const
        {
            return h.value;
        }
    };
}

