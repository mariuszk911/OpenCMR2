// Bounds and truncation behaviour of the fork-owned credits text parser.
// The buffer is scanned byte by byte with an explicit end pointer, so the
// cases below cover what the game's files can contain and what a truncated
// copy of one looks like.
#include "port/credits_text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void Check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "credits_text: %s\n", what);
        std::exit(1);
    }
}

struct Parsed {
    std::vector<char> storage;
    std::vector<char *> entries;
    size_t count;

    explicit Parsed(const std::string &text, size_t capacity = 16)
        : storage(text.size() + 2, '#'), entries(capacity * 2, nullptr), count(0)
    {
        std::memcpy(&storage[1], text.data(), text.size());
        count = CreditsText_Parse(&storage[1], text.size(), entries.data(), capacity);
    }

    std::string Role(size_t index) const { return Text(index * 2); }
    std::string Name(size_t index) const { return Text(index * 2 + 1); }

    std::string Text(size_t index) const
    {
        if (entries[index] == nullptr)
            return std::string();
        return std::string(entries[index]);
    }
};

void CheckSentinels(const Parsed &parsed)
{
    Check(parsed.storage.front() == '#', "write before the buffer start");
    Check(parsed.storage.back() == '#', "write past the buffer end");
}

void TestWellFormed()
{
    Parsed parsed("\"producer\",\t\"guy wilday\"\r\n\"\"\t\t\"\"\r\n\"artists\", \"a b\"\r\n");
    Check(parsed.count == 3, "well-formed pair count");
    Check(parsed.Role(0) == "producer" && parsed.Name(0) == "guy wilday", "first pair");
    Check(parsed.Role(1).empty() && parsed.Name(1).empty(), "empty pair");
    Check(parsed.Role(2) == "artists" && parsed.Name(2) == "a b", "last pair");
    CheckSentinels(parsed);
}

void TestTruncatedName()
{
    Parsed parsed("\"a\", \"b\"\r\n\"c\", \"d");
    Check(parsed.count == 1, "truncated name stops at the last complete pair");
    Check(parsed.Role(0) == "a" && parsed.Name(0) == "b", "complete pair kept");
    CheckSentinels(parsed);
}

void TestMissingClosingQuote()
{
    Parsed parsed("\"a\", \"b\"\r\n\"c, \"d\"");
    Check(parsed.count == 1, "missing role quote stops the scan");
    Check(parsed.Role(0) == "a" && parsed.Name(0) == "b", "previous pair kept");
    CheckSentinels(parsed);
}

void TestNoQuotes()
{
    Parsed parsed("plain text without quotes\r\n");
    Check(parsed.count == 0, "text without quotes has no entries");
    CheckSentinels(parsed);
}

void TestEmpty()
{
    Parsed parsed("");
    Check(parsed.count == 0, "empty file has no entries");
}

void TestNoTerminator()
{
    const std::string text = "\"role\", \"name\"";
    std::vector<char> storage(text.begin(), text.end());
    std::vector<char *> entries(2, nullptr);
    const size_t count = CreditsText_Parse(storage.data(), storage.size(), entries.data(), 1);
    Check(count == 1, "unterminated buffer still parses the complete pair");
    Check(std::strcmp(entries[0], "role") == 0, "unterminated role");
    Check(std::strcmp(entries[1], "name") == 0, "unterminated name");
}

void TestLongString()
{
    const std::string longName(5000, 'x');
    Parsed parsed("\"role\", \"" + longName + "\"\r\n");
    Check(parsed.count == 1, "long pair count");
    Check(parsed.Name(0) == longName, "long name is not truncated");
    CheckSentinels(parsed);
}

void TestCapacity()
{
    Parsed parsed("\"a\", \"b\"\r\n\"c\", \"d\"\r\n\"e\", \"f\"\r\n", 2);
    Check(parsed.count == 2, "capacity limits the written pairs");
    Check(parsed.Role(0) == "a" && parsed.Name(0) == "b", "first pair within capacity");
    Check(parsed.Role(1) == "c" && parsed.Name(1) == "d", "second pair within capacity");
    CheckSentinels(parsed);
}

void TestNullBuffer()
{
    char *entry = nullptr;
    Check(CreditsText_Parse(nullptr, 16, &entry, 1) == 0, "null buffer is rejected");
}

} // namespace

int main()
{
    TestWellFormed();
    TestTruncatedName();
    TestMissingClosingQuote();
    TestNoQuotes();
    TestEmpty();
    TestNoTerminator();
    TestLongString();
    TestCapacity();
    TestNullBuffer();
    std::puts("credits_text: in-place pairs, truncation, long names and capacity passed");
    return 0;
}
