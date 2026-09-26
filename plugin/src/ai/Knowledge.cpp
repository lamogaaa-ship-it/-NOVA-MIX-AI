#include "Knowledge.h"

#include "NovaBinaryData.h"

#include <cmath>
#include <set>

namespace nova::ai
{

KnowledgeBase::KnowledgeBase()
{
    for (int i = 0; i < NovaBinary::namedResourceListSize; ++i)
    {
        const juce::String name (NovaBinary::originalFilenames[i]);
        if (! name.endsWithIgnoreCase (".md")) continue;
        int size = 0;
        if (const char* data = NovaBinary::getNamedResource (NovaBinary::namedResourceList[i], size))
            addDocument (name.upToLastOccurrenceOf (".", false, false).toStdString(), std::string (data, (size_t) size));
    }
}

KnowledgeBase& KnowledgeBase::shared()
{
    static KnowledgeBase kb;
    return kb;
}

std::vector<std::string> KnowledgeBase::tokenize (const std::string& s)
{
    static const std::set<std::string> stop { "the", "a", "an", "and", "or", "of", "to", "in", "on", "is", "it", "for", "with", "that", "this",
                                              "are", "be", "as", "at", "by", "not", "but", "its", "can", "has", "have", "from", "than", "so" };
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s)
    {
        if (std::isalnum (c) || c >= 128) cur += (char) std::tolower (c);
        else
        {
            if (cur.size() > 1 && ! stop.count (cur)) out.push_back (cur);
            cur.clear();
        }
    }
    if (cur.size() > 1 && ! stop.count (cur)) out.push_back (cur);
    // light stemming: plural / -ing
    for (auto& t : out)
    {
        if (t.size() > 4 && t.back() == 's' && t[t.size() - 2] != 's') t.pop_back();
        else if (t.size() > 6 && t.compare (t.size() - 3, 3, "ing") == 0) t.resize (t.size() - 3);
    }
    return out;
}

void KnowledgeBase::addDocument (const std::string& name, const std::string& md)
{
    juce::StringArray lines;
    lines.addLines (juce::String::fromUTF8 (md.c_str()));
    std::string title = name, heading, text;
    auto flush = [&]
    {
        if (! text.empty())
        {
            Chunk c;
            c.doc = title; c.heading = heading; c.text = text;
            c.tokens = tokenize (title + " " + heading + " " + heading + " " + text);
            chunks.push_back (std::move (c));
        }
        text.clear();
    };
    for (auto& l : lines)
    {
        if (l.startsWith ("# ")) { title = l.substring (2).trim().toStdString(); continue; }
        if (l.startsWith ("## ")) { flush(); heading = l.substring (3).trim().toStdString(); continue; }
        if (l.trim().isNotEmpty()) text += l.trim().toStdString() + " ";
    }
    flush();
    reindex();
}

void KnowledgeBase::reindex()
{
    docFreq.clear();
    double total = 0;
    for (auto& c : chunks)
    {
        std::set<std::string> uniq (c.tokens.begin(), c.tokens.end());
        for (auto& t : uniq) ++docFreq[t];
        total += (double) c.tokens.size();
    }
    avgLen = chunks.empty() ? 1.0 : total / (double) chunks.size();
}

std::vector<KnowledgeBase::Hit> KnowledgeBase::search (const std::string& query, int k) const
{
    const auto q = tokenize (query);
    std::vector<Hit> hits;
    const double N = (double) chunks.size(), k1 = 1.5, b = 0.75;
    for (auto& c : chunks)
    {
        double score = 0;
        for (auto& t : q)
        {
            auto df = docFreq.find (t);
            if (df == docFreq.end()) continue;
            const double tf = (double) std::count (c.tokens.begin(), c.tokens.end(), t);
            if (tf <= 0) continue;
            const double idf = std::log (1.0 + (N - df->second + 0.5) / (df->second + 0.5));
            score += idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * (double) c.tokens.size() / avgLen));
        }
        if (score > 0) hits.push_back ({ &c, score });
    }
    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& h) { return a.score > h.score; });
    if ((int) hits.size() > k) hits.resize ((size_t) k);
    return hits;
}

} // namespace nova::ai
