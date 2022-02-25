#include "search_server.h"

SearchServer::SearchServer(const std::string& stop_words) {
        SetStopWords(SplitIntoWords(stop_words));
    }

void SearchServer::AddDocument(int document_id, const std::string& document, DocumentStatus status, const std::vector<int>& ratings) {

        if (document_id < 0) {
            throw std::invalid_argument("Отрицательный Id документа");
        }
        if (documents_.count(document_id) > 0) {
            throw std::invalid_argument("Документ с таким id уже есть в системе");
        }
        const std::vector<std::string> words = SplitIntoWordsNoStop(document);
        
        const int words_size = words.size();    
        const double inv_word_count = 1.0 /words_size;
        
        for (const std::string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
    
        std::set<std::string> unique_words(words.begin(), words.end());
        for (const std::string& word : unique_words){
            document_to_word_freqs_[document_id].insert({word, 
            std::count(words.begin(), words.end(), word) / static_cast<double>(words_size)});
        }
    
        documents_.emplace(document_id, 
                           DocumentData{ComputeAverageRating(ratings), status});
        document_ids_.insert(document_id);
    }

std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(
            raw_query,
            [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }

std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
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

const std::map<std::string, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static std::map<std::string, double> words_freqs;

    if (!words_freqs.empty()) {
        words_freqs.clear();
    }
    
    if (document_id < 0 || !documents_.count(document_id)) {
        return words_freqs;
    }

    words_freqs = document_to_word_freqs_.at(document_id);
    return words_freqs;
}

std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(const std::string& raw_query, int document_id) const {
        if (documents_.count(document_id) == 0) {
            throw std::invalid_argument("Документ не найден");
        }

        const Query query = ParseQuery(raw_query);
        std::vector<std::string> matched_words;
        for (const std::string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const std::string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return std::tuple{matched_words, documents_.at(document_id).status};
    }

void SearchServer::RemoveDocument(int document_id) {
    if (document_ids_.find(document_id) != document_ids_.end()) 
    {
        for (auto word : document_to_word_freqs_.at(document_id)){
            word_to_document_freqs_.erase(word.first);
        }
        document_to_word_freqs_.erase(document_id);
        documents_.erase(document_id);
        document_ids_.erase(document_id);
    }
}

bool SearchServer::IsStopWord(const std::string& word) const {
        return stop_words_.count(word) > 0;
    }

bool SearchServer::IsValidWord(const std::string& word) {
        // A valid word must not contain special characters
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }

std::vector<std::string> SearchServer::SplitIntoWordsNoStop(const std::string& text) const {
        std::vector<std::string> words;
        for (const std::string& word : SplitIntoWords(text)) {
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

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string text) const {
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

SearchServer::Query SearchServer::ParseQuery(const std::string& text) const {
        Query query;
        for (const std::string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

double SearchServer::ComputeWordInverseDocumentFreq(const std::string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }