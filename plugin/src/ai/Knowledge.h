#pragma once

// Retrieval over NOVA's own engineering knowledge base (shared/knowledge/*.md, embedded as
// binary data so it works offline). BM25 over heading-level chunks. Knowledge informs the
// engineer; it never overrides audio evidence.

#include <juce_core/juce_core.h>

#include <string>
#include <map>
#include <vector>

namespace nova::ai
{

class KnowledgeBase
{
public:
    struct Chunk { std::string doc, heading, text; std::vector<std::string> tokens; };
    struct Hit { const Chunk* chunk; double score; };

    KnowledgeBase();                                    // loads the embedded documents
    void addDocument (const std::string& name, const std::string& markdown);
    std::vector<Hit> search (const std::string& query, int k = 3) const;
    size_t size() const { return chunks.size(); }

    static KnowledgeBase& shared();

private:
    std::vector<Chunk> chunks;
    std::map<std::string, int> docFreq;
    double avgLen = 1;
    void reindex();
    static std::vector<std::string> tokenize (const std::string& s);
};

} // namespace nova::ai
