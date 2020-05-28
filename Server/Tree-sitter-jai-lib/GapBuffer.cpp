#include "GapBuffer.h"

char GapBuffer::Advance()
{
    before.push_back(after.back());
    after.pop_back();

    return before.back();
}

char GapBuffer::Retreat()
{
    after.push_back(before.back());
    before.pop_back();

    return after.back();
}

void GapBuffer::SeekLine(int line)
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

void GapBuffer::CalculateCurrentColumnAtEdge()
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

GapBuffer::GapBuffer() {}

bool GapBuffer::IsRewound()
{
    return after.size() == 0;
}

void GapBuffer::Rewind()
{
    for (int i = 0; i < after.size(); i++)
    {
        before.push_back(after.back());
        after.pop_back();
    }
}

char GapBuffer::GetChar(int index)
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

void GapBuffer::Seek(int line, int col)
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

void GapBuffer::InsertAtCursor(const char* content, int length)
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

GapBuffer::GapBuffer(const char* initialContent, int length)
{
    InsertAtCursor(initialContent, length);
}

std::string_view GapBuffer::GetStringView(int start, int length)
{
    assert(IsRewound());
    auto view = std::string_view(&before[start], length);
    return view;
}

std::string_view GapBuffer::GetEntireStringView()
{
    assert(IsRewound());
    auto view = std::string_view(&before[0], before.size());
    return view;
}

TSInputEdit GapBuffer::Edit(int line, int col, int endLine, int endCol, const char* content, int contentLength, int rangeLength)
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

int GapBuffer::GetOffset()
{
    return static_cast<int>(before.size());
}

void GapBuffer::PrintContents()
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
 char* GapBuffer::Copy()
{
    auto storage = (char*)malloc(before.size() + after.size() + 1);
    if (before.size() > 0)
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

buffer_view::buffer_view(int start, int end, GapBuffer* buffer)
{
    this->start = start;
    this->length = end - start;
    this->buffer = buffer;
}

std::unique_ptr<char[]> buffer_view::Copy()
{
    auto ptr = std::make_unique<char[]>(length + 1);
    for (uint32_t i = 0; i < length; i++)
        ptr.get()[i] = buffer->GetChar(start + i);

    ptr.get()[length] = '\0';
    return ptr;
}
