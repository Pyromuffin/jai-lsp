#pragma once

#include <vector>
#include <tree_sitter/api.h>
#include <string_view>

#include "Hash.h"



class GapBuffer
{
    static constexpr char newline = '\n';

    int currentLine = 0;
    int currentCol = 0;

public:
    std::vector<char> before;
    std::vector<char> after;

private:
    char Advance();
    char Retreat();
    void SeekLine(int line);
    void CalculateCurrentColumnAtEdge();

public:

    GapBuffer();
    bool IsRewound();
    void Rewind();
    inline char GetChar(int index) const;
    void Seek(int line, int col);
    void InsertAtCursor(const char* content, int length);
    GapBuffer(const char* initialContent, int length);

    // this is slightly dangerous, because you can hold on to a string view for longer than it might be valid.
    // mostly just use this for module parsing and initial tree creation.
    std::string_view GetStringView(int start, int length);
    std::string_view GetEntireStringView();
    void GetRowCopy(int row, std::string& s);

    /*
    The idea is that start and old_end describe the range of text that was removed,
    and start and new_end describe the new range of text that was inserted.
    So for insertions, old_end == start, and for deletions, new_end == start.
    */
    TSInputEdit Edit(int line, int col, int endLine, int endCol, const char* content, int contentLength, int rangeLength);
    uint32_t GetOffset();
    void PrintContents();

    // you gotta free this
    char* Copy();
};


struct buffer_view
{
    const GapBuffer* buffer;
    uint32_t start;
    uint32_t length;

    buffer_view() = default;
    buffer_view(int start, int end, const GapBuffer* buffer);

    std::string Copy();
    char* CopyMalloc();
};


inline Hash StringHash(std::string_view string)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    Hash h;

#if HASH_DEBUG_STRING
    h.debug_name = new char[string.length() + 1];
    h.debug_name[string.length()] = '\0';
#endif


    for (uint32_t i = 0; i < string.length(); i++)
    {
        hash = hash ^ string[i];
        hash = hash * 0x00000100000001B3ULL;

#if HASH_DEBUG_STRING
        h.debug_name[i]= string[i];
#endif
    }

    h.value = hash;

    return h;
}

inline Hash StringHash(buffer_view string)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    Hash h;

#if HASH_DEBUG_STRING
    h.debug_name = new char[string.length + 1];
    h.debug_name[string.length] = '\0';
#endif

    for (uint32_t i = 0; i < string.length; i++)
    {
        auto c = string.buffer->GetChar(string.start + i);
        hash = hash ^ c;
        hash = hash * 0x00000100000001B3ULL;

#if HASH_DEBUG_STRING
        h.debug_name[i] = c;
#endif
    }

    h.value = hash;

    return h;
}


inline Hash GetIdentifierHash(const TSNode& node, std::string_view code)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    auto length = end - start;
    return StringHash(std::string_view(&code[start], length));
}

inline Hash GetIdentifierHash(const TSNode& node, const GapBuffer* buffer)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    return StringHash(buffer_view(start, end, buffer));
}

inline std::string_view GetIdentifier(const TSNode& node, std::string_view code)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    auto length = end - start;
    return std::string_view(&code[start], length);
}


inline buffer_view GetIdentifierFromBuffer(const TSNode& node, const GapBuffer* buffer)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    return buffer_view(start, end, buffer);
}


inline std::string GetIdentifierFromBufferCopy(const TSNode& node, const GapBuffer* buffer)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    return buffer_view(start, end, buffer).Copy();
}
