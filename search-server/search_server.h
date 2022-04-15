#pragma once

#include <algorithm>
#include <cmath>
#include <execution>
#include <list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"


const int MAX_RESULT_DOCUMENT_COUNT = 5;

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    
    explicit SearchServer(const std::string& stop_words);
    explicit SearchServer(std::string_view stop_word_text);
    
    void AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;
            
    template <typename DocumentPredicate, typename Policy>
    std::vector<Document> FindTopDocuments(const Policy& policy, std::string_view raw_query, DocumentPredicate document_predicate) const;
    template <typename Policy>
    std::vector<Document> FindTopDocuments(const Policy& policy, std::string_view raw_query, DocumentStatus status) const;
    template <typename Policy>
    std::vector<Document> FindTopDocuments(const Policy& policy, std::string_view raw_query) const;

    int GetDocumentCount() const;
    
    std::set<int>::const_iterator begin() const;
    std::set<int>::const_iterator end() const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;
    
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const;
    
    void RemoveDocument(int document_id);
      
    template <typename Policy>
    void RemoveDocument(Policy& policy, int document_id);
           
private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string text;
    };

    std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;
    
    bool IsStopWord(std::string_view word) const;
    static bool IsValidWord(std::string_view word);
    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;
    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(std::string_view text, bool skip_sort = false) const;

    double ComputeWordInverseDocumentFreq(std::string_view word) const;

    template <typename DocumentPredicate, class Policy>
std::vector<Document> FindAllDocuments(const Policy policy, const Query& query, DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words))
{
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        throw std::invalid_argument("Some of stop words are invalid");
    }
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}
    
template <typename DocumentPredicate, typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy& policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
    const auto query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    sort(policy,matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < 1e-6) {
            return lhs.rating > rhs.rating;
        } else {
            return lhs.relevance > rhs.relevance;
        }
    });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query,[status](int document_id, DocumentStatus document_status, int rating) {return document_status == status;});
}

template <typename Policy>
std::vector<Document> SearchServer::FindTopDocuments(const Policy& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename DocumentPredicate, typename Policy>
std::vector<Document> SearchServer::FindAllDocuments(const Policy policy, const Query& query, DocumentPredicate document_predicate) const {
    ConcurrentMap<int, double> document_to_relevance(8);

    const auto func = [&](std::string_view word) 
        { 
            if (word_to_document_freqs_.count(word) != 0) 
            { 
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word); 
                for (const auto& [document_id, term_freq] : (*word_to_document_freqs_.find(word)).second) 
                { 
                    const auto& document_data = documents_.at(document_id); 
                    if (document_predicate(document_id, document_data.status, document_data.rating) &&  
                        std::all_of(query.minus_words.begin(), query.minus_words.end(), [&](std::string_view minus_word) 
                            { 
                                return (document_to_word_freqs_.at(document_id).count(minus_word) == 0); 
                            })
                        ) 
                    { 
                        document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq; 
                    } 
                }
            }
        };

    std::for_each(policy, query.plus_words.begin(), query.plus_words.end(), func);

    std::map<int, double> m_doc_to_relevance = document_to_relevance.BuildOrdinaryMap();
    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : m_doc_to_relevance) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}

template <typename Policy>
void SearchServer::RemoveDocument(Policy& policy, int document_id) {
    if (document_to_word_freqs_.count(document_id) == 0) {
        return;
    }

    document_ids_.erase(document_id);
    
    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    std::vector<std::string_view> words(word_freqs.size());
    transform(
        policy,
        word_freqs.begin(), word_freqs.end(),
        words.begin(),
        [](const auto& item) { return item.first; }
    );
    for_each(
        policy,
        words.begin(), words.end(),
        [this, document_id](std::string_view word) {
            word_to_document_freqs_.at(word).erase(document_id);
        });
    
    documents_.erase(document_id);
}
