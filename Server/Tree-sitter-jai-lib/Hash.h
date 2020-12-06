#pragma once

#define HASH_DEBUG_STRING 0



struct Hash
{
    uint64_t value;
    bool operator==(Hash const& rOther) const { return value == rOther.value; }
#if HASH_DEBUG_STRING
    char* debug_name;
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

