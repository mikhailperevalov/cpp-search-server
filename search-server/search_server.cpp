#include "search_server.h"

SearchServer::SearchServer(std::string_view stop_words_text)
        :SearchServer(SplitIntoWords(stop_words_text))
{}

SearchServer::SearchServer(const std::string& stop_words_text)
        : SearchServer(std::string_view(stop_words_text))
{}

void SearchServer::AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings) {
    if (document_id < 0) {
            throw std::invalid_argument("Отрицательный Id документа");
    }
    if (documents_.count(document_id) > 0) {
            throw std::invalid_argument("Документ с таким id уже есть в системе");
    }
    const auto [it, inserted] = documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, std::string(document)});
    const auto words = SplitIntoWordsNoStop(it->second.text);
    
    const double inv_word_count = 1.0 / words.size();
    for (const std::string_view word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
    document_ids_.insert(document_id);
}
  
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(std::execution::seq, raw_query,[status](int document_id, DocumentStatus document_status, int rating) {return document_status == status;});
}

std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    return FindTopDocuments(std::execution::seq, raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

std::set<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static std::map<std::string_view, double> words_freqs;

    if (!words_freqs.empty()) {
        words_freqs.clear();
    }
    
    if (document_id < 0 || !documents_.count(document_id)) {
        return words_freqs;
    }

    words_freqs = document_to_word_freqs_.at(document_id);
    return words_freqs;
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const {
    if (documents_.count(document_id) == 0) {
        throw std::invalid_argument("Документ не найден");
    }

    const Query query = ParseQuery(raw_query);
    const auto status = documents_.at(document_id).status;
    
    for (std::string_view word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                return {{}, status};
            }
        }
    
    std::vector<std::string_view> matched_words;
    for (std::string_view word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
    return {matched_words, status};
}
    
std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const {
    if (documents_.count(document_id) == 0) {
        throw std::invalid_argument("Документ не найден");
    }
    const auto status = documents_.at(document_id).status;
    const Query query = ParseQuery(raw_query, true);
    const auto word_checker = 
        [this, document_id](std::string_view word) {
            const auto it = word_to_document_freqs_.find(word);
            return it != word_to_document_freqs_.end() && it->second.count(document_id);
        };
    if (std::any_of(std::execution::par, query.minus_words.begin(), query.minus_words.end(), word_checker)) {
        return {{}, status};
    }
    std::vector<std::string_view> matched_words(query.plus_words.size());
    auto words_end = copy_if(
        std::execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        word_checker
    );
    std::sort(matched_words.begin(), words_end);
    words_end = std::unique(matched_words.begin(), words_end);
    matched_words.erase(words_end, matched_words.end());
    
    return {matched_words, status};
}


void SearchServer::RemoveDocument(int document_id) {
    RemoveDocument(std::execution::seq, document_id);
}

bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(std::string_view word) {
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    std::vector<std::string_view> words;
    for (std::string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Некорректный ввод: " + std::string(word));
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    if (text.empty()) {
        throw std::invalid_argument("Пустой запрос");
    }
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw std::invalid_argument("Некорректный ввод: " + std::string(text));
    }
    return {text,
            is_minus,
            IsStopWord(text)
            };
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text, bool skip_sort) const {
    Query query;
    for (std::string_view word : SplitIntoWords(text)) {
        const QueryWord query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                query.minus_words.push_back(query_word.data);
            } else {
                query.plus_words.push_back(query_word.data);
            }
        }
    }
        
    if (!skip_sort) {
        for (auto* words : {&query.plus_words, &query.minus_words}) {
            std::sort(words->begin(), words->end());
            words->erase(std::unique(words->begin(), words->end()),words->end());
        }
    }
    return query;
}

double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}
