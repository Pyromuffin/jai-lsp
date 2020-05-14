#pragma once
#include <vector>
#include <iostream>
#include <tree_sitter/api.h>



class GapBuffer
{
    static constexpr char newline = '\n';

    int currentLine = 0;
    int currentCol = 0;

public:
    std::vector<char> before;
    std::vector<char> after;

private:
    char Advance()
    {
        before.push_back(after.back());
        after.pop_back();

        return before.back();
    }

    char Retreat()
    {
        after.push_back(before.back());
        before.pop_back();

        return after.back();
    }

    void SeekLine(int line)
    {
        while (currentLine < line)
        {
            if (Advance() == newline)
            {
                currentLine++;
                currentCol = 0;
            }
        }

        while (currentLine > line)
        {
            if (Retreat() == newline)
            {
                currentLine--;
                currentCol = -1;
            }
        }
    }

    void CalculateCurrentColumnAtEdge()
    {
        if (currentLine == 0)
        {
            currentCol = static_cast<int>(before.size());
        }
        else
        {
            int col = 0;
            auto back = before.back();
            while (back != newline)
            {
                col++;
                back = before[before.size() - 1 - col];
            }

            currentCol = col;
        }
    }

  

public:

    GapBuffer() {}

    GapBuffer(const char* initialContent, int length)
    {
        for (int i = 0; i < length; i++)
        {
            int index = length - i - 1;
            after.push_back(initialContent[index]);
        }
    }

    char GetChar(int index)
    {
        if (index < before.size())
        {
            return before[index];
        }
        else
        {
            index -= static_cast<int>(before.size());
            return after[after.size() - index - 1];
        }
    }

    void Seek(int line, int col)
    {
        SeekLine(line);
        if (currentCol == -1)
        {
            CalculateCurrentColumnAtEdge();
        }

        while (currentCol < col)
        {
            Advance();
            currentCol++;
        }

        while (currentCol > col)
        {
            Retreat();
            currentCol--;
        }
    }

    void InsertAtCursor(const char* content, int length)
    {
        for (int i = 0; i < length; i++)
        {
            before.push_back(content[i]);
            if (content[i] == newline)
            {
                currentLine++;
                currentCol = 0;
            }
            else
            {
                currentCol++;
            }
        }
    }

    /*
    The idea is that start and old_end describe the range of text that was removed,
    and start and new_end describe the new range of text that was inserted. 
    So for insertions, old_end == start, and for deletions, new_end == start.
    */


    TSInputEdit Edit(int line, int col, int endLine, int endCol, const char* content, int contentLength, int rangeLength)
    {
        Seek(line, col);

        TSInputEdit edit;
        edit.start_byte = GetOffset();
        edit.start_point = { .row = static_cast<uint32_t>(line), .column = static_cast<uint32_t>(col) };
        edit.old_end_byte = edit.start_byte + rangeLength;
        edit.old_end_point = { .row = static_cast<uint32_t>(endLine), .column = static_cast<uint32_t>(endCol) };

        after.resize(after.size() - rangeLength);

        InsertAtCursor(content, contentLength);
        edit.new_end_byte = edit.start_byte + contentLength;
        edit.new_end_point = { .row = static_cast<uint32_t>(currentLine), .column = static_cast<uint32_t>(currentCol) };
        
        return edit;
    }


    int GetOffset()
    {
        return static_cast<int>(before.size());
    }


    void PrintContents()
    {
        for (auto c : before)
        {
            std::cout << c;
        }

        for (int i = 0; i < after.size(); i++)
        {
            int index = static_cast<int>(after.size()) - i - 1;
            std::cout << after[index];
        }
    }

    // you gotta free this
    char* Copy()
    {
        auto storage = (char*) malloc(before.size() + after.size() + 1);
        if(before.size() > 0)
            memcpy(storage, &before[0], before.size());

        if (after.size() > 0)
        {
            for (int i = 0; i < after.size(); i++)
            {
                int index = static_cast<int>(after.size()) - i - 1;
                storage[before.size() + i] = after[index];
            }
        }
     
        storage[before.size() + after.size()] = '\0';

        return storage;
    }

};


struct buffer_view
{
    GapBuffer* buffer;
    uint32_t start;
    uint32_t length;

    
    bool operator ==(const buffer_view& rhs) const
    {
        if (length != rhs.length)
            return false;

        for (uint32_t i = start; i < length; i++)
        {
            if (buffer->GetChar(i) != rhs.buffer->GetChar(i))
                return false;
        }

        return true;
    }

    bool operator ==(const std::string_view& rhs) const
    {
        if (length != rhs.size())
            return false;

        for (uint32_t i = start; i < length; i++)
        {
            if (buffer->GetChar(i) != rhs[i])
                return false;
        }

        return true;
    }

    std::unique_ptr<char[]> Copy()
    {
        auto ptr = std::make_unique<char[]>(length + 1);
        for (uint32_t i = 0; i < length; i++)
            ptr.get()[i] = buffer->GetChar(start + i);
        
        ptr.get()[length] = '\0';
        return ptr;
    }
};


namespace std {
    template <> struct hash<buffer_view>
    {
        size_t operator()(const buffer_view& view) const
        {
            auto storage = (char*)_malloca(view.length); // lol is this a bomb
            auto hasher = hash<string_view>();

            for (uint32_t i = 0; i < view.length; i++)
            {
                storage[i] = view.buffer->GetChar(view.start + i);
            }

            auto sv = string_view(storage, view.length);
            auto hash = hasher(sv);
            _freea(storage);

            return hash;
        }
    };
}