#include "search_server.h"

SearchServer::SearchServer(const string& stop_words) {
        SetStopWords(SplitIntoWords(stop_words));
    }

void SearchServer::AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {

        if (document_id < 0) {
            throw invalid_argument("Отрицательный Id документа");
        }
        if (documents_.count(document_id) > 0) {
            throw invalid_argument("Документ с таким id уже есть в системе");
        }
        const vector<string> words = SplitIntoWordsNoStop(document);

        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        document_ids_.push_back(document_id);
    }

vector<Document> SearchServer::FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(
            raw_query,
            [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }

vector<Document> SearchServer::FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

int SearchServer::GetDocumentCount() const {
        return documents_.size();
    }

int SearchServer::GetDocumentId(int index) const {
        if (index >= 0 && index < GetDocumentCount()) {
            return document_ids_[index];
        }
        throw out_of_range("Индекс переданного документа выходит за пределы допустимого диапазона"s);
    }

tuple<vector<string>, DocumentStatus> SearchServer::MatchDocument(const string& raw_query, int document_id) const {
        if (documents_.count(document_id) == 0) {
            throw invalid_argument("Документ не найден");
        }

        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }

        return tuple{matched_words, documents_.at(document_id).status};
    }

bool SearchServer::IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

bool SearchServer::IsValidWord(const string& word) {
        // A valid word must not contain special characters
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }

vector<string> SearchServer::SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsValidWord(word)) {
            	throw invalid_argument("Некорректный ввод: "s + string(word));
            }
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

SearchServer::QueryWord SearchServer::ParseQueryWord(string text) const {
        if (text.empty()) {
        throw invalid_argument("Пустой запрос"s);
        }
        bool is_minus = false;
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw invalid_argument("Некорректный ввод: "s + string(text));
        }
        return {text,
                is_minus,
                IsStopWord(text)
                };
    }

SearchServer::Query SearchServer::ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
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

double SearchServer::ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }