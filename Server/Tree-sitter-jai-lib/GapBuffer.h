#pragma once
#include <vector>
#include <tree_sitter/api.h>
#include <string_view>


struct Hash
{
    uint64_t value;
    bool operator==(Hash const& rOther) const { return value == rOther.value; }
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
    inline char GetChar(int index);
    void Seek(int line, int col);
    void InsertAtCursor(const char* content, int length);
    GapBuffer(const char* initialContent, int length);

    // this is slightly dangerous, because you can hold on to a string view for longer than it might be valid.
    // mostly just use this for module parsing and initial tree creation.
    std::string_view GetStringView(int start, int length);

    std::string_view GetEntireStringView();

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
    GapBuffer* buffer;
    uint32_t start;
    uint32_t length;

    buffer_view() = default;
    buffer_view(int start, int end, GapBuffer* buffer);

    std::string Copy();
    char* CopyMalloc();
};


inline Hash StringHash(std::string_view string)
{
    uint64_t hash = 0xcbf29ce484222325ULL;

    for (uint32_t i = 0; i < string.length(); i++)
    {
        hash = hash ^ string[i];
        hash = hash * 0x00000100000001B3ULL;
    }

    return { hash };
}

inline Hash StringHash(buffer_view string)
{
    uint64_t hash = 0xcbf29ce484222325ULL;

    for (uint32_t i = 0; i < string.length; i++)
    {
        hash = hash ^ string.buffer->GetChar(string.start + i);
        hash = hash * 0x00000100000001B3ULL;
    }

    return { hash };
}
